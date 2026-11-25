#include "vulkcore/Utils/SbfresLoader.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace VulkCore {
namespace {

using Bytes = std::span<const std::uint8_t>;
constexpr std::size_t kArchiveLimit = 256u * 1024u * 1024u;
constexpr std::size_t kTextureLimit = 128u * 1024u * 1024u;
constexpr std::size_t kVertexLimit = 1'000'000;
constexpr std::size_t kIndexLimit = 6'000'000;

class Binary {
public:
    Bytes bytes;
    std::string_view kind;

    [[noreturn]] void fail(std::string_view reason) const {
        throw std::runtime_error(std::string(kind) + ": " + std::string(reason));
    }

    Bytes slice(std::size_t offset, std::size_t size) const {
        if (offset > bytes.size() || size > bytes.size() - offset) fail("truncated or invalid range");
        return bytes.subspan(offset, size);
    }

    bool hasMagic(std::size_t offset, std::string_view value) const {
        return offset <= bytes.size() && value.size() <= bytes.size() - offset &&
            std::equal(value.begin(), value.end(), bytes.begin() + offset);
    }

    void magic(std::size_t offset, std::string_view value) const {
        if (!hasMagic(offset, value)) fail("expected " + std::string(value));
    }

    std::uint8_t u8(std::size_t offset) const { return slice(offset, 1)[0]; }
    std::uint16_t u16(std::size_t offset) const {
        const auto b = slice(offset, 2);
        return std::uint16_t(b[0] | (b[1] << 8));
    }
    std::uint32_t u32(std::size_t offset) const {
        return u16(offset) | (std::uint32_t(u16(offset + 2)) << 16);
    }
    float f32(std::size_t offset) const { return std::bit_cast<float>(u32(offset)); }

    std::size_t pointer(std::size_t field) const {
        const std::uint64_t target = u32(field) | (std::uint64_t(u32(field + 4)) << 32);
        if (target > bytes.size()) fail("pointer outside file");
        return static_cast<std::size_t>(target);
    }

    std::size_t table(std::size_t field, std::size_t count, std::size_t stride) const {
        if (count == 0) return 0;
        const auto offset = pointer(field);
        if (offset == 0 || count > (bytes.size() - offset) / stride) fail("invalid table");
        return offset;
    }

    std::string string(std::size_t field) const {
        const auto offset = pointer(field);
        if (offset == 0) return {};
        const auto value = slice(offset + 2, u16(offset));
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }

    std::vector<std::string> dictionary(std::size_t field) const {
        const auto offset = pointer(field);
        if (offset == 0) return {};
        const auto count = u32(offset + 4);
        if (count > 65535) fail("invalid dictionary count");
        // ResDict nodes contain a search key, two child indices and a name pointer.
        // The value array follows the order of the keys after the root node.
        slice(offset + 8, (std::size_t(count) + 1) * 16);
        std::vector<std::string> keys;
        keys.reserve(count);
        for (std::size_t i = 0; i < count; ++i) keys.push_back(string(offset + 32 + i * 16));
        return keys;
    }
};

std::vector<std::uint8_t> readArchive(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(kArchiveLimit)) {
        throw std::runtime_error("cannot open archive or invalid file size: " + path.string());
    }
    std::vector<std::uint8_t> input(static_cast<std::size_t>(size));
    if (!file.seekg(0).read(reinterpret_cast<char*>(input.data()), size)) {
        throw std::runtime_error("failed to read archive: " + path.string());
    }
    const Binary stream{input, "Yaz0"};
    if (!stream.hasMagic(0, "Yaz0")) return input;
    stream.slice(0, 16);
    const auto outputSize = (std::uint32_t(stream.u8(4)) << 24) |
        (std::uint32_t(stream.u8(5)) << 16) | (std::uint32_t(stream.u8(6)) << 8) | stream.u8(7);
    if (outputSize == 0 || outputSize > kArchiveLimit) stream.fail("invalid decoded size");

    std::vector<std::uint8_t> output;
    output.reserve(outputSize);
    std::size_t cursor = 16;
    while (output.size() < outputSize) {
        const auto control = stream.u8(cursor++);
        for (unsigned mask = 0x80; mask != 0 && output.size() < outputSize; mask >>= 1) {
            if (control & mask) {
                output.push_back(stream.u8(cursor++));
                continue;
            }
            const auto first = stream.u8(cursor++);
            const std::size_t distance = ((first & 0x0f) << 8) + stream.u8(cursor++) + 1;
            const std::size_t length = (first >> 4) ? (first >> 4) + 2 : stream.u8(cursor++) + 18;
            if (distance > output.size() || length > outputSize - output.size()) {
                stream.fail("invalid back reference");
            }
            for (std::size_t i = 0; i < length; ++i) output.push_back(output[output.size() - distance]);
        }
    }
    return output;
}

Binary fres(Bytes bytes) {
    Binary file{bytes, "BFRES"};
    file.magic(0, "FRES");
    if (file.u16(0x0c) != 0xfeff || (file.u32(8) >> 16) != 5) {
        file.fail("expected little-endian Switch version 5.x");
    }
    const auto size = file.u32(0x1c);
    if (size < 0xd0) file.fail("invalid header size");
    file.bytes = file.slice(0, size);
    return file;
}

