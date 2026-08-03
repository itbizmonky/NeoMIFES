#include "neomifes/app/plugin_core_api_bridge.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

#include "neomifes/document/document.h"
#include "neomifes/document/text_pos.h"
#include "neomifes/plugin/plugin_host.h"
#include "neomifes/ui/command_descriptor.h"
#include "neomifes/ui/plugin_command_registry.h"
#include "neomifes/ui/toast_state.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::app {

namespace {

// Both directions of the opaque-handle idiom confined to this file - the
// only place NeoMifesDocument* and document::Document* are ever equated
// (see plugin_sdk.h's NeoMifesDocument comment).
document::Document& toDocument(NeoMifesDocument* doc) noexcept {
    return *reinterpret_cast<document::Document*>(doc);
}

void insertTextImpl(NeoMifesDocument* doc, const wchar_t* text, unsigned line, unsigned column) {
    if (doc == nullptr || text == nullptr) {
        return;
    }
    document::Document&     liveDocument = toDocument(doc);
    const document::TextPos pos =
        liveDocument.lineColumnToOffset(static_cast<document::LineNumber>(line), column);
    liveDocument.insertText(pos, util::fromWstringView(std::wstring_view(text)));
}

void deleteRangeImpl(NeoMifesDocument* doc, unsigned lineStart, unsigned columnStart,
                      unsigned lineEnd, unsigned columnEnd) {
    if (doc == nullptr) {
        return;
    }
    document::Document& liveDocument = toDocument(doc);
    document::TextPos   startOffset  = liveDocument.lineColumnToOffset(
        static_cast<document::LineNumber>(lineStart), columnStart);
    document::TextPos endOffset = liveDocument.lineColumnToOffset(
        static_cast<document::LineNumber>(lineEnd), columnEnd);
    // Untrusted plugin input - a resolved end-before-start pair is
    // normalized (swapped) rather than passed through as-is, since
    // PieceTree::eraseRange() treats start>=end as a silent no-op (see
    // ADR-016) rather than deleting what the caller evidently intended.
    if (startOffset > endOffset) {
        std::swap(startOffset, endOffset);
    }
    liveDocument.eraseRange(document::TextRange{.start = startOffset, .end = endOffset});
}

unsigned int getLineCountImpl(NeoMifesDocument* doc) {
    if (doc == nullptr) {
        return 0;
    }
    const std::uint64_t count = toDocument(doc).lineCount();
    return static_cast<unsigned int>(
        std::min<std::uint64_t>(count, std::numeric_limits<unsigned int>::max()));
}

unsigned int getLineTextImpl(NeoMifesDocument* doc, unsigned line, wchar_t* buffer,
                              unsigned bufferLen) {
    if (doc == nullptr || buffer == nullptr || bufferLen == 0) {
        return 0;
    }
    const std::u16string text = toDocument(doc).lineText(static_cast<document::LineNumber>(line));
    const std::size_t    copyLen =
        std::min(text.size(), static_cast<std::size_t>(bufferLen) - 1);
    const std::wstring_view src = util::toWstringView(text);
    std::copy_n(src.begin(), copyLen, buffer);
    buffer[copyLen] = L'\0';
    return static_cast<unsigned int>(copyLen);
}

// Both directions of the opaque-handle idiom confined to this file, same
// pattern as toDocument() above (Phase 8e).
ui::ToastState& toToastState(NeoMifesToastSink* sink) noexcept {
    return *reinterpret_cast<ui::ToastState*>(sink);
}

void showToastImpl(NeoMifesToastSink* sink, const wchar_t* message) {
    if (sink == nullptr || message == nullptr) {
        return;
    }
    toToastState(sink).show(util::fromWstringView(std::wstring_view(message)));
}

// Both directions of the opaque-handle idiom confined to this file, same
// pattern as toDocument()/toToastState() above (Phase 8f).
ui::PluginCommandRegistry& toCommandRegistry(NeoMifesCommandRegistry* registry) noexcept {
    return *reinterpret_cast<ui::PluginCommandRegistry*>(registry);
}

void registerCommandImpl(NeoMifesPluginContext* ctx, const wchar_t* id, const wchar_t* title,
                          void (*callback)(NeoMifesPluginContext*)) {
    if (ctx == nullptr || ctx->commandRegistry == nullptr || id == nullptr || title == nullptr ||
        callback == nullptr) {
        return;
    }
    toCommandRegistry(ctx->commandRegistry)
        .registerCommand(ui::CommandDescriptor{
            .id              = std::u16string(util::fromWstringView(std::wstring_view(id))),
            .title           = std::u16string(util::fromWstringView(std::wstring_view(title))),
            .keybindingLabel = u"",
            .action =
                [callback, ctx]() {
                    bool crashed = false;
                    neomifes::plugin::invokePluginCallbackSafe(callback, ctx, crashed);
                    // Not surfaced further - no error-toast/log UI exists
                    // yet for a command that crashed when run (see
                    // plugin_sdk.h's threading-contract comment).
                },
        });
}

const NeoMifesCoreApi kFullCoreApi = {
    .apiVersion      = NEOMIFES_CORE_API_VERSION,
    .insertText      = &insertTextImpl,
    .deleteRange     = &deleteRangeImpl,
    .getLineCount    = &getLineCountImpl,
    .getLineText     = &getLineTextImpl,
    .showToast       = &showToastImpl,
    .registerCommand = &registerCommandImpl,
};

// Phase 8d: returned instead of kFullCoreApi when the plugin didn't declare
// NEOMIFES_PLUGIN_PERMISSION_DOCUMENT - the 4 document function pointers
// are explicitly nullptr. A plugin calling e.g. ctx->coreApi->insertText(...)
// anyway crashes on a null-pointer call, caught by PluginHost's existing
// unconditional SEH trampoline (OnLoadCrashed) - see
// plugins/samples/permission_denied_plugin/. showToast/registerCommand are
// deliberately identical to kFullCoreApi's (Phase 8e/8f: never
// permission-gated, see plugin_sdk.h's own comments). Every field is
// listed explicitly (not left to designated-initializer zero-fill) because
// clang-cl's -Wmissing-designated-field-initializers (enabled under this
// repo's ubsan preset, /WX) rejects the omitted-field form MSVC accepts.
const NeoMifesCoreApi kDocumentDeniedCoreApi = {
    .apiVersion      = NEOMIFES_CORE_API_VERSION,
    .insertText      = nullptr,
    .deleteRange     = nullptr,
    .getLineCount    = nullptr,
    .getLineText     = nullptr,
    .showToast       = &showToastImpl,
    .registerCommand = &registerCommandImpl,
};

}  // namespace

const NeoMifesCoreApi* buildPluginCoreApi(unsigned int grantedPermissions) noexcept {
    return (grantedPermissions & NEOMIFES_PLUGIN_PERMISSION_DOCUMENT) != 0U ? &kFullCoreApi
                                                                             : &kDocumentDeniedCoreApi;
}

NeoMifesDocument* toNeoMifesDocument(document::Document& document) noexcept {
    return reinterpret_cast<NeoMifesDocument*>(&document);
}

NeoMifesToastSink* toNeoMifesToastSink(ui::ToastState& toastState) noexcept {
    return reinterpret_cast<NeoMifesToastSink*>(&toastState);
}

NeoMifesCommandRegistry* toNeoMifesCommandRegistry(ui::PluginCommandRegistry& registry) noexcept {
    return reinterpret_cast<NeoMifesCommandRegistry*>(&registry);
}

}  // namespace neomifes::app
