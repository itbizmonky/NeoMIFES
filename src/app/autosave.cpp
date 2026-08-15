#include "neomifes/app/autosave.h"

#include <array>
#include <system_error>

#include "neomifes/app/editor_session.h"
#include "neomifes/document/file_saver.h"
#include "neomifes/util/hash.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::app {

namespace {

// Same normalize-for-identity-comparison helper as Workspace::openFile()'s
// own canonicalOrSelf() (workspace.cpp) - kept as an independent local copy,
// matching this codebase's established "small, single-purpose helper, not
// worth a cross-module extraction" precedent.
std::filesystem::path canonicalOrSelf(const std::filesystem::path& p) {
    std::error_code             ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(p, ec);
    return ec ? p : canonical;
}

constexpr std::array<char16_t, 16> kHexDigits{u'0', u'1', u'2', u'3', u'4', u'5', u'6', u'7',
                                               u'8', u'9', u'a', u'b', u'c', u'd', u'e', u'f'};

// 16 lowercase hex digits (the full 64-bit range, most-significant nibble
// first).
std::u16string toHex16(std::uint64_t value) {
    std::u16string hex(16, u'0');
    for (std::size_t i = 16; i > 0; --i) {
        hex[i - 1] = kHexDigits.at(value & 0xFU);
        value >>= 4;
    }
    return hex;
}

}  // namespace

std::u16string autosaveHashFor(const std::filesystem::path& documentPath) {
    const std::filesystem::path normalized = canonicalOrSelf(documentPath);
    const std::u16string        asU16(util::fromWstringView(normalized.wstring()));
    return toHex16(util::fnv1aHash64(asU16));
}

std::filesystem::path autosaveFilePathFor(const std::filesystem::path& autosaveDir,
                                          const std::filesystem::path& documentPath) {
    std::filesystem::path result = autosaveDir;
    result /= std::filesystem::path(std::wstring(util::toWstringView(autosaveHashFor(documentPath))));
    result += L".tmp";
    return result;
}

void performAutoSave(EditorSession& session, const std::filesystem::path& autosaveDir,
                     core::AutosaveIndex& index, const std::filesystem::path& indexPath) {
    if (session.isUntitled() || !session.isDirty()) {
        return;
    }
    const std::u16string        hash         = autosaveHashFor(session.path());
    const std::filesystem::path autosavePath = autosaveFilePathFor(autosaveDir, session.path());
    const auto&                 fileState    = session.fileState();

    const auto err = document::saveFile(session.document(), autosavePath, fileState.encoding,
                                        fileState.lineEnding, fileState.writeBom,
                                        /*keepBackup=*/false, /*markAsSaved=*/false);
    if (err) {
        return;  // best-effort - see this header's own comment
    }
    index.record(hash, session.path());
    index.saveTo(indexPath);
}

void clearAutoSave(const EditorSession& session, const std::filesystem::path& autosaveDir,
                   core::AutosaveIndex& index, const std::filesystem::path& indexPath) {
    if (session.isUntitled()) {
        return;
    }
    const std::u16string hash = autosaveHashFor(session.path());
    std::error_code       ec;
    std::filesystem::remove(autosaveFilePathFor(autosaveDir, session.path()), ec);
    index.remove(hash);
    index.saveTo(indexPath);
}

std::vector<RecoverableAutoSave> scanForRecoverableAutoSaves(const std::filesystem::path& autosaveDir,
                                                              const core::AutosaveIndex&    index) {
    std::vector<RecoverableAutoSave> result;
    for (const auto& entry : index.entries()) {
        std::filesystem::path tmpPath = autosaveDir;
        tmpPath /= std::filesystem::path(std::wstring(util::toWstringView(entry.hash)));
        tmpPath += L".tmp";

        std::error_code tmpExistsEc;
        if (!std::filesystem::exists(tmpPath, tmpExistsEc) || tmpExistsEc) {
            continue;  // stale index entry (e.g. manually deleted) - nothing to recover
        }

        std::error_code realExistsEc;
        const bool       realFileExists = std::filesystem::exists(entry.originalPath, realExistsEc);

        bool isCandidate = false;
        if (!realFileExists) {
            isCandidate = true;
        } else {
            std::error_code tmpTimeEc;
            std::error_code realTimeEc;
            const auto       tmpTime  = std::filesystem::last_write_time(tmpPath, tmpTimeEc);
            const auto       realTime = std::filesystem::last_write_time(entry.originalPath, realTimeEc);
            isCandidate                = !tmpTimeEc && !realTimeEc && tmpTime > realTime;
        }

        if (isCandidate) {
            result.push_back(RecoverableAutoSave{
                .autosaveTmpPath = tmpPath, .originalPath = entry.originalPath, .hash = entry.hash});
        }
    }
    return result;
}

}  // namespace neomifes::app