std::size_t roundUp(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// Tegra groups 64-byte-wide, 8-row GOBs vertically into block-height groups.
std::size_t blockLinearOffset(std::size_t xBytes, std::size_t y, std::size_t pitch,
                              std::size_t blockHeight) {
    const auto groupRows = 8 * blockHeight;
    const auto group = (y / groupRows) * pitch * groupRows;
    const auto gob = (xBytes / 64) * 512 * blockHeight + (y % groupRows / 8) * 512;
    const auto column = xBytes % 64;
    const auto row = y % 8;
    return group + gob + (column / 32) * 256 + (row / 2) * 64 +
        (column % 32 / 16) * 32 + (row % 2) * 16 + column % 16;
}

using Pixel = std::array<std::uint8_t, 4>;

Pixel rgb565(std::uint16_t value) {
    const unsigned r = (value >> 11) & 31;
    const unsigned g = (value >> 5) & 63;
    const unsigned b = value & 31;
    return {std::uint8_t((r << 3) | (r >> 2)), std::uint8_t((g << 2) | (g >> 4)),
            std::uint8_t((b << 3) | (b >> 2)), 255};
}

void decodeBcColor(const Binary& block, std::array<Pixel, 16>& pixels, bool forceOpaque) {
    const auto first = block.u16(0);
    const auto second = block.u16(2);
    const bool fourColors = forceOpaque || first > second;
    std::array<Pixel, 4> colors{rgb565(first), rgb565(second), Pixel{0, 0, 0, 255}, Pixel{}};
    for (std::size_t c = 0; c < 3; ++c) {
        colors[2][c] = fourColors ? (2 * colors[0][c] + colors[1][c]) / 3
                                  : (colors[0][c] + colors[1][c]) / 2;
        if (fourColors) colors[3][c] = (colors[0][c] + 2 * colors[1][c]) / 3;
    }
    if (fourColors) colors[3][3] = 255;
    const auto selectors = block.u32(4);
    for (unsigned i = 0; i < 16; ++i) pixels[i] = colors[(selectors >> (2 * i)) & 3];
}

void decodeBcChannel(const Binary& block, std::array<Pixel, 16>& pixels, std::size_t channel) {
    const unsigned first = block.u8(0);
    const unsigned second = block.u8(1);
    std::array<unsigned, 8> values{first, second};
    const unsigned steps = first > second ? 7 : 5;
    for (unsigned i = 1; i < steps; ++i) values[i + 1] = ((steps - i) * first + i * second) / steps;
    if (steps == 5) values[7] = 255;
    const std::uint64_t selectors = block.u32(2) | (std::uint64_t(block.u16(6)) << 32);
    for (unsigned i = 0; i < 16; ++i) pixels[i][channel] = values[(selectors >> (3 * i)) & 7];
}

float half(std::uint16_t value);

EmbeddedTextureData decodeTexture(const Binary& bntx, std::size_t header, std::size_t budget,
                                  bool normalMap, std::uint32_t layer = 0) {
    const auto width = bntx.u32(header + 0x24);
    const auto height = bntx.u32(header + 0x28);
    const auto format = bntx.u32(header + 0x1c);
    const auto tile = bntx.u8(header + 0x10);
    const auto blockHeightLog = bntx.u32(header + 0x34) & 7;
    const auto mips = bntx.u16(header + 0x16);
    const auto layers = bntx.u32(header + 0x30);
    const auto dimension = bntx.u32(header + 0x5c);
    if (width == 0 || height == 0 || width > 8192 || height > 8192 ||
        bntx.u32(header + 0x2c) != 1 || (dimension != 1 && dimension != 5) ||
        (dimension == 1 && layers != 1) || layers == 0 || tile > 1 || blockHeightLog > 5 || mips == 0) {
        bntx.fail("expected a supported 2D texture layout");
    }
    if (layer >= layers) bntx.fail("array layer out of range");
    std::size_t bytesPerBlock = 0;
    const auto codec = format >> 8;
    switch (format) {
    case 0x0201: bytesPerBlock = 1; break; // R8
    case 0x0b01: case 0x0b06: bytesPerBlock = 4; break; // RGBA8
    case 0x1505: bytesPerBlock = 8; break; // RGBA16 float
    case 0x1a01: case 0x1a06: case 0x1d01: bytesPerBlock = 8; break; // BC1, BC4
    case 0x1b01: case 0x1b06: case 0x1c01: case 0x1c06: case 0x1e01:
        bytesPerBlock = 16; break; // BC2, BC3, BC5
    default: bntx.fail("unsupported texture format " + std::to_string(format));
    }
    const auto mipTable = bntx.table(header + 0x70, mips, 8);
    const auto mip0 = bntx.pointer(mipTable);
    const auto surface = bntx.slice(mip0, bntx.u32(header + 0x50));
    // Each layer stores a complete mip chain. The BRTI mip offsets address layer 0.
    const auto layerStride = surface.size() / layers;
    if (mip0 == 0 || layerStride == 0 || surface.size() % layers != 0) {
        bntx.fail("invalid array layer size");
    }
    std::size_t mip0Size = layerStride;
    std::size_t previous = mip0;
    for (std::size_t level = 1; level < mips; ++level) {
        const auto offset = bntx.pointer(mipTable + 8 * level);
        if (offset <= previous || offset - mip0 >= layerStride) bntx.fail("invalid mip offsets");
        if (level == 1) mip0Size = offset - mip0;
        previous = offset;
    }
    const Binary source{surface.subspan(layerStride * layer, mip0Size), "BNTX mip 0"};

    const std::size_t blockSize = codec >= 0x1a ? 4 : 1;
    const auto blocksWide = (width + blockSize - 1) / blockSize;
    const auto blocksHigh = (height + blockSize - 1) / blockSize;
    const auto pitch = roundUp(blocksWide * bytesPerBlock, tile == 0 ? 32 : 64);
    const auto pixelBytes = std::size_t(width) * height * 4;
    if (pixelBytes > budget) bntx.fail("decoded texture budget exceeded");
    EmbeddedTextureData result;
    result.width = width;
    result.height = height;
    result.srgb = !normalMap && (format & 0xff) == 6;
    result.data.resize(pixelBytes);
    std::array<unsigned, 4> selectors{};
    for (std::size_t c = 0; c < selectors.size(); ++c) {
        selectors[c] = bntx.u8(header + 0x58 + c);
        if (selectors[c] > 5) bntx.fail("invalid channel selector");
    }

    for (std::size_t y = 0; y < blocksHigh; ++y) {
        for (std::size_t x = 0; x < blocksWide; ++x) {
            const auto offset = tile == 0 ? y * pitch + x * bytesPerBlock :
                blockLinearOffset(x * bytesPerBlock, y, pitch, std::size_t(1) << blockHeightLog);
            const Binary block{source.slice(offset, bytesPerBlock), source.kind};
            std::array<Pixel, 16> pixels{};
            pixels.fill({0, 0, 0, 255});
            switch (codec) {
            case 0x02: pixels[0][0] = block.u8(0); break;
            case 0x0b: std::copy_n(block.bytes.begin(), 4, pixels[0].begin()); break;
            case 0x15:
                for (std::size_t c = 0; c < pixels[0].size(); ++c) {
                    const float value = half(block.u16(c * 2));
                    pixels[0][c] = std::uint8_t(std::round(255.0f *
                        (std::isnan(value) ? 0.0f : std::clamp(value, 0.0f, 1.0f))));
                }
                break;
            case 0x1a: decodeBcColor(block, pixels, false); break;
            case 0x1b:
            case 0x1c:
                decodeBcColor({block.bytes.subspan(8), source.kind}, pixels, true);
                if (codec == 0x1c) {
                    decodeBcChannel(block, pixels, 3);
                } else {
                    for (unsigned i = 0; i < 16; ++i) {
                        pixels[i][3] = ((block.u8(i / 2) >> (4 * (i % 2))) & 15) * 17;
                    }
                }
                break;
            case 0x1d: decodeBcChannel(block, pixels, 0); break;
            case 0x1e:
                decodeBcChannel(block, pixels, 0);
                decodeBcChannel({block.bytes.subspan(8), source.kind}, pixels, 1);
                break;
            }
            for (std::size_t row = 0; row < blockSize && y * blockSize + row < height; ++row) {
                for (std::size_t col = 0; col < blockSize && x * blockSize + col < width; ++col) {
                    const auto& pixel = pixels[row * blockSize + col];
                    const auto dest = ((y * blockSize + row) * width + x * blockSize + col) * 4;
                    for (std::size_t c = 0; c < selectors.size(); ++c) {
                        const auto component = selectors[c];
                        result.data[dest + c] = component >= 2 ? pixel[component - 2] : component * 255;
                    }
                    if (normalMap && codec == 0x1e) {
                        const auto nx = result.data[dest] / 127.5f - 1.0f;
                        const auto ny = result.data[dest + 1] / 127.5f - 1.0f;
                        result.data[dest + 2] = std::uint8_t(std::round(127.5f *
                            (1.0f + std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny)))));
                    }
                }
            }
        }
    }
    return result;
}

struct TextureSource { Binary pack; std::size_t header; };

std::vector<TextureSource> readTextureSources(const Binary& file) {
    std::vector<TextureSource> sources;
    const auto count = file.u16(0xc8);
    const auto external = file.table(0x98, count, 16);
    for (std::size_t i = 0; i < count; ++i) {
        const auto entry = external + 16 * i;
        Binary pack{file.slice(file.pointer(entry), file.u32(entry + 8)), "BNTX"};
        if (!pack.hasMagic(0, "BNTX")) continue;
        pack.magic(0x20, "NX  ");
        if (pack.u16(0x0c) != 0xfeff) pack.fail("unsupported byte order");
        pack.bytes = pack.slice(0, pack.u32(0x1c));
        const auto textures = pack.u32(0x24);
        if (textures > 65535 - sources.size()) pack.fail("texture count exceeds budget");
        const auto table = pack.table(0x28, textures, 8);
        for (std::size_t j = 0; j < textures; ++j) {
            const auto header = pack.pointer(table + 8 * j);
            pack.magic(header, "BRTI");
            sources.push_back({pack, header});
        }
    }
    return sources;
}

class TexturePacks {
public:
    explicit TexturePacks(bool preview) : preview_(preview) {}

