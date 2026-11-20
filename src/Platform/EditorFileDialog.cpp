#include "EditorFileDialog.hpp"

#include <array>
#include <string_view>
#include <windows.h>
#include <commdlg.h>

namespace VulkCore {

std::filesystem::path pickEditorFile(
    const char* title, bool scene, const std::filesystem::path& directory
) {
    std::array<wchar_t, 32768> path{};
    const std::string_view utf8Title(title);
    const auto dialogTitle = std::filesystem::path(
        std::u8string(utf8Title.begin(), utf8Title.end())).wstring();
    OPENFILENAMEW options{};
    options.lStructSize = sizeof(options);
    options.hwndOwner = GetActiveWindow();
    options.lpstrTitle = dialogTitle.c_str();
    options.lpstrFilter = scene ? L"VulkCore scenes (*.scene)\0*.scene\0" :
        L"Images\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.psd;*.hdr\0";
    options.lpstrFile = path.data();
    options.nMaxFile = static_cast<DWORD>(path.size());
    options.lpstrInitialDir = directory.empty() ? nullptr : directory.c_str();
    options.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&options) ? std::filesystem::path(path.data()) : std::filesystem::path{};
}

} // namespace VulkCore
