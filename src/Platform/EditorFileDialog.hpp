#pragma once

#include <filesystem>

namespace VulkCore {

#if defined(__APPLE__) || defined(_WIN32)
std::filesystem::path pickEditorFile(
    const char* title, bool scene, const std::filesystem::path& directory
);
#endif

} // namespace VulkCore