    void add(const std::vector<TextureSource>& sources) {
        for (const auto& source : sources) {
            // A companion added after the model can replace an embedded entry.
            sources_.insert_or_assign(source.pack.string(source.header + 0x60), source);
        }
    }

    EmbeddedTextureData resolve(const std::string& name, bool normalMap) {
        const auto source = sources_.find(name);
        if (source == sources_.end()) return {};
        const auto& [pack, header] = source->second;
        if (pack.u32(header + 0x30) > 1) {
            // The regular material shader has no source-specific array layer mapping.
            if (preview_) return {};
            pack.fail("expected a supported 2D texture layout");
        }
        const auto key = (normalMap ? "normal:" : "color:") + name;
        auto cached = decoded_.find(key);
        if (cached == decoded_.end()) {
            auto texture = decodeTexture(pack, header, remaining_, normalMap);
            remaining_ -= texture.data.size();
            cached = decoded_.emplace(key, std::move(texture)).first;
        }
        if (cached->second.data.size() > remaining_) source->second.pack.fail("texture budget exceeded");
        remaining_ -= cached->second.data.size();
        return cached->second;
    }

private:
    bool preview_;
    std::unordered_map<std::string, TextureSource> sources_;
    std::unordered_map<std::string, EmbeddedTextureData> decoded_;
    std::size_t remaining_ = kTextureLimit;
};

MaterialData readMaterial(const Binary& file, std::size_t offset, TexturePacks& textures) {
    file.magic(offset, "FMAT");
    MaterialData result;
    result.name = file.string(offset + 0x10);
    const auto samplerNames = file.dictionary(offset + 0x50);
    const auto textureCount = file.u8(offset + 0xa8);
    const auto textureNames = file.table(offset + 0x38, textureCount, 8);
    std::string maskName;
    for (std::size_t i = 0; i < std::min<std::size_t>(samplerNames.size(), textureCount); ++i) {
        const auto& sampler = samplerNames[i];
        if (sampler == "_a0" || sampler == "_albedo0") {
            result.embeddedTexture = textures.resolve(file.string(textureNames + 8 * i), false);
        } else if (sampler == "_n0" || sampler == "_normal0") {
            result.embeddedNormalTexture = textures.resolve(file.string(textureNames + 8 * i), true);
        } else if (sampler == "_ms0") {
            maskName = file.string(textureNames + 8 * i);
        }
    }

    const auto renderKeys = file.dictionary(offset + 0x20);
    const auto infoArray = file.table(offset + 0x18, renderKeys.size(), 24);
    bool alphaTest = false;
    float cutoff = 0.5f;
    for (std::size_t i = 0; i < renderKeys.size(); ++i) {
        const auto& key = renderKeys[i];
        const auto info = infoArray + 24 * i;
        if (file.u16(info + 16) == 0) continue;
        const auto type = file.u8(info + 18);
        if (key == "gsys_alpha_test_value" && type == 1) {
            const auto value = file.f32(file.table(info + 8, 1, 4));
            if (std::isfinite(value)) cutoff = std::clamp(value, 0.0f, 1.0f);
        } else if (type == 2 && (key == "gsys_alpha_test_enable" ||
                   key == "gsys_render_state_display_face" || key == "gsys_render_state_mode")) {
            const auto value = file.string(file.table(info + 8, 1, 8));
            if (key == "gsys_alpha_test_enable") alphaTest = value == "true";
            if (key == "gsys_render_state_display_face") result.doubleSided = value == "both";
            if (key == "gsys_render_state_mode" && value == "translucent") {
                result.blendMode = MaterialData::BlendMode::AlphaBlend;
                result.depthWrite = false;
            }
        }
    }
    if (alphaTest) {
        result.alphaCutoff = cutoff;
        if (!maskName.empty()) result.embeddedAlphaMask = textures.resolve(maskName, false);
    }
    return result;
}

float half(std::uint16_t value) {
    const auto mantissa = value & 0x3ff;
    const int exponent = (value >> 10) & 31;
    const float magnitude = exponent == 0 ? std::ldexp(float(mantissa), -24) :
        exponent == 31 ? std::numeric_limits<float>::quiet_NaN() :
        std::ldexp(float(1024 + mantissa), exponent - 25);
    return (value & 0x8000) ? -magnitude : magnitude;
}

float snorm(unsigned value, unsigned bits) {
    const int sign = 1 << (bits - 1);
    const int signedValue = static_cast<int>(value ^ sign) - sign;
    return std::max(-1.0f, float(signedValue) / float(sign - 1));
}

struct AttributeFormat {
    enum class Type { Float, Half, Unorm, Snorm, Unsigned, PackedNormal };
    Type type;
    unsigned components;
    unsigned componentBytes;
    unsigned size() const { return components * componentBytes; }
};

AttributeFormat attributeFormat(const Binary& file, unsigned format) {
    using Type = AttributeFormat::Type;
    switch (format) {
    case 0x0517: return {Type::Float, 2, 4};
    case 0x0518: return {Type::Float, 3, 4};
    case 0x0519: return {Type::Float, 4, 4};
    case 0x0512: return {Type::Half, 2, 2};
    case 0x0515: return {Type::Half, 4, 2};
    case 0x0112: return {Type::Unorm, 2, 2};
    case 0x0115: return {Type::Unorm, 4, 2};
    case 0x0212: return {Type::Snorm, 2, 2};
    case 0x0215: return {Type::Snorm, 4, 2};
    case 0x0109: return {Type::Unorm, 2, 1};
    case 0x0209: return {Type::Snorm, 2, 1};
    case 0x0309: return {Type::Unsigned, 2, 1};
    case 0x010b: return {Type::Unorm, 4, 1};
    case 0x020b: return {Type::Snorm, 4, 1};
    case 0x030b: return {Type::Unsigned, 4, 1};
    case 0x020e: return {Type::PackedNormal, 4, 1};
    default: file.fail("unsupported vertex attribute format " + std::to_string(format));
    }
}

glm::vec4 readAttribute(const Binary& buffer, std::size_t offset, const AttributeFormat& format) {
    using Type = AttributeFormat::Type;
    if (format.type == Type::PackedNormal) {
        const auto packed = buffer.u32(offset);
        return {snorm(packed & 1023, 10), snorm((packed >> 10) & 1023, 10),
                snorm((packed >> 20) & 1023, 10), float(packed >> 30) / 3.0f};
    }
    glm::vec4 result(0.0f, 0.0f, 0.0f, 1.0f);
    for (unsigned c = 0; c < format.components; ++c, offset += format.componentBytes) {
        switch (format.type) {
        case Type::Float: result[c] = buffer.f32(offset); break;
        case Type::Half: result[c] = half(buffer.u16(offset)); break;
        case Type::Unorm:
            result[c] = format.componentBytes == 1 ? buffer.u8(offset) / 255.0f :
                buffer.u16(offset) / 65535.0f;
            break;
        case Type::Snorm:
            result[c] = format.componentBytes == 1 ? snorm(buffer.u8(offset), 8) :
                snorm(buffer.u16(offset), 16);
            break;
        case Type::Unsigned:
            result[c] = static_cast<float>(buffer.u8(offset));
            break;
        case Type::PackedNormal: break;
        }
        if (!std::isfinite(result[c])) buffer.fail("non-finite vertex attribute");
    }
    return result;
}

struct VertexStream { Binary buffer; std::size_t stride; };

