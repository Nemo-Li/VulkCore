#include "EditorFileDialog.hpp"

#include <array>
#include <string_view>
#include <windows.h>
#include <commdlg.h>

namespace VulkCore {

std::filesystem::path pickEditorFile(
    const char* title, EditorFileType type, const std::filesystem::path& directory
) {
    std::array<wchar_t, 32768> path{};
    const std::string_view utf8Title(title);
    const auto dialogTitle = std::filesystem::path(
        std::u8string(utf8Title.begin(), utf8Title.end())).wstring();
    OPENFILENAMEW options{};
    options.lStructSize = sizeof(options);
    options.hwndOwner = GetActiveWindow();
    options.lpstrTitle = dialogTitle.c_str();
    switch (type) {
        case EditorFileType::Scene:
            options.lpstrFilter = L"VulkCore scenes (*.scene)\0*.scene\0";
            break;
        case EditorFileType::RealtimeEffect:
            options.lpstrFilter = L"VulkCore realtime effects (*.vfx3d)\0*.vfx3d\0";
            break;
        case EditorFileType::Images:
            options.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.psd;*.hdr\0";
            break;
    }
    options.lpstrFile = path.data();
    options.nMaxFile = static_cast<DWORD>(path.size());
    options.lpstrInitialDir = directory.empty() ? nullptr : directory.c_str();
    options.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&options) ? std::filesystem::path(path.data()) : std::filesystem::path{};
}

} // namespace VulkCore
