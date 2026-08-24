#pragma once

// XmlTreeWorker - runs parseXmlTree() (the BufferSnapshot overload) on a
// single dedicated background thread, mirroring
// neomifes::jsontree::JsonTreeWorker as the direct template (itself modeled
// on neomifes::logmode::LogIndexWorker / render::SyntaxWorker). Lives in
// neomifes::xmltree for the same layering reason JsonTreeWorker lives in
// neomifes::jsontree: this is not a rendering concern, and neomifes::xmltree
// is already a self-contained module depending only on neomifes::document.
//
// FIFO (std::deque), not SyntaxWorker's "keep only the latest pending
// request" design - same reasoning as JsonTreeWorker/LogIndexWorker's own
// header comments: this worker serves multiple independent tabs
// (EditorSession), each of which needs its own result.
//
// Simpler than every prior worker in one respect: parseXmlTree() never
// returns std::optional (see xml_tree.h's own header comment on why - it
// always produces a real XmlTree, using XmlNodeKind::Error as an in-band
// sentinel rather than an out-of-band failure). So there is no "should a
// failed parse be dropped or posted" question at all (unlike
// JsonTreeWorker's own header comment on that point, or CsvModelWorker's
// opposite choice) - workerLoop() below always posts a plain XmlTree.

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/xmltree/xml_tree.h"

namespace neomifes::xmltree {

// Posted to the target HWND on completion of every requestIndex() call.
// WM_APP+1 through WM_APP+6 are already in use codebase-wide (kMsgDeferredInit,
// kMsgSyntaxTokensReady, kMsgLogIndexReady, kMsgJsonTreeReady,
// kMsgCsvIndexReady, kMsgGitDiffReady - grep-confirmed, no central registry).
inline constexpr UINT kMsgXmlTreeReady = WM_APP + 7;

// One queued indexing request. `sessionToken` is an opaque identifier the
// caller supplies (in practice EditorSession's own `this` pointer) and this
// class never dereferences - it is only round-tripped back via
// kMsgXmlTreeReady's wParam, same contract as PendingJsonTreeRequest.
struct PendingXmlTreeRequest {
    std::shared_ptr<const document::BufferSnapshot> snapshot;
    const void*                                       sessionToken = nullptr;
};

class XmlTreeWorker {
public:
    // targetHwnd receives kMsgXmlTreeReady on completion of every request
    // this instance ever processes. The thread starts immediately and
    // blocks on m_cv until the first requestIndex() call or destruction.
    explicit XmlTreeWorker(HWND targetHwnd);
    ~XmlTreeWorker();

    XmlTreeWorker(const XmlTreeWorker&)            = delete;
    XmlTreeWorker& operator=(const XmlTreeWorker&) = delete;
    XmlTreeWorker(XmlTreeWorker&&)                 = delete;
    XmlTreeWorker& operator=(XmlTreeWorker&&)      = delete;

    // Fire-and-forget: appends to the FIFO queue (see this file's header
    // comment on why this never overwrites a still-pending request).
    // `sessionToken` is opaque - see PendingXmlTreeRequest. Safe to call
    // only from the UI thread (same single-writer assumption as every other
    // worker-request API in this codebase).
    void requestIndex(std::shared_ptr<const document::BufferSnapshot> snapshot,
                      const void* sessionToken) noexcept;

private:
    void workerLoop();

    HWND m_targetHwnd;

    // Declared BEFORE m_thread deliberately (JsonTreeWorker/LogIndexWorker's
    // own precedent): member construction follows declaration order
    // regardless of the constructor's init-list order, and the constructor
    // starts m_thread running workerLoop() immediately - workerLoop() must
    // never observe a not-yet-constructed mutex/condition_variable.
    std::mutex                          m_mutex;
    std::condition_variable             m_cv;
    std::deque<PendingXmlTreeRequest>   m_pending;  // guarded by m_mutex; FIFO, never overwritten
    bool                                 m_shuttingDown = false;

    std::thread m_thread;
};

}  // namespace neomifes::xmltree