void appendVertices(
    const Binary& file,
    const Binary& gpu,
    std::size_t offset,
    std::size_t count,
    const glm::mat4& transform,
    std::size_t skinCount,
    std::size_t skeletonBoneCount,
    const std::vector<std::uint16_t>& matrixToBoneIndices,
    std::size_t skeletonBoneOffset,
    MeshData& meshData
) {
    if (skinCount > 4) file.fail("BFRES vertex skinning has more than four influences");
    const auto attributeCount = file.u8(offset + 0x54);
    const auto streamCount = file.u8(offset + 0x55);
    const auto attributes = file.table(offset + 0x10, attributeCount, 16);
    const auto sizes = file.table(offset + 0x38, streamCount, 16);
    const auto strides = file.table(offset + 0x40, streamCount, 16);
    std::vector<VertexStream> streams;
    auto streamOffset = std::size_t(file.u32(offset + 0x50));
    for (std::size_t i = 0; i < streamCount; ++i) {
        const auto size = file.u32(sizes + i * 16);
        const auto stride = file.u32(strides + i * 16);
        streamOffset = roundUp(streamOffset, 8);
        if (stride == 0 || count > size / stride) file.fail("invalid FVTX stream stride or size");
        streams.push_back({{gpu.slice(streamOffset, size), "FVTX stream"}, stride});
        streamOffset += size;
    }

    const auto vertexStart = meshData.vertices.size();
    meshData.vertices.resize(vertexStart + count, Vertex{{}, {0.0f, 1.0f, 0.0f}, {}, glm::vec4(1.0f)});
    bool hasPositions = false;
    for (std::size_t i = 0; i < attributeCount; ++i) {
        const auto attribute = attributes + i * 16;
        const auto name = file.string(attribute);
        if (name != "_p0" && name != "_n0" && name != "_u0" && name != "_c0" &&
            name != "_i0" && name != "_i1" && name != "_w0" && name != "_w1") {
            continue;
        }
        const auto type = (file.u8(attribute + 8) << 8) | file.u8(attribute + 9);
        const auto format = attributeFormat(file, type);
        const auto index = file.u16(attribute + 14);
        const auto elementOffset = file.u16(attribute + 12);
        if (index >= streams.size() || elementOffset + format.size() > streams[index].stride) {
            file.fail("vertex attribute outside stream stride");
        }
        const auto& stream = streams[index];
        hasPositions |= name == "_p0";
        for (std::size_t j = 0; j < count; ++j) {
            auto& vertex = meshData.vertices[vertexStart + j];
            const auto value = readAttribute(stream.buffer, j * stream.stride + elementOffset, format);
            if (name == "_p0") vertex.position = glm::vec3(value);
            if (name == "_n0") vertex.normal = glm::vec3(value);
            if (name == "_u0") vertex.texCoord = glm::vec2(value);
            if (name == "_c0") vertex.color = value;
            if (skinCount > 0 && (name == "_i0" || name == "_i1")) {
                const int firstInfluence = name == "_i0" ? 0 : 4;
                for (std::size_t influence = 0; influence < 4 &&
                     firstInfluence + static_cast<int>(influence) < static_cast<int>(skinCount);
                     ++influence) {
                    // Vertex skin attributes address the matrix palette. BFRES stores
                    // the corresponding skeleton bone in MatrixToBoneList.
                    const auto localIndex = static_cast<std::size_t>(std::lround(value[influence]));
                    if (localIndex >= matrixToBoneIndices.size()) {
                        file.fail("vertex skin matrix index outside FSKL");
                    }
                    const auto boneIndex = matrixToBoneIndices[localIndex];
                    if (boneIndex >= skeletonBoneCount) {
                        file.fail("vertex skin bone index outside FSKL");
                    }
                    vertex.boneIndices[firstInfluence + static_cast<int>(influence)] =
                        static_cast<int>(skeletonBoneOffset + boneIndex);
                }
            }
            if (skinCount > 0 && (name == "_w0" || name == "_w1")) {
                const int firstInfluence = name == "_w0" ? 0 : 4;
                for (std::size_t influence = 0; influence < 4 &&
                     firstInfluence + static_cast<int>(influence) < static_cast<int>(skinCount);
                     ++influence) {
                    vertex.boneWeights[firstInfluence + static_cast<int>(influence)] =
                        std::max(0.0f, value[influence]);
                }
            }
        }
    }
    if (!hasPositions) file.fail("FVTX has no _p0 position");
    const auto normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
    for (std::size_t j = vertexStart; j < meshData.vertices.size(); ++j) {
        auto& vertex = meshData.vertices[j];
        vertex.position = glm::vec3(transform * glm::vec4(vertex.position, 1.0f));
        const auto normal = normalMatrix * vertex.normal;
        vertex.normal = glm::dot(normal, normal) > 0.0f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
    }
}

glm::vec3 readVec3(const Binary& file, std::size_t offset) {
    const glm::vec3 result(file.f32(offset), file.f32(offset + 4), file.f32(offset + 8));
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z)) {
        file.fail("non-finite bone transform");
    }
    return result;
}

struct SbfresSkeleton {
    std::vector<std::string> names;
    std::vector<std::int32_t> parents;
    std::vector<std::uint16_t> matrixToBoneIndices;
    std::vector<glm::mat4> localTransforms;
    std::vector<glm::mat4> globalTransforms;
};

SbfresSkeleton readSkeleton(const Binary& file, std::size_t skeleton) {
    SbfresSkeleton result;
    if (skeleton == 0) return result;
    file.magic(skeleton, "FSKL");
    const auto count = file.u16(skeleton + 0x3c);
    const auto bones = file.table(skeleton + 0x18, count, 0x50);
    const auto matrixCount = static_cast<std::size_t>(file.u16(skeleton + 0x3e)) +
        file.u16(skeleton + 0x40);
    const auto matrixToBone = file.table(skeleton + 0x20, matrixCount, 2);
    result.matrixToBoneIndices.reserve(matrixCount);
    for (std::size_t i = 0; i < matrixCount; ++i) {
        const auto boneIndex = file.u16(matrixToBone + i * 2);
        if (boneIndex >= count) file.fail("FSKL matrix-to-bone index outside skeleton");
        result.matrixToBoneIndices.push_back(boneIndex);
    }
    result.names.reserve(count);
    result.parents.reserve(count);
    result.localTransforms.reserve(count);
    result.globalTransforms.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto bone = bones + i * 0x50;
        const auto parent = file.u16(bone + 0x1a);
        const auto scale = readVec3(file, bone + 0x28);
        const auto rotation = readVec3(file, bone + 0x34);
        const auto translation = readVec3(file, bone + 0x44);
        glm::quat orientation;
        if ((file.u32(bone + 0x24) & 0x7000) == 0x1000) {
            orientation = glm::quat(rotation);
        } else {
            orientation = glm::quat(file.f32(bone + 0x40), rotation);
        }
        const float length = glm::length(orientation);
        if (!std::isfinite(length) || length == 0.0f ||
            scale.x == 0.0f || scale.y == 0.0f || scale.z == 0.0f) {
            file.fail("invalid rigid bone transform");
        }
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), translation) *
            glm::mat4_cast(orientation / length) *
            glm::scale(glm::mat4(1.0f), scale);
        if (parent != 0xffff && parent >= i) file.fail("invalid or unordered bone parent");
        result.names.push_back(file.string(bone));
        result.parents.push_back(parent == 0xffff ? -1 : static_cast<std::int32_t>(parent));
        result.localTransforms.push_back(local);
        result.globalTransforms.push_back(
            parent == 0xffff ? local : result.globalTransforms[parent] * local
        );
    }
    return result;
}

