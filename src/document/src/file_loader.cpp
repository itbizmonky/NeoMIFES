#include "neomifes/document/file_loader.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/document/original_buffer.h"
#include "neomifes/platform/file_mapping.h"

namespace neomifes::document {

namespace {

// How much of a file's head loadFile() reads to run detectBom()/
// detectEncoding() against - matches master_roadmap.md §6's "64KB head"
// perf target and neomifes::encoding::detectEncoding()'s own doc comment.
constexpr std::uint64_t kDetectionHeadBytes = 64ULL * 1024ULL;

// Shared by loadUtf8File() (UTF-8-only, unchanged contract) and loadFile()
// (Phase 6d, any encoding) for mapping OriginalBuffer::openMemoryMapped()'s
// error. `invalidEncoding` is the LoadError to report for
// OriginalBufferError::InvalidEncoding - loadUtf8File() must keep reporting
// its existing InvalidUtf8 value (search::GrepService and existing tests
// depend on it), while loadFile() reports the newer, encoding-neutral
// InvalidEncoding so callers aren't told "InvalidUtf8" for e.g. a Shift-JIS
// decode failure.
LoadError mapOriginalBufferError(OriginalBufferError err, LoadError invalidEncoding) noexcept {
    switch (err) {
        case OriginalBufferError::NotFound:         return LoadError::NotFound;
        case OriginalBufferError::PermissionDenied: return LoadError::PermissionDenied;
        case OriginalBufferError::InvalidEncoding:  return invalidEncoding;
        case OriginalBufferError::EmptyFile:
        case OriginalBufferError::IoFailure:
        default:
            return LoadError::IoFailure;
    }
}

// loadFile()'s group-B (legacy codepage) path opens its own FileMapping
// directly (it doesn't go through OriginalBuffer::openMemoryMapped() at
// all - see file_loader.h's loadFile() doc comment), so it needs its own
// tiny FileMappingError mapper rather than mapOriginalBufferError() above.
LoadError mapFileMappingError(platform::FileMappingError err) noexcept {
    switch (err) {
        case platform::FileMappingError::NotFound:         return LoadError::NotFound;
        case platform::FileMappingError::PermissionDenied: return LoadError::PermissionDenied;
        case platform::FileMappingError::EmptyFile:
        case platform::FileMappingError::IoFailure:
        default:
            return LoadError::IoFailure;
    }
}

// Shared preflight for both loadUtf8File()/loadFile(): existence, size
// query, size-cap rejection, and the "empty file" fast path (valid, empty
// Document - OriginalBuffer::openMemoryMapped() rejects 0-byte files, so
// both callers must special-case this before ever reaching mmap). Returns
// the concrete LoadResult/LoadError to return immediately if the caller
// should stop here, or nullopt if `path` is a non-empty, appropriately
// sized file the caller should proceed to open - in which case `outSize` is
// set to the file's size, so the caller never needs its own second
// std::filesystem::file_size() call for the same path (search_grep_multi_gb_
// performance_gap.md, P1: both callers used to immediately re-query this
// themselves right after preflightFile() had already computed it).
std::optional<std::variant<LoadResult, LoadError>>
preflightFile(const std::filesystem::path& path, std::uint64_t maxBytes, std::uint64_t& outSize) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return LoadError::NotFound;
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return LoadError::IoFailure;
    }
    outSize = size;
    if (size > maxBytes) {
        return LoadError::TooLarge;
    }
    if (size == 0) {
        LoadResult result;
        result.document   = std::make_unique<Document>();
        result.hadBom     = false;
        result.byteLength = 0;
        return result;
    }
    return std::nullopt;
}

// Encoding auto-detection result: the Encoding to interpret `path`'s
// content as (a *Bom variant if a BOM was found) and how many leading
// bytes to skip before that content starts.
struct DetectedContent {
    encoding::Encoding detected   = encoding::Encoding::Utf8;
    std::uint64_t       byteOffset = 0;
};

// Reads up to kDetectionHeadBytes of `path`'s head and classifies it via
// neomifes::encoding::detectBom()/detectEncoding(), falling back to Utf8
// (byteOffset 0) when neither matches - the same default loadUtf8File()
// has always implicitly assumed for BOM-less input.
[[nodiscard]] std::variant<DetectedContent, LoadError>
detectFileEncoding(const std::filesystem::path& path, std::uint64_t fileSize) {
    std::FILE* fp = nullptr;
    if (::_wfopen_s(&fp, path.c_str(), L"rb") != 0 || fp == nullptr) {
        return LoadError::PermissionDenied;
    }
    std::vector<unsigned char> headBuf(
        static_cast<std::size_t>(std::min<std::uint64_t>(fileSize, kDetectionHeadBytes)));
    const std::size_t headRead = std::fread(headBuf.data(), 1, headBuf.size(), fp);
    (void)std::fclose(fp);
    const auto headBytes = std::as_bytes(std::span(headBuf.data(), headRead));

    if (const auto bom = encoding::detectBom(headBytes)) {
        return DetectedContent{.detected = bom->encoding, .byteOffset = bom->bomLength};
    }
    if (const auto guess = encoding::detectEncoding(headBytes)) {
        return DetectedContent{.detected = *guess, .byteOffset = 0};
    }
    return DetectedContent{.detected = encoding::Encoding::Utf8, .byteOffset = 0};
}

