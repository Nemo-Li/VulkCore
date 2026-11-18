#pragma once

#include <filesystem>
#include <vector>

namespace VulkCore {

std::vector<char> readBinaryFile(const std::filesystem::path& path);

} // namespace VulkCore
