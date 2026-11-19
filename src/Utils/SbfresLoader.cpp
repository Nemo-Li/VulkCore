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

EmbeddedTextureData decodeTexture(const Binary& bntx, std::size_t header, std::size_t budget,
                                  bool normalMap) {
    const auto width = bntx.u32(header + 0x24);
    const auto height = bntx.u32(header + 0x28);
    const auto format = bntx.u32(header + 0x1c);
    const auto tile = bntx.u8(header + 0x10);
    const auto blockHeightLog = bntx.u32(header + 0x34) & 7;
    const auto mips = bntx.u16(header + 0x16);
    if (width == 0 || height == 0 || width > 8192 || height > 8192 ||
        bntx.u32(header + 0x2c) != 1 || bntx.u32(header + 0x30) != 1 ||
        bntx.u32(header + 0x5c) != 1 || tile > 1 || blockHeightLog > 5 || mips == 0) {
        bntx.fail("expected a supported 2D texture layout");
    }
    std::size_t bytesPerBlock = 0;
    const auto codec = format >> 8;
    switch (format) {
    case 0x0201: bytesPerBlock = 1; break; // R8
    case 0x0b01: case 0x0b06: bytesPerBlock = 4; break; // RGBA8
    case 0x1a01: case 0x1a06: case 0x1d01: bytesPerBlock = 8; break; // BC1, BC4
    case 0x1b01: case 0x1b06: case 0x1c01: case 0x1c06: case 0x1e01:
        bytesPerBlock = 16; break; // BC2, BC3, BC5
    default: bntx.fail("unsupported texture format " + std::to_string(format));
    }
    const auto mipTable = bntx.table(header + 0x70, mips, 8);
    const auto mip0 = bntx.pointer(mipTable);
    const auto surface = bntx.slice(mip0, bntx.u32(header + 0x50));
    const auto mip1 = mips > 1 ? bntx.pointer(mipTable + 8) : mip0 + surface.size();
    if (!mip0 || mip1 <= mip0 || mip1 - mip0 > surface.size()) bntx.fail("invalid mip offsets");
    const Binary source{surface.first(mip1 - mip0), "BNTX mip 0"};

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

class TexturePacks {
public:
    bool add(const Binary& file) {
        bool hasPack = false;
        const auto count = file.u16(0xc8);
        const auto external = file.table(0x98, count, 16);
        for (std::size_t i = 0; i < count; ++i) {
            const auto entry = external + 16 * i;
            Binary pack{file.slice(file.pointer(entry), file.u32(entry + 8)), "BNTX"};
            if (!pack.hasMagic(0, "BNTX")) continue;
            hasPack = true;
            pack.magic(0x20, "NX  ");
            if (pack.u16(0x0c) != 0xfeff) pack.fail("unsupported byte order");
            pack.bytes = pack.slice(0, pack.u32(0x1c));
            const auto textures = pack.u32(0x24);
            const auto table = pack.table(0x28, textures, 8);
            for (std::size_t j = 0; j < textures; ++j) {
                const auto header = pack.pointer(table + 8 * j);
                pack.magic(header, "BRTI");
                const auto name = pack.string(header + 0x60);
                // A texture sidecar added after the model can replace an embedded entry.
                sources_.insert_or_assign(name, TextureSource{pack, header});
            }
        }
        return hasPack;
    }

    EmbeddedTextureData resolve(const std::string& name, bool normalMap) {
        const auto source = sources_.find(name);
        if (source == sources_.end()) return {};
        const auto key = (normalMap ? "normal:" : "color:") + name;
        auto cached = decoded_.find(key);
        if (cached == decoded_.end()) {
            const auto& [pack, header] = source->second;
            auto texture = decodeTexture(pack, header, remaining_, normalMap);
            remaining_ -= texture.data.size();
            cached = decoded_.emplace(key, std::move(texture)).first;
        }
        if (cached->second.data.size() > remaining_) source->second.pack.fail("texture budget exceeded");
        remaining_ -= cached->second.data.size();
        return cached->second;
    }

private:
    struct TextureSource { Binary pack; std::size_t header; };
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
    enum class Type { Float, Half, Unorm, Snorm, PackedNormal };
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
    case 0x010b: return {Type::Unorm, 4, 1};
    case 0x020b: return {Type::Snorm, 4, 1};
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
        case Type::PackedNormal: break;
        }
        if (!std::isfinite(result[c])) buffer.fail("non-finite vertex attribute");
    }
    return result;
}

struct VertexStream { Binary buffer; std::size_t stride; };