void appendSkeletonNodes(
    const SbfresSkeleton& skeleton,
    const std::vector<std::uint32_t>& nodeIds,
    ModelNode& model
) {
    const auto buildNode = [&skeleton, &nodeIds](const auto& self, std::size_t index) -> ModelNode {
        ModelNode node;
        node.name = skeleton.names[index];
        node.nodeId = nodeIds[index];
        node.localTransform = skeleton.localTransforms[index];
        node.globalTransform = skeleton.globalTransforms[index];
        for (std::size_t childIndex = 0; childIndex < skeleton.parents.size(); ++childIndex) {
            if (skeleton.parents[childIndex] == static_cast<std::int32_t>(index)) {
                node.children.push_back(self(self, childIndex));
            }
        }
        return node;
    };
    for (std::size_t index = 0; index < skeleton.parents.size(); ++index) {
        if (skeleton.parents[index] < 0) {
            model.children.push_back(buildNode(buildNode, index));
        }
    }
}


void addBounds(ModelNode& node, const glm::vec3& p) {
    if (node.hasBounds) {
        node.boundsMinimum = glm::min(node.boundsMinimum, p);
        node.boundsMaximum = glm::max(node.boundsMaximum, p);
    } else {
        node.hasBounds = true;
        node.boundsMinimum = node.boundsMaximum = p;
    }
}

// Some FMDLs repeat an FVTX header for another shape but keep only one copy
// of the layout tables. Only reuse a layout for the exact same GPU stream.
std::size_t sharedVertexLayout(const Binary& file, std::size_t table, std::size_t count, std::size_t vertex) {
    if (file.pointer(vertex + 0x10) || file.pointer(vertex + 0x38) || file.pointer(vertex + 0x40)) {
        return vertex;
    }
    std::size_t layout = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto candidate = table + i * 0x60;
        if (file.u32(candidate + 0x50) != file.u32(vertex + 0x50) ||
            file.u16(candidate + 0x54) != file.u16(vertex + 0x54) ||
            file.u32(candidate + 0x58) != file.u32(vertex + 0x58) ||
            !file.hasMagic(candidate, "FVTX") || !file.pointer(candidate + 0x10) ||
            !file.pointer(candidate + 0x38) || !file.pointer(candidate + 0x40)) continue;
        if (layout != 0) file.fail("ambiguous shared FVTX layout");
        layout = candidate;
    }
    if (layout == 0) file.fail("missing FVTX layout");
    return layout;
}

void appendShape(
    const Binary& file,
    const Binary& gpu,
    std::size_t shape,
    std::size_t vertexTable,
    std::size_t vertexBufferCount,
    std::size_t materialStart,
    std::size_t materialCount,
    const SbfresSkeleton& skeleton,
    std::size_t skeletonBoneOffset,
    ModelNode& node,
    MeshData& result
) {
    file.magic(shape, "FSHP");
    node.name = file.string(shape + 0x10);
    const auto lodCount = file.u8(shape + 0x67);
    const auto skinCount = file.u8(shape + 0x66);
    const auto skinBoneCount = file.u16(shape + 0x64);
    if (skinCount > 4) file.fail("BFRES shape has more than four skin influences");
    if (skinCount > 0 && skinBoneCount == 0) file.fail("skinned BFRES shape has no skin bone table");
    const auto skinBoneTable = file.table(shape + 0x28, skinBoneCount, 2);
    std::vector<std::uint16_t> skinBoneIndices;
    skinBoneIndices.reserve(skinBoneCount);
    for (std::size_t i = 0; i < skinBoneCount; ++i) {
        const auto boneIndex = file.u16(skinBoneTable + i * 2);
        if (boneIndex >= skeleton.globalTransforms.size()) {
            file.fail("FSHP skin bone index outside FSKL");
        }
        skinBoneIndices.push_back(boneIndex);
    }
    const auto lod = file.table(shape + 0x20, lodCount, 0x38);
    if (lodCount == 0) return;
    const auto indexCount = file.u32(lod + 0x2c);
    if (indexCount == 0) return;
    if (file.u32(lod + 0x24) != 3 || indexCount % 3 != 0) file.fail("expected triangle-list LOD0");
    const auto materialIndex = file.u16(shape + 0x5e);
    const auto boneIndex = file.u16(shape + 0x60);
    const auto vertexIndex = file.u16(shape + 0x62);
    if (materialIndex >= materialCount || vertexIndex >= vertexBufferCount ||
        (!skeleton.globalTransforms.empty() && boneIndex >= skeleton.globalTransforms.size())) {
        file.fail("invalid FSHP reference");
    }
    const auto vertex = vertexTable + vertexIndex * 0x60;
    file.magic(vertex, "FVTX");
    const auto vertexCount = file.u32(vertex + 0x58);
    if (vertexCount == 0 || vertexCount > kVertexLimit - result.vertices.size() ||
        indexCount > kIndexLimit - result.indices.size()) file.fail("geometry budget exceeded");
    const auto indexFormat = file.u32(lod + 0x28);
    if (indexFormat > 2) file.fail("unsupported index format");
    const std::size_t indexSize = std::size_t(1) << indexFormat;
    const auto bufferSize = file.u32(file.table(lod + 0x18, 1, 4));
    if (indexCount > bufferSize / indexSize) file.fail("LOD index count exceeds buffer size");
    const Binary indices{gpu.slice(file.u32(lod + 0x20), bufferSize), "FSHP indices"};
    const auto baseVertex = file.u32(lod + 0x30);
    const auto start = static_cast<std::uint32_t>(result.vertices.size());
    const glm::mat4 geometryTransform = skinCount == 0
        ? (skeleton.globalTransforms.empty() ? glm::mat4(1.0f) : skeleton.globalTransforms[boneIndex])
        : glm::mat4(1.0f);
    node.localTransform = node.globalTransform = geometryTransform;
    appendVertices(
        file,
        gpu,
        sharedVertexLayout(file, vertexTable, vertexBufferCount, vertex),
        vertexCount,
        geometryTransform,
        skinCount,
        skeleton.globalTransforms.size(),
        skeleton.matrixToBoneIndices,
        skeletonBoneOffset,
        result
    );

    if (skinCount > 0) {
        ++result.skinnedMeshCount;
        for (std::size_t vertexIndex = start; vertexIndex < result.vertices.size(); ++vertexIndex) {
            const Vertex& skinnedVertex = result.vertices[vertexIndex];
            for (std::size_t influence = 0; influence < 4; ++influence) {
                if (skinnedVertex.boneWeights[influence] <= 0.0f) continue;
                ++result.skinWeightCount;
                const auto globalBoneIndex = skinnedVertex.boneIndices[influence];
                if (globalBoneIndex >= 0 &&
                    static_cast<std::size_t>(globalBoneIndex) < result.skeletonBones.size()) {
                    ++result.skeletonBones[static_cast<std::size_t>(globalBoneIndex)].influenceCount;
                }
            }
        }
    }

    const auto firstIndex = static_cast<std::uint32_t>(result.indices.size());
    for (std::size_t i = 0; i < indexCount; ++i) {
        const auto offset = i * indexSize;
        const auto index = indexSize == 1 ? indices.u8(offset) :
            indexSize == 2 ? indices.u16(offset) : indices.u32(offset);
        if (baseVertex >= vertexCount || index >= vertexCount - baseVertex) indices.fail("index outside FVTX");
        const auto absolute = start + baseVertex + index;
        result.indices.push_back(absolute);
        addBounds(node, result.vertices[absolute].position);
    }
    node.meshCount = 1;
    node.meshPartIndices.push_back(static_cast<std::uint32_t>(result.parts.size()));
    result.parts.push_back({firstIndex, indexCount, static_cast<std::uint32_t>(materialStart + materialIndex), node.nodeId});
}

struct BfresCurve {
    std::uint16_t flags = 0;
    std::uint32_t target = 0;
    float startFrame = 0.0f;
    float endFrame = 0.0f;
    float scale = 1.0f;
    float offset = 0.0f;
    std::vector<float> frames;
    std::vector<std::array<float, 4>> keys;

    unsigned curveType() const { return (flags >> 4) & 7; }

