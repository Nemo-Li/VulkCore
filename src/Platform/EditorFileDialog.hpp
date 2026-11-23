#pragma once

#include <filesystem>

namespace VulkCore {

enum class EditorFileType { Images, Scene, RealtimeEffect };

#if defined(__APPLE__) || defined(_WIN32)
std::filesystem::path pickEditorFile(
    const char* title, EditorFileType type, const std::filesystem::path& directory
);
#endif

} // namespace VulkCore