// Strips a *Bom suffix to the plain Encoding OriginalBuffer::
// openMemoryMapped()/neomifes::encoding::decode() expect the CONTENT to be
// interpreted as (the BOM bytes themselves are skipped via byteOffset, not
// re-verified by a second layer) - mirrors loadUtf8File()'s existing
// "FileLoader strips the BOM itself" convention.
[[nodiscard]] encoding::Encoding stripBom(encoding::Encoding e) noexcept {
    using encoding::Encoding;
    switch (e) {
        case Encoding::Utf8Bom:    return Encoding::Utf8;
        case Encoding::Utf16LeBom: return Encoding::Utf16Le;
        case Encoding::Utf16BeBom: return Encoding::Utf16Be;
        case Encoding::Utf32LeBom: return Encoding::Utf32Le;
        case Encoding::Utf32BeBom: return Encoding::Utf32Be;
        default:
            return e;  // already a plain (non-Bom) variant
    }
}

// Group A (mmap + Lazy Decode, OriginalBuffer::openMemoryMapped()) vs Group
// B (eager whole-buffer decode via neomifes::encoding::decode() +
// OriginalBuffer::fromU16String()) - see the Phase 6d plan's "design
// decision 1" for why the split exists: ISO-2022-JP's escape-sequence
// statefulness makes checkpoint-based resumption a materially harder,
// separate problem, and none of this project's personas realistically open
// multi-GB Shift-JIS/EUC-JP/ISO-2022-JP files.
[[nodiscard]] bool isLazyDecodable(encoding::Encoding e) noexcept {
    using encoding::Encoding;
    switch (e) {
        case Encoding::Utf8:
        case Encoding::Utf16Le:
        case Encoding::Utf16Be:
        case Encoding::Utf32Le:
        case Encoding::Utf32Be:
            return true;
        case Encoding::ShiftJis:
        case Encoding::EucJp:
        case Encoding::Iso2022Jp:
        default:
            return false;
    }
}

// WI-02: how much of the decoded document detectLineEndingBounded() scans -
// matches file_saver.cpp's kMaxChunkCodeUnits order of magnitude (~2MiB),
// an unbenchmarked initial value per CLAUDE.md rule 10.
constexpr std::uint64_t kLineEndingDetectionHeadCodeUnits = 1ULL << 20;

// Scans only the first kLineEndingDetectionHeadCodeUnits code units of
// `document` (never the whole thing - see LoadResult::lineEnding's doc
// comment) and classifies the line-ending convention via
// encoding::detectLineEnding(). Falls back to Lf when nothing is detected.
//
// search_grep_multi_gb_performance_gap.md (P1): uses extractNoCache(), not
// extract() - this call happens exactly once per file load (nothing re-reads
// the same [0, headEnd) range again), so caching the decoded prefix in
// OriginalBuffer's decode cache would only add bookkeeping cost and retain
// memory for text nobody asks for again - the same "used once, discard"
// pattern decode_cache_unbounded_growth.md already established
// extractNoCache()/pieceTextNoCache() for elsewhere in this codebase; this
// call site was simply missed when that fix went in. For a file at or under
// the ~1MiB bound (the common case for most real files), this decodes the
// file's ENTIRE content - previously via the caching path, so the immediate
// caller (loadUtf8File()) paid for both the decode AND a full-document cache
// entry it would never read from again.
encoding::LineEnding detectLineEndingBounded(const Document& document) {
    const auto totalLength = document.length();
    if (totalLength == 0) {
        return encoding::LineEnding::Lf;
    }
    const auto snap    = document.snapshot();
    const auto headEnd = std::min<std::uint64_t>(totalLength, kLineEndingDetectionHeadCodeUnits);
    std::u16string prefix = snap->extractNoCache(TextRange{.start = 0, .end = headEnd});
    // Truncated mid-scan and the slice ends on a lone '\r' - the code unit
    // just past the bound (not visible here) might be the paired '\n'.
    // Trim it so detectLineEnding() doesn't misreport an otherwise-uniform
    // CRLF file as Mixed (WI-02 design review finding).
    if (headEnd < totalLength && !prefix.empty() && prefix.back() == u'\r') {
        prefix.pop_back();
    }
    return encoding::detectLineEnding(prefix).value_or(encoding::LineEnding::Lf);
}

}  // namespace

