#pragma once

#include <filesystem>

namespace VulkCore {

// Throws if the desktop trash service cannot recycle the file.
void trashEditorFile(const std::filesystem::path& path);

} // namespace VulkCore