    float sample(float frame) const {
        if (frames.empty()) return offset;
        if (frames.size() == 1 || frame <= frames.front()) {
            return keys.front()[0] * scale + offset;
        }
        if (frame >= frames.back()) {
            return keys.back()[0] * scale + offset;
        }
        const auto upper = std::upper_bound(frames.begin(), frames.end(), frame);
        const std::size_t rightIndex = static_cast<std::size_t>(upper - frames.begin());
        const std::size_t leftIndex = rightIndex - 1;
        const float span = frames[rightIndex] - frames[leftIndex];
        const float factor = span <= std::numeric_limits<float>::epsilon()
            ? 0.0f
            : (frame - frames[leftIndex]) / span;
        const auto& left = keys[leftIndex];
        if (curveType() == 0) {
            const float a = left[0] * scale + offset;
            const float b = left[1] * scale;
            const float c = left[2] * scale;
            const float d = left[3] * scale;
            return ((d * factor + c) * factor + b) * factor + a;
        }
        if (curveType() == 1) {
            return left[0] * scale + offset + left[1] * scale * factor;
        }
        if (curveType() == 2 || curveType() == 5 || curveType() == 7) {
            const auto index = std::min<std::size_t>(
                keys.size() - 1,
                static_cast<std::size_t>(std::floor(frame - startFrame))
            );
            return keys[index][0] * scale + offset;
        }
        return left[0] * scale + offset;
    }
};

float readCurveKey(const Binary& file, std::size_t offset, unsigned keyType, unsigned curveType) {
    if (curveType == 4 || curveType == 6) {
        return static_cast<float>(file.u32(offset));
    }
    switch (keyType) {
    case 0: return file.f32(offset);
    case 1: return static_cast<float>(static_cast<std::int16_t>(file.u16(offset)));
    case 2: return static_cast<float>(static_cast<std::int8_t>(file.u8(offset)));
    default: file.fail("unsupported FSKA curve key type");
    }
}

BfresCurve readCurve(const Binary& file, std::size_t offset) {
    BfresCurve result;
    result.flags = file.u16(offset + 0x10);
    const auto frameCount = file.u16(offset + 0x12);
    result.target = file.u32(offset + 0x14);
    result.startFrame = file.f32(offset + 0x18);
    result.endFrame = file.f32(offset + 0x1c);
    result.scale = file.f32(offset + 0x20);
    result.offset = file.f32(offset + 0x24);
    const auto frameType = result.flags & 3;
    const auto keyType = (result.flags >> 2) & 3;
    const auto type = result.curveType();
    const auto frameData = file.pointer(offset);
    const auto keyData = file.pointer(offset + 8);
    if (frameCount == 0 || frameData == 0 || keyData == 0 ||
        !std::isfinite(result.startFrame) || !std::isfinite(result.endFrame) ||
        !std::isfinite(result.scale) || !std::isfinite(result.offset)) {
        file.fail("invalid FSKA curve");
    }
    result.frames.reserve(frameCount);
    for (std::size_t i = 0; i < frameCount; ++i) {
        float frame = 0.0f;
        switch (frameType) {
        case 0: frame = file.f32(frameData + i * 4); break;
        case 1: frame = static_cast<float>(static_cast<std::int16_t>(file.u16(frameData + i * 2))) / 32.0f; break;
        case 2: frame = static_cast<float>(file.u8(frameData + i)); break;
        default: file.fail("unsupported FSKA curve frame type");
        }
        if (!std::isfinite(frame)) file.fail("non-finite FSKA curve frame");
        result.frames.push_back(frame);
    }
    const std::size_t elements = type == 0 ? 4 : type == 1 ? 2 : 1;
    const std::size_t elementBytes = keyType == 0 ? 4 : keyType == 1 ? 2 : 1;
    result.keys.resize(frameCount);
    for (std::size_t i = 0; i < frameCount; ++i) {
        for (std::size_t element = 0; element < elements; ++element) {
            result.keys[i][element] = readCurveKey(
                file,
                keyData + (i * elements + element) * elementBytes,
                keyType,
                type
            );
            if (!std::isfinite(result.keys[i][element])) file.fail("non-finite FSKA curve key");
        }
    }
    return result;
}

std::vector<SkeletalAnimationClip> readSkeletalAnimations(const Binary& file) {
    const auto animationCount = file.u16(0xbe);
    if (animationCount == 0) return {};
    const auto animations = file.table(0x38, animationCount, 0x60);
    std::vector<SkeletalAnimationClip> result;
    result.reserve(animationCount);
    for (std::size_t animationIndex = 0; animationIndex < animationCount; ++animationIndex) {
        const auto animation = animations + animationIndex * 0x60;
        SkeletalAnimationClip clip;
        clip.name = file.string(animation + 0x10);
        clip.durationTicks = static_cast<double>(file.u32(animation + 0x4c));
        clip.ticksPerSecond = 30.0;
        const auto boneCount = file.u16(animation + 0x58);
        const auto bones = file.table(animation + 0x30, boneCount, 0x28);
        const bool looping = (file.u32(animation + 0x48) & 4) != 0;
        clip.channels.reserve(boneCount);
        for (std::size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
            const auto bone = bones + boneIndex * 0x28;
            const auto curveCount = file.u8(bone + 0x1e);
            if (curveCount == 0) continue;
            const auto boneFlags = file.u32(bone + 0x18);
            const auto baseData = file.pointer(bone + 0x10);
            glm::vec3 baseScale(1.0f);
            glm::vec3 baseTranslation(0.0f);
            glm::vec4 baseRotation(0.0f, 0.0f, 0.0f, 1.0f);
            if (baseData != 0) {
                std::size_t cursor = baseData;
                if (boneFlags & (1u << 3)) {
                    baseScale = readVec3(file, cursor);
                    cursor += 12;
                }
                if (boneFlags & (1u << 4)) {
                    baseRotation = glm::vec4(
                        file.f32(cursor), file.f32(cursor + 4),
                        file.f32(cursor + 8), file.f32(cursor + 12)
                    );
                    cursor += 16;
                }
                if (boneFlags & (1u << 5)) baseTranslation = readVec3(file, cursor);
            }
            const auto curveTable = file.table(bone + 8, curveCount, 0x30);
            std::vector<BfresCurve> curves;
            curves.reserve(curveCount);
            for (std::size_t curveIndex = 0; curveIndex < curveCount; ++curveIndex) {
                curves.push_back(readCurve(file, curveTable + curveIndex * 0x30));
            }
            const auto findCurve = [&curves](std::uint32_t target) -> const BfresCurve* {
                for (const BfresCurve& curve : curves) {
                    if (curve.target == target) return &curve;
                }
                return nullptr;
            };
            const bool hasPosition = findCurve(0x10) != nullptr ||
                findCurve(0x14) != nullptr || findCurve(0x18) != nullptr;
            const bool hasScale = findCurve(0x04) != nullptr ||
                findCurve(0x08) != nullptr || findCurve(0x0c) != nullptr;
            const bool hasRotation = findCurve(0x20) != nullptr ||
                findCurve(0x24) != nullptr || findCurve(0x28) != nullptr ||
                findCurve(0x2c) != nullptr;
            if (!hasPosition && !hasScale && !hasRotation) continue;

            SkeletalAnimationChannel channel;
            channel.nodeName = file.string(bone);
            channel.preBehavior = looping ? AnimationBehavior::Repeat : AnimationBehavior::Default;
            channel.postBehavior = looping ? AnimationBehavior::Repeat : AnimationBehavior::Default;
            const auto sample = [&findCurve](std::uint32_t target, float frame, float fallback) {
                const BfresCurve* curve = findCurve(target);
                return curve == nullptr ? fallback : curve->sample(frame);
            };
            const auto frameCount = static_cast<std::size_t>(clip.durationTicks);
            for (std::size_t frame = 0; frame <= frameCount; ++frame) {
                const float time = static_cast<float>(frame);
                if (hasPosition) {
                    channel.positionKeys.push_back({
                        static_cast<double>(frame),
                        glm::vec3(
                            sample(0x10, time, baseTranslation.x),
                            sample(0x14, time, baseTranslation.y),
                            sample(0x18, time, baseTranslation.z)
                        )
                    });
                }
                if (hasScale) {
                    channel.scalingKeys.push_back({
                        static_cast<double>(frame),
                        glm::vec3(
                            sample(0x04, time, baseScale.x),
                            sample(0x08, time, baseScale.y),
                            sample(0x0c, time, baseScale.z)
                        )
                    });
                }
                if (hasRotation) {
                    if ((file.u32(animation + 0x48) & 0x7000) == 0x1000) {
                        const glm::vec3 euler(
                            sample(0x20, time, baseRotation.x),
                            sample(0x24, time, baseRotation.y),
                            sample(0x28, time, baseRotation.z)
                        );
                        const glm::quat rotation = glm::normalize(glm::quat(euler));
                        channel.rotationKeys.push_back({
                            static_cast<double>(frame),
                            glm::vec4(rotation.x, rotation.y, rotation.z, rotation.w)
                        });
                    } else {
                        const glm::vec4 rotation(
                            sample(0x20, time, baseRotation.x),
                            sample(0x24, time, baseRotation.y),
                            sample(0x28, time, baseRotation.z),
                            sample(0x2c, time, baseRotation.w)
                        );
                        const glm::quat normalized = glm::normalize(glm::quat(
                            rotation.w, rotation.x, rotation.y, rotation.z
                        ));
                        channel.rotationKeys.push_back({
                            static_cast<double>(frame),
                            glm::vec4(normalized.x, normalized.y, normalized.z, normalized.w)
                        });
                    }
                }
            }
            clip.channels.push_back(std::move(channel));
        }
        result.push_back(std::move(clip));
    }
    return result;
}