void appendVertices(const Binary& file, const Binary& gpu, std::size_t offset,
                    std::size_t count, const glm::mat4& transform, MeshData& meshData) {
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
        if (name != "_p0" && name != "_n0" && name != "_u0" && name != "_c0") continue;
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

std::vector<glm::mat4> boneTransforms(const Binary& file, std::size_t skeleton) {
    if (skeleton == 0) return {};
    file.magic(skeleton, "FSKL");
    const auto count = file.u16(skeleton + 0x3c);
    const auto bones = file.table(skeleton + 0x18, count, 0x50);
    std::vector<glm::mat4> transforms;
    transforms.reserve(count);
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
        if (!std::isfinite(length) || length == 0.0f || scale.x == 0 || scale.y == 0 || scale.z == 0) {
            file.fail("invalid rigid bone transform");
        }
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), translation) *
            glm::mat4_cast(orientation / length) * glm::scale(glm::mat4(1.0f), scale);
        if (parent != 0xffff && parent >= i) file.fail("invalid or unordered bone parent");
        transforms.push_back(parent == 0xffff ? local : transforms[parent] * local);
    }
    return transforms;
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

void appendShape(const Binary& file, const Binary& gpu, std::size_t shape, std::size_t vertexTable,
                 std::size_t vertexBufferCount, std::size_t materialStart, std::size_t materialCount,
                 const std::vector<glm::mat4>& bones, ModelNode& node, MeshData& result) {
    file.magic(shape, "FSHP");
    node.name = file.string(shape + 0x10);
    if (file.u8(shape + 0x66) != 0) file.fail("weighted skinning is unsupported");
    const auto lodCount = file.u8(shape + 0x67);
    if (lodCount == 0) return;
    const auto lod = file.table(shape + 0x20, lodCount, 0x38);
    const auto indexCount = file.u32(lod + 0x2c);
    if (indexCount == 0) return;
    if (file.u32(lod + 0x24) != 3 || indexCount % 3 != 0) file.fail("expected triangle-list LOD0");
    const auto materialIndex = file.u16(shape + 0x5e);
    const auto boneIndex = file.u16(shape + 0x60);
    const auto vertexIndex = file.u16(shape + 0x62);
    if (materialIndex >= materialCount || vertexIndex >= vertexBufferCount ||
        (!bones.empty() && boneIndex >= bones.size())) file.fail("invalid FSHP reference");
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
    node.localTransform = node.globalTransform = bones.empty() ? glm::mat4(1.0f) : bones[boneIndex];
    appendVertices(file, gpu, vertex, vertexCount, node.globalTransform, result);

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

MeshData parseModel(const Binary& file, TexturePacks& textures) {
    const auto modelCount = file.u16(0xbc);
    if (modelCount == 0 || modelCount > 1024) file.fail("invalid FMDL count");
    const auto models = file.table(0x28, modelCount, 0x78);
    const auto bufferInfo = file.table(0x90, 1, 0x20);
    const auto gpuStart = file.pointer(bufferInfo + 8);
    if (gpuStart == 0) file.fail("missing GPU section");
    const Binary gpu{file.slice(gpuStart, file.u32(bufferInfo + 4)), "BFRES GPU section"};
    MeshData result;
    result.hierarchy.name = file.string(0x20);
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
        const auto bones = boneTransforms(file, file.pointer(model + 0x20));
        const auto materialStart = result.materials.size();
        for (std::size_t j = 0; j < materialCount; ++j) {
            result.materials.push_back(readMaterial(file, materials + j * 0xb8, textures));
        }
        ModelNode node;
        node.name = file.string(model + 0x10);
        node.nodeId = nextNode++;
        for (std::size_t j = 0; j < shapeCount; ++j) {
            ModelNode shape;
            shape.nodeId = nextNode++;
            appendShape(file, gpu, shapes + j * 0x70, vertices, vertexCount, materialStart, materialCount,
                        bones, shape, result);
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
        result.hierarchy.children.push_back(std::move(node));
    }
    if (result.indices.empty()) file.fail("no renderable triangles");
    return result;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

} // namespace

bool isBfresPath(const std::filesystem::path& path) {
    const auto extension = lower(path.extension().string());
    return extension == ".sbfres" || extension == ".bfres";
}

bool isBfresTextureSidecarPath(const std::filesystem::path& path) {
    return isBfresPath(path) && lower(path.stem().extension().string()) == ".tex";
}

MeshData loadSbfresMesh(const std::filesystem::path& modelPath, const std::filesystem::path& texturePath) {
    auto sidecarPath = texturePath;
    if (sidecarPath.empty()) {
        sidecarPath = modelPath.parent_path() /
            (modelPath.stem().string() + ".Tex" + modelPath.extension().string());
        if (!std::filesystem::exists(sidecarPath)) sidecarPath.clear();
    }
    try {
        const auto modelBytes = readArchive(modelPath);
        const auto model = fres(modelBytes);
        std::vector<std::uint8_t> sidecarBytes;
        TexturePacks textures;
        textures.add(model);
        if (!sidecarPath.empty()) {
            sidecarBytes = readArchive(sidecarPath);
            if (!textures.add(fres(sidecarBytes))) throw std::runtime_error("missing BNTX texture pack");
        }
        auto result = parseModel(model, textures);
        if (result.hierarchy.name.empty()) result.hierarchy.name = modelPath.stem().string();
        return result;
    } catch (const std::exception& error) {
        auto source = modelPath.string();
        if (!sidecarPath.empty()) source += " + " + sidecarPath.string();
        throw std::runtime_error("failed to load '" + source + "': " + error.what());
    }
}

} // namespace VulkCore
