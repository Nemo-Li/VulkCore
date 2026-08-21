#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace VulkCore {

void writeRgbaPng(
    const std::filesystem::path& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<std::uint8_t>& rgbaPixels
);

} // namespace VulkCore
