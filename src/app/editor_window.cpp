#include "neomifes/app/editor_window.h"

#include <utility>

namespace neomifes::app {

EditorWindow::EditorWindow(document::Document                          initialDocument,
                           DocumentFileState                            initialFileState,
                           const std::optional<std::filesystem::path>& initialPath)
    : workspace(std::move(initialDocument), initialFileState, initialPath) {}

}  // namespace neomifes::app