struct ImportedModelRange {
    std::uint32_t rootNodeId = 0;
    std::size_t vertexStart = 0;
    std::size_t vertexCount = 0;
    std::size_t boneStart = 0;
    std::size_t boneCount = 0;
    std::vector<std::string> boneNames;
};

MeshData parseModel(
    const Binary& file,
    TexturePacks& textures,
    std::vector<SkeletalAnimationClip> animations
) {
    const auto modelCount = file.u16(0xbc);
    if (modelCount == 0 || modelCount > 1024) file.fail("invalid FMDL count");
    const auto models = file.table(0x28, modelCount, 0x78);
    const auto bufferInfo = file.table(0x90, 1, 0x20);
    const auto gpuStart = file.pointer(bufferInfo + 8);
    if (gpuStart == 0) file.fail("missing GPU section");
    const Binary gpu{file.slice(gpuStart, file.u32(bufferInfo + 4)), "BFRES GPU section"};
    MeshData result;
    result.hierarchy.name = file.string(0x20);
    std::vector<ImportedModelRange> ranges;
    ranges.reserve(modelCount);
    std::uint32_t nextNode = 1;
    for (std::size_t i = 0; i < modelCount; ++i) {
        const auto model = models + i * 0x78;
        file.magic(model, "FMDL");
        const auto vertexCount = file.u16(model + 0x68);
        const auto shapeCount = file.u16(model + 0x6a);
        const auto materialCount = file.u16(model + 0x6c);
        if (result.materials.size() + materialCount > 4096 || nextNode + shapeCount >= 1'000'000) {
            file.fail("model hierarchy or material budget exceeded");
        }
        const auto vertices = file.table(model + 0x28, vertexCount, 0x60);
        const auto shapes = file.table(model + 0x30, shapeCount, 0x70);
        const auto materials = file.table(model + 0x40, materialCount, 0xb8);
        const SbfresSkeleton skeleton = readSkeleton(file, file.pointer(model + 0x20));
        const auto materialStart = result.materials.size();
        for (std::size_t j = 0; j < materialCount; ++j) {
            result.materials.push_back(readMaterial(file, materials + j * 0xb8, textures));
        }
        ModelNode node;
        node.name = file.string(model + 0x10);
        node.nodeId = nextNode++;
        const auto vertexStart = result.vertices.size();
        const auto boneStart = result.skeletonBones.size();
        std::vector<std::uint32_t> boneNodeIds;
        boneNodeIds.reserve(skeleton.names.size());
        for (std::size_t boneIndex = 0; boneIndex < skeleton.names.size(); ++boneIndex) {
            const auto nodeId = nextNode++;
            boneNodeIds.push_back(nodeId);
            SkeletonBone bone;
            bone.name = skeleton.names[boneIndex];
            bone.nodeId = nodeId;
            bone.parentIndex = skeleton.parents[boneIndex] < 0
                ? -1
                : static_cast<std::int32_t>(boneStart + skeleton.parents[boneIndex]);
            bone.bindPoseGlobalTransform = skeleton.globalTransforms[boneIndex];
            bone.offsetMatrix = glm::inverse(bone.bindPoseGlobalTransform);
            result.skeletonBones.push_back(std::move(bone));
        }
        appendSkeletonNodes(skeleton, boneNodeIds, node);
        for (std::size_t j = 0; j < shapeCount; ++j) {
            ModelNode shape;
            shape.nodeId = nextNode++;
            appendShape(
                file,
                gpu,
                shapes + j * 0x70,
                vertices,
                vertexCount,
                materialStart,
                materialCount,
                skeleton,
                boneStart,
                shape,
                result
            );
            if (shape.hasBounds) {
                addBounds(node, shape.boundsMinimum);
                addBounds(node, shape.boundsMaximum);
            }
            node.children.push_back(std::move(shape));
        }
        if (node.hasBounds) {
            addBounds(result.hierarchy, node.boundsMinimum);
            addBounds(result.hierarchy, node.boundsMaximum);
        }
        ranges.push_back({
            node.nodeId,
            vertexStart,
            result.vertices.size() - vertexStart,
            boneStart,
            skeleton.names.size(),
            skeleton.names
        });
        result.hierarchy.children.push_back(std::move(node));
    }
    if (result.indices.empty()) file.fail("no renderable triangles");

    if (!animations.empty()) {
        std::size_t targetRange = 0;
        std::size_t bestMatchCount = 0;
        for (std::size_t rangeIndex = 0; rangeIndex < ranges.size(); ++rangeIndex) {
            std::size_t matchCount = 0;
            for (const SkeletalAnimationClip& animation : animations) {
                for (const SkeletalAnimationChannel& channel : animation.channels) {
                    if (std::find(
                            ranges[rangeIndex].boneNames.begin(),
                            ranges[rangeIndex].boneNames.end(),
                            channel.nodeName
                        ) != ranges[rangeIndex].boneNames.end()) {
                        ++matchCount;
                    }
                }
            }
            if (matchCount > bestMatchCount) {
                bestMatchCount = matchCount;
                targetRange = rangeIndex;
            }
        }
        if (bestMatchCount == 0) file.fail("FSKA bones do not match any model skeleton");
        const ImportedModelRange& range = ranges[targetRange];
        for (SkeletalAnimationClip& animation : animations) {
            for (SkeletalAnimationChannel& channel : animation.channels) {
                const auto boneIt = std::find(
                    range.boneNames.begin(), range.boneNames.end(), channel.nodeName
                );
                if (boneIt == range.boneNames.end()) continue;
                const auto localBoneIndex = static_cast<std::size_t>(boneIt - range.boneNames.begin());
                channel.boneIndex = static_cast<std::int32_t>(range.boneStart + localBoneIndex);
                channel.nodeId = result.skeletonBones[range.boneStart + localBoneIndex].nodeId;
            }
        }
        result.skeletalAnimations = std::move(animations);
        SkeletalAnimationInstance instance;
        instance.rootNodeId = range.rootNodeId;
        instance.vertexStart = static_cast<std::uint32_t>(range.vertexStart);
        instance.vertexCount = static_cast<std::uint32_t>(range.vertexCount);
        instance.boneStart = static_cast<std::uint32_t>(range.boneStart);
        instance.boneCount = static_cast<std::uint32_t>(range.boneCount);
        instance.animationStart = 0;
        instance.animationCount = static_cast<std::uint32_t>(result.skeletalAnimations.size());
        instance.globalInverseTransform = glm::mat4(1.0f);
        instance.enabled = true;
        instance.playing = true;
        instance.looping = true;
        result.skeletalAnimationInstances.push_back(instance);
    }
    result.bindPoseVertices = result.vertices;
    result.skinSourceVertices = result.vertices;
    return result;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

} // namespace

