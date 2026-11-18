#include "vulkcore/Utils/Screenshot.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace VulkCore {

namespace {

void appendU32BE(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
    output.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    output.push_back(static_cast<std::uint8_t>(value & 0xffu));
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) != 0u
                ? (crc >> 1) ^ 0xedb88320u
                : crc >> 1;
        }
    }
    return ~crc;
}

std::uint32_t adler32(const std::vector<std::uint8_t>& data) {
    constexpr std::uint32_t kModulus = 65521u;
    std::uint32_t first = 1u;
    std::uint32_t second = 0u;
    for (const std::uint8_t value : data) {
        first = (first + value) % kModulus;
        second = (second + first) % kModulus;
    }
    return (second << 16) | first;
}

void appendChunk(
    std::vector<std::uint8_t>& png,
    const std::array<std::uint8_t, 4>& type,
    const std::vector<std::uint8_t>& data
) {
    if (data.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("PNG chunk is too large");
    }
    appendU32BE(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t crcStart = png.size();
    png.insert(png.end(), type.begin(), type.end());
    png.insert(png.end(), data.begin(), data.end());
    appendU32BE(png, crc32(png.data() + crcStart, 4 + data.size()));
}

} // namespace

void writeRgbaPng(
    const std::filesystem::path& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<std::uint8_t>& rgbaPixels
) {
    if (width == 0 || height == 0) {
        throw std::invalid_argument("cannot write an empty PNG");
    }
    const std::size_t rowSize = static_cast<std::size_t>(width) * 4u;
    const std::size_t expectedSize = rowSize * static_cast<std::size_t>(height);
    if (rgbaPixels.size() != expectedSize) {
        throw std::invalid_argument("RGBA pixel data does not match PNG dimensions");
    }

    std::vector<std::uint8_t> png = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a
    };

    std::vector<std::uint8_t> header;
    appendU32BE(header, width);
    appendU32BE(header, height);
    header.insert(header.end(), {8u, 6u, 0u, 0u, 0u});
    appendChunk(png, {'I', 'H', 'D', 'R'}, header);

    std::vector<std::uint8_t> scanlines;
    scanlines.reserve((rowSize + 1u) * static_cast<std::size_t>(height));
    for (std::uint32_t row = 0; row < height; ++row) {
        scanlines.push_back(0u);
        const auto rowStart = rgbaPixels.begin() +
            static_cast<std::ptrdiff_t>(static_cast<std::size_t>(row) * rowSize);
        scanlines.insert(
            scanlines.end(),
            rowStart,
            rowStart + static_cast<std::ptrdiff_t>(rowSize)
        );
    }

    // Store the filtered scanlines in uncompressed DEFLATE blocks. This keeps
    // the screenshot path dependency-free while remaining valid PNG data.
    std::vector<std::uint8_t> compressed = {0x78u, 0x01u};
    std::size_t offset = 0;
    while (offset < scanlines.size()) {
        const std::size_t blockSize = std::min<std::size_t>(
            65535u,
            scanlines.size() - offset
        );
        const bool finalBlock = offset + blockSize == scanlines.size();
        compressed.push_back(finalBlock ? 1u : 0u);
        const auto length = static_cast<std::uint16_t>(blockSize);
        compressed.push_back(static_cast<std::uint8_t>(length & 0xffu));
        compressed.push_back(static_cast<std::uint8_t>((length >> 8) & 0xffu));
        const auto inverseLength = static_cast<std::uint16_t>(~length);
        compressed.push_back(static_cast<std::uint8_t>(inverseLength & 0xffu));
        compressed.push_back(static_cast<std::uint8_t>((inverseLength >> 8) & 0xffu));
        compressed.insert(
            compressed.end(),
            scanlines.begin() + static_cast<std::ptrdiff_t>(offset),
            scanlines.begin() + static_cast<std::ptrdiff_t>(offset + blockSize)
        );
        offset += blockSize;
    }
    appendU32BE(compressed, adler32(scanlines));
    appendChunk(png, {'I', 'D', 'A', 'T'}, compressed);
    appendChunk(png, {'I', 'E', 'N', 'D'}, {});

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open screenshot path: " + path.string());
    }
    file.write(
        reinterpret_cast<const char*>(png.data()),
        static_cast<std::streamsize>(png.size())
    );
    if (!file) {
        throw std::runtime_error("failed to write screenshot: " + path.string());
    }
}

} // namespace VulkCore
