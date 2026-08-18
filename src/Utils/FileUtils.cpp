#include "vulkcore/Utils/FileUtils.hpp"

#include <fstream>
#include <stdexcept>

namespace VulkCore {

std::vector<char> readBinaryFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open binary file: " + path.string());
    }

    const std::streampos end = file.tellg();
    if (end < 0) {
        throw std::runtime_error("failed to determine binary file size: " + path.string());
    }

    std::vector<char> buffer(static_cast<size_t>(end));
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));

    if (!file && !file.eof()) {
        throw std::runtime_error("failed to read binary file: " + path.string());
    }

    return buffer;
}

} // namespace VulkCore