struct SbfresTextureArchive::Data {
    explicit Data(const std::filesystem::path& path) : bytes(readArchive(path)), file(fres(bytes)),
        sources(readTextureSources(file)) {}

    void readCatalog() {
        entries.reserve(sources.size());
        for (const auto& [pack, header] : sources) {
            SbfresTextureInfo info;
            info.name = pack.string(header + 0x60);
            info.width = pack.u32(header + 0x24);
            info.height = pack.u32(header + 0x28);
            info.layers = pack.u32(header + 0x30);
            info.mipLevels = pack.u16(header + 0x16);
            info.format = pack.u32(header + 0x1c);
            for (std::size_t i = 0; i < info.components.size(); ++i) {
                info.components[i] = pack.u8(header + 0x58 + i);
            }
            entries.push_back(std::move(info));
        }
    }

    std::vector<std::uint8_t> bytes;
    Binary file;
    std::vector<TextureSource> sources;
    std::vector<SbfresTextureInfo> entries;
};

SbfresTextureArchive::SbfresTextureArchive(const std::filesystem::path& path) {
    try {
        auto archive = std::make_shared<Data>(path);
        if (archive->sources.empty()) throw std::runtime_error("missing BNTX textures");
        archive->readCatalog();
        data_ = std::move(archive);
    } catch (const std::exception& error) {
        throw std::runtime_error("failed to load '" + path.string() + "': " + error.what());
    }
}

std::span<const SbfresTextureInfo> SbfresTextureArchive::entries() const {
    if (!data_) return {};
    return data_->entries;
}

std::vector<std::uint32_t> SbfresTextureArchive::readMaterialLayerIndices(std::size_t index) const {
    if (index >= entries().size()) throw std::runtime_error("BNTX texture index out of range");
    const auto& [pack, header] = data_->sources[index];

    // The BRTI user data entries follow the key order in the user data dictionary.
    const auto keys = pack.dictionary(header + 0x98);
    constexpr std::string_view key = "array_index";
    const auto found = std::find(keys.begin(), keys.end(), key);
    if (found == keys.end()) return {};

    constexpr std::size_t entrySize = 0x40;
    const auto userData = pack.table(header + 0x78, keys.size(), entrySize);
    const auto entry = userData + static_cast<std::size_t>(found - keys.begin()) * entrySize;
    const auto count = pack.u32(entry + 0x10);
    if (pack.string(entry) != key || pack.u8(entry + 0x14) != 0 || count == 0 || count > 256) {
        pack.fail("invalid material array_index user data");
    }

    const auto values = pack.table(entry + 0x08, count, sizeof(std::uint32_t));
    const auto layerCount = entries()[index].layers;
    std::vector<std::uint32_t> layers;
    layers.reserve(count);
    for (std::size_t id = 0; id < count; ++id) {
        const auto layer = pack.u32(values + id * sizeof(std::uint32_t));
        if (layer >= layerCount) {
            pack.fail("array_index layer out of range for material ID " + std::to_string(id));
        }
        layers.push_back(layer);
    }
    return layers;
}

EmbeddedTextureData SbfresTextureArchive::decodeLayer(std::size_t index, std::uint32_t layer) const {
    if (index >= entries().size()) throw std::runtime_error("BNTX texture index out of range");
    const auto& [pack, header] = data_->sources[index];
    return decodeTexture(pack, header, kTextureLimit, false, layer);
}

bool isBfresPath(const std::filesystem::path& path) {
    const auto extension = lower(path.extension().string());
    return extension == ".sbfres" || extension == ".bfres";
}

bool isBfresTextureSidecarPath(const std::filesystem::path& path) {
    return isBfresPath(path) && lower(path.stem().extension().string()) == ".tex";
}

bool isBfresAnimationPath(const std::filesystem::path& path) {
    if (!isBfresPath(path)) return false;
    std::string name = lower(path.stem().string());
    return name.find("animation") != std::string::npos || name.find("anim") != std::string::npos;
}

BfresContent inspectBfresContent(const std::filesystem::path& path) {
    const auto bytes = readArchive(path);
    const auto file = fres(bytes);
    const bool hasAnimation = file.u16(0xbe) != 0;
    // Animation packages may carry a skeleton-only FMDL, without a GPU section.
    if (file.u16(0xbc) != 0 && (!hasAnimation || file.pointer(0x90) != 0)) {
        return BfresContent::Model;
    }
    if (hasAnimation) return BfresContent::SkeletalAnimation;
    if (!readTextureSources(file).empty()) return BfresContent::Texture;
    return BfresContent::Other;
}

std::filesystem::path findBfresTextureSidecar(const std::filesystem::path& modelPath) {
    auto name = modelPath.stem();
    name += ".Tex";
    name += modelPath.extension();
    auto sidecar = modelPath.parent_path() / name;
    return std::filesystem::exists(sidecar) ? sidecar : std::filesystem::path{};
}

MeshData loadSbfresMesh(
    const std::filesystem::path& modelPath,
    const std::filesystem::path& texturePath,
    const std::filesystem::path& animationPath,
    bool includeEmbeddedAnimations,
    SbfresTextureArchive* previewTextures
) {
    const auto sidecarPath = texturePath.empty() ? findBfresTextureSidecar(modelPath) : texturePath;
    try {
        auto model = std::make_shared<SbfresTextureArchive::Data>(modelPath);
        std::shared_ptr<SbfresTextureArchive::Data> sidecar;
        TexturePacks textures(previewTextures != nullptr);
        textures.add(model->sources);
        if (!sidecarPath.empty()) {
            sidecar = std::make_shared<SbfresTextureArchive::Data>(sidecarPath);
            if (sidecar->sources.empty()) throw std::runtime_error("missing BNTX texture pack");
            textures.add(sidecar->sources);
        }
        std::vector<std::uint8_t> animationBytes;
        std::vector<SkeletalAnimationClip> animations;
        if (!animationPath.empty()) {
            animationBytes = readArchive(animationPath);
            animations = readSkeletalAnimations(fres(animationBytes));
            if (animations.empty()) throw std::runtime_error("animation BFRES contains no FSKA clips");
        } else if (includeEmbeddedAnimations) {
            animations = readSkeletalAnimations(model->file);
        }
        auto result = parseModel(model->file, textures, std::move(animations));
        if (result.hierarchy.name.empty()) result.hierarchy.name = modelPath.stem().string();
        if (previewTextures) {
            auto archive = sidecar ? std::move(sidecar) : std::move(model);
            archive->readCatalog();
            previewTextures->data_ = archive->entries.empty() ? nullptr : std::move(archive);
        }
        return result;
    } catch (const std::exception& error) {
        auto source = modelPath.string();
        if (!sidecarPath.empty()) source += " + " + sidecarPath.string();
        if (!animationPath.empty()) source += " + " + animationPath.string();
        throw std::runtime_error("failed to load '" + source + "': " + error.what());
    }
}

} // namespace VulkCore
