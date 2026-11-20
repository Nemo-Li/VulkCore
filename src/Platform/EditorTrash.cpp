#include "EditorTrash.hpp"

#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>
#else
#include <cerrno>
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;
#endif

namespace VulkCore {

void trashEditorFile(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto checkResult = [](HRESULT result) {
        if (FAILED(result)) {
            throw std::system_error(static_cast<int>(result), std::system_category());
        }
    };
    checkResult(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    struct Apartment {
        ~Apartment() { CoUninitialize(); }
    } apartment;

    Microsoft::WRL::ComPtr<IFileOperation> operation;
    checkResult(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(operation.GetAddressOf())));
    // Request recycling explicitly, including when the shell cannot offer Undo.
    checkResult(operation->SetOperationFlags(FOFX_RECYCLEONDELETE | FOFX_EARLYFAILURE |
        FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI));
    Microsoft::WRL::ComPtr<IShellItem> item;
    checkResult(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(item.GetAddressOf())));
    checkResult(operation->DeleteItem(item.Get(), nullptr));
    checkResult(operation->PerformOperations());
    BOOL aborted = FALSE;
    checkResult(operation->GetAnyOperationsAborted(&aborted));
    if (aborted) throw std::runtime_error("The recycle operation did not complete.");
#else
    auto filename = path.native();
    char command[] = "gio";
    char verb[] = "trash";
    char endOptions[] = "--";
    char* const arguments[] = {command, verb, endOptions, filename.data(), nullptr};
    pid_t process = 0;
    if (const int error = posix_spawnp(&process, command, nullptr, nullptr, arguments, environ)) {
        throw std::system_error(error, std::generic_category(), "Could not start gio trash");
    }
    int status = 0;
    pid_t result;
    do {
        result = waitpid(process, &status, 0);
    } while (result == -1 && errno == EINTR);
    if (result == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error("The desktop trash service could not recycle the file.");
    }
#endif
}

} // namespace VulkCore