namespace {

// Shared by loadUtf8File() and loadUtf8FileForGrep() - identical up through
// opening the mmap; they differ only in whether the caller needs
// LoadResult::lineEnding. `detectLineEnding = false` skips
// detectLineEndingBounded() entirely (search_grep_multi_gb_performance_gap.md,
// P1: GrepService::grepOneFile() never reads .lineEnding, so paying for a
// decode of the file's head - the file's ENTIRE content for anything at or
// under the ~1MiB bound - just to compute a field nobody looks at was pure
// waste on the multi-file grep path).
std::variant<LoadResult, LoadError>
loadUtf8FileImpl(const std::filesystem::path& path, std::uint64_t maxBytes,
                  bool detectLineEnding) {
    std::uint64_t size = 0;
    if (auto early = preflightFile(path, maxBytes, size)) {
        return std::move(*early);
    }

    // Peek the first 3 bytes for a UTF-8 BOM with a tiny fopen/fread - simpler
    // than mmap-then-inspect for just 3 bytes, and happens before the
    // (potentially large) mmap is created.
    std::FILE* fp = nullptr;
    if (::_wfopen_s(&fp, path.c_str(), L"rb") != 0 || fp == nullptr) {
        return LoadError::PermissionDenied;
    }
    std::array<unsigned char, 3> bomBuf{};
    const std::size_t bomRead = std::fread(bomBuf.data(), 1, bomBuf.size(), fp);
    (void)std::fclose(fp);
    const bool hadBom = (bomRead == 3
                        && bomBuf[0] == 0xEF && bomBuf[1] == 0xBB && bomBuf[2] == 0xBF);
    const std::uint64_t byteOffset = hadBom ? 3 : 0;

    auto opened = OriginalBuffer::openMemoryMapped(path, byteOffset, encoding::Encoding::Utf8);
    if (std::holds_alternative<OriginalBufferError>(opened)) {
        return mapOriginalBufferError(std::get<OriginalBufferError>(opened), LoadError::InvalidUtf8);
    }

    LoadResult result;
    result.document   = std::make_unique<Document>(
        std::move(std::get<std::shared_ptr<const OriginalBuffer>>(opened)));
    result.hadBom     = hadBom;
    result.byteLength = size;
    if (detectLineEnding) {
        result.lineEnding = detectLineEndingBounded(*result.document);
    }
    return result;
}

}  // namespace

std::variant<LoadResult, LoadError>
loadUtf8File(const std::filesystem::path& path, std::uint64_t maxBytes) {
    return loadUtf8FileImpl(path, maxBytes, /*detectLineEnding=*/true);
}

std::variant<LoadResult, LoadError>
loadUtf8FileForGrep(const std::filesystem::path& path, std::uint64_t maxBytes) {
    return loadUtf8FileImpl(path, maxBytes, /*detectLineEnding=*/false);
}

std::variant<LoadResult, LoadError>
loadFile(const std::filesystem::path& path, std::uint64_t maxBytes) {
    std::uint64_t size = 0;
    if (auto early = preflightFile(path, maxBytes, size)) {
        return std::move(*early);
    }

    auto detection = detectFileEncoding(path, size);
    if (std::holds_alternative<LoadError>(detection)) {
        return std::get<LoadError>(detection);
    }
    const auto& [detected, byteOffset]       = std::get<DetectedContent>(detection);
    const encoding::Encoding contentEncoding = stripBom(detected);

    LoadResult result;
    result.hadBom           = byteOffset > 0;
    result.byteLength       = size;
    result.detectedEncoding = detected;

    if (isLazyDecodable(contentEncoding)) {
        auto opened = OriginalBuffer::openMemoryMapped(path, byteOffset, contentEncoding);
        if (std::holds_alternative<OriginalBufferError>(opened)) {
            return mapOriginalBufferError(std::get<OriginalBufferError>(opened),
                                          LoadError::InvalidEncoding);
        }
        result.document = std::make_unique<Document>(
            std::move(std::get<std::shared_ptr<const OriginalBuffer>>(opened)));
        result.lineEnding = detectLineEndingBounded(*result.document);
        return result;
    }

    auto mapped = platform::FileMapping::open(path);
    if (std::holds_alternative<platform::FileMappingError>(mapped)) {
        return mapFileMappingError(std::get<platform::FileMappingError>(mapped));
    }
    auto& mapping = std::get<platform::FileMapping>(mapped);
    if (byteOffset > mapping.size()) {
        return LoadError::IoFailure;
    }
    auto decoded = encoding::decode(mapping.data().subspan(byteOffset), contentEncoding);
    if (std::holds_alternative<encoding::DecodeError>(decoded)) {
        return LoadError::InvalidEncoding;
    }
    result.document = std::make_unique<Document>(
        OriginalBuffer::fromU16String(std::move(std::get<std::u16string>(decoded))));
    result.lineEnding = detectLineEndingBounded(*result.document);
    return result;
}

}  // namespace neomifes::document
