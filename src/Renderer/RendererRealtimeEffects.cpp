#include "RendererImplementation.hpp"
#include "HdrComposition.hpp"

#include <bit>
#include <cstring>
#include <span>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <stb_image.h>

namespace VulkCore {
namespace {

constexpr unsigned kParameters = 96;
constexpr unsigned kTrackCount = 26;
constexpr unsigned kCustomStart = 40;
constexpr unsigned kMaximumEffects = 32;
constexpr auto kHostMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
constexpr auto kStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
enum Kind : unsigned { Particles, Mesh, Ribbon, Light, Post };
enum Flags : unsigned { Additive = 1, DepthWrite = 2, Wireframe = 4, AfterBloom = 8, OpaqueInput = 16 };
enum Track : unsigned {
    Px, Py, Pz, Rx, Ry, Rz, Sx, Sy, Sz, Intensity, Opacity,
    Speed, Gravity, Drag, Turbulence, Size, Stretch, RotationSpeed,
    Radius, Width, Height, RibbonWidth, RibbonRadius, LightIntensity, PostStrength, PostRadius
};
static_assert(PostRadius + 1 == kTrackCount);

void require(bool value, const char* message = "Invalid VulkCore realtime effect package.") {
    if (!value) throw std::runtime_error(message);
}

class Reader {
public:
    explicit Reader(const std::filesystem::path& path) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        require(!ec && size > 0 && size <= 128u * 1024 * 1024, "Cannot open effect package or package exceeds 128 MiB.");
        bytes_.resize(size);
        std::ifstream file(path, std::ios::binary);
        require(bool(file.read(reinterpret_cast<char*>(bytes_.data()), bytes_.size())), "Cannot read effect package.");
    }
    unsigned uint(unsigned limit = std::numeric_limits<unsigned>::max()) {
        const auto data = take(4);
        const auto value = (unsigned(data[0]) | unsigned(data[1]) << 8 | unsigned(data[2]) << 16 | unsigned(data[3]) << 24);
        require(value <= limit);
        return value;
    }
    float number() {
        const auto value = std::bit_cast<float>(uint());
        require(std::isfinite(value));
        return value;
    }
    glm::vec3 vec3() {
        const auto x = number(), y = number(), z = number();
        return {x, y, z};
    }
    std::span<const std::uint8_t> blob(unsigned limit) { return take(uint(limit)); }
    std::string string(unsigned limit) {
        auto value = blob(limit);
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }
    bool finished() const { return offset_ == bytes_.size(); }
    std::size_t remaining() const { return bytes_.size() - offset_; }
private:
    std::span<const std::uint8_t> take(std::size_t size) {
        require(size <= remaining());
        auto part = std::span(bytes_).subspan(offset_, size);
        offset_ += size;
        return part;
    }
    std::vector<std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

struct Curve {
    float base = 0.0f;
    unsigned mode = 0;
    std::vector<glm::vec2> keys;
    float at(float time) const {
        if (keys.empty()) return base;
        const auto end = std::lower_bound(keys.begin(), keys.end(), time,
            [](glm::vec2 key, float t) { return key.x < t; });
        if (end == keys.begin()) return end->y;
        if (end == keys.end()) return keys.back().y;
        const auto start = end - 1;
        if (mode == 1) return start->y;
        float t = (time - start->x) / (end->x - start->x);
        if (mode == 2) t = t * t * (3.0f - 2.0f * t);
        return glm::mix(start->y, end->y, t);
    }
};

struct EffectVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};
struct LayerUniform {
    glm::mat4 model{1.0f};
    std::array<float, kParameters> values{};
    std::array<glm::vec4, 128> life{};
};
static_assert(sizeof(EffectVertex) == 8 * sizeof(float));
static_assert(sizeof(LayerUniform) == (16 + kParameters + 128 * 4) * sizeof(float));

struct LayerFrame {
    VkDescriptorSet set = VK_NULL_HANDLE;
    std::unique_ptr<Buffer> uniform;
    std::unique_ptr<Buffer> ribbon;
    bool active = false;
};
struct Layer {
    unsigned kind{}, flags{}, shape{}, path{}, segments{};
    bool horizontal{};
    float start{}, duration{}, softness{}, style{}, continuous{}, cycle{}, emissionWindow{};
    float ribbonHeight{}, turns{}, speed{}, tail{}, jitter{}, salt{}, lightDistance{}, lightDecay{};
    glm::vec3 tint{}, secondary{};
    std::array<unsigned, 5> textures{};
    std::array<Curve, kTrackCount + kParameters> tracks;
    std::vector<unsigned> animatedCustom;
    LayerUniform uniforms;
    std::array<VkShaderModule, 2> modules{};
    VkPipeline pipeline = VK_NULL_HANDLE;
    std::unique_ptr<Buffer> geometry, indices, particles;
    std::vector<EffectVertex> ribbonVertices;
    std::array<LayerFrame, kMaxFramesInFlight> frames;
    unsigned indexCount = 0, instanceCount = 1;
};

std::unique_ptr<Buffer> upload(VulkanContext& context, const void* data, std::size_t size, VkBufferUsageFlags usage) {
    auto buffer = std::make_unique<Buffer>(context, size, usage, kHostMemory);
    buffer->upload(data, size);
    return buffer;
}

VkShaderModule readModule(VulkanContext& context, Reader& reader) {
    const auto bytes = reader.blob(1024 * 1024);
    require(bytes.size() >= 20 && bytes.size() % 4 == 0, "Invalid effect SPIR-V module size.");
    std::vector<std::uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    require(words[0] == 0x07230203 && words[1] == 0x10000 && words[4] == 0, "Invalid effect SPIR-V header.");
    for (std::size_t i = 5; i < words.size();) {
        const auto size = words[i] >> 16;
        require(size && size <= words.size() - i, "Invalid effect SPIR-V instruction.");
        i += size;
    }
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = bytes.size();
    info.pCode = words.data();
    VkShaderModule module{};
    require(vkCreateShaderModule(context.device(), &info, nullptr, &module) == VK_SUCCESS,
        "Failed to create Vulkan effect shader module.");
    return module;
}

void readLayer(VulkanContext& context, Reader& reader, Layer& layer, unsigned textureCount, unsigned& particleBudget) {
    layer.kind = reader.uint(Post);
    layer.flags = reader.uint(31);
    layer.start = reader.number();
    layer.duration = reader.number();
    require(layer.start >= 0 && layer.start <= 120 && layer.duration > 0 && layer.duration <= 120);
    layer.softness = reader.number();
    layer.style = reader.number();
    layer.continuous = reader.number();
    layer.cycle = reader.number();
    layer.emissionWindow = reader.number();
    layer.tint = reader.vec3();
    layer.secondary = reader.vec3();
    layer.shape = reader.uint(6);
    layer.horizontal = reader.uint(1) != 0;
    layer.path = reader.uint(3);
    layer.segments = reader.uint(1024);
    layer.ribbonHeight = reader.number();
    layer.turns = reader.number();
    layer.speed = reader.number();
    layer.tail = reader.number();
    layer.jitter = reader.number();
    layer.salt = reader.number();
    layer.lightDistance = reader.number();
    layer.lightDecay = reader.number();
    for (auto& texture : layer.textures) texture = reader.uint(textureCount);
    for (auto& value : layer.uniforms.values) value = reader.number();
    std::array<bool, kTrackCount + kParameters> seen{};
    const auto tracks = reader.uint(kTrackCount + (kParameters - kCustomStart) / 4);
    for (unsigned i = 0; i < tracks; ++i) {
        const auto slot = reader.uint(kTrackCount + kParameters - 1);
        require(!seen[slot]);
        seen[slot] = true;
        if (slot >= kTrackCount) {
            const auto value = slot - kTrackCount;
            require(value >= kCustomStart && value % 4 == 0);
            layer.animatedCustom.push_back(value);
        }
        auto& curve = layer.tracks[slot];
        curve.base = reader.number();
        curve.mode = reader.uint(2);
        curve.keys.resize(reader.uint(1024));
        float previous = -std::numeric_limits<float>::infinity();
        for (auto& key : curve.keys) {
            key = {reader.number(), reader.number()};
            require(key.x > previous);
            previous = key.x;
        }
    }
    require(std::all_of(seen.begin(), seen.begin() + kTrackCount, [](bool value) { return value; }));
    if (layer.kind == Light) return;
    if (layer.kind == Particles) {
        for (auto& entry : layer.uniforms.life) entry = {reader.number(), reader.number(), 1.0f, 1.0f};
    }
    for (auto& module : layer.modules) module = readModule(context, reader);
    const auto vertexCount = reader.uint(500000);
    require(vertexCount && vertexCount <= reader.remaining() / sizeof(EffectVertex));
    std::vector<EffectVertex> vertices(vertexCount);
    for (auto& vertex : vertices) {
        vertex.position = reader.vec3();
        vertex.normal = reader.vec3();
        vertex.uv = {reader.number(), reader.number()};
    }
    if (layer.kind == Ribbon) {
        require(layer.segments && vertices.size() == 2 * (layer.segments + 1));
        layer.ribbonVertices = std::move(vertices);
    } else {
        layer.geometry = upload(context, vertices.data(), vertices.size() * sizeof(EffectVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
    layer.indexCount = reader.uint(3000000);
    const unsigned group = (layer.flags & Wireframe) && layer.kind != Post ? 2 : 3;
    require(layer.indexCount > 0 && layer.indexCount % group == 0 && layer.indexCount <= reader.remaining() / sizeof(unsigned));
    std::vector<std::uint32_t> indices(layer.indexCount);
    for (auto& index : indices) index = reader.uint(vertexCount - 1);
    layer.indices = upload(context, indices.data(), indices.size() * sizeof(indices[0]), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    const auto instances = reader.uint(particleBudget);
    require((instances > 0) == (layer.kind == Particles));
    if (instances) {
        particleBudget -= instances;
        layer.instanceCount = instances;
        std::vector<float> data(instances * 11);
        for (auto& value : data) value = reader.number();
        layer.particles = upload(context, data.data(), data.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
}

VkPipeline createEffectPipeline(VkDevice device, VkPipelineLayout layout, VkRenderPass pass, const Layer& layer) {
    const bool post = layer.kind == Post;
    const bool particle = layer.kind == Particles;
    const std::array stages{
        VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
            VK_SHADER_STAGE_VERTEX_BIT, layer.modules[0], "main", nullptr},
        VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
            VK_SHADER_STAGE_FRAGMENT_BIT, layer.modules[1], "main", nullptr}
    };
    const std::array bindings{
        VkVertexInputBindingDescription{0, sizeof(EffectVertex), VK_VERTEX_INPUT_RATE_VERTEX},
        VkVertexInputBindingDescription{1, 11 * sizeof(float), VK_VERTEX_INPUT_RATE_INSTANCE}
    };
    const std::array attributes{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(EffectVertex, position)},
        VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(EffectVertex, normal)},
        VkVertexInputAttributeDescription{2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(EffectVertex, uv)},
        VkVertexInputAttributeDescription{3, 1, VK_FORMAT_R32G32B32_SFLOAT, 0},
        VkVertexInputAttributeDescription{4, 1, VK_FORMAT_R32G32B32_SFLOAT, 12},
        VkVertexInputAttributeDescription{5, 1, VK_FORMAT_R32_SFLOAT, 24},
        VkVertexInputAttributeDescription{6, 1, VK_FORMAT_R32_SFLOAT, 28},
        VkVertexInputAttributeDescription{7, 1, VK_FORMAT_R32_SFLOAT, 32},
        VkVertexInputAttributeDescription{8, 1, VK_FORMAT_R32_SFLOAT, 36},
        VkVertexInputAttributeDescription{9, 1, VK_FORMAT_R32_SFLOAT, 40}
    };
    VkPipelineVertexInputStateCreateInfo vertex{};
    vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex.vertexBindingDescriptionCount = particle ? 2 : 1;
    vertex.pVertexBindingDescriptions = bindings.data();
    vertex.vertexAttributeDescriptionCount = particle ? attributes.size() : 3;
    vertex.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = !post && (layer.flags & Wireframe) ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo samples{};
    samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = !post;
    depth.depthWriteEnable = !post && (layer.flags & DepthWrite);
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    const bool add = layer.flags & Additive;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = !post;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = add ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.srcAlphaBlendFactor = add ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = add ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0,
        dynamicStates.size(), dynamicStates.data()};
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = stages.size();
    info.pStages = stages.data();
    info.pVertexInputState = &vertex;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &samples;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    info.renderPass = pass;
    VkPipeline pipeline = VK_NULL_HANDLE;
    require(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) == VK_SUCCESS,
        "Failed to create Vulkan effect pipeline. Check the shader interface and device capabilities.");
    return pipeline;
}

void updateRibbon(Layer& layer, LayerFrame& frame, float time, float width, float radius,
                  const glm::mat4& model, glm::vec3 camera) {
    if (std::abs(glm::determinant(model)) < std::numeric_limits<float>::min()) return;
    const glm::vec3 localCamera = glm::inverse(model) * glm::vec4(camera, 1.0f);
    const auto point = [&](unsigned i) -> glm::vec3 {
        constexpr float pi = glm::pi<float>();
        const auto u = float(i) / float(layer.segments);
        const auto f = time * layer.speed * 0.35f - (1 - u) * layer.tail;
        const auto angle = f * 2 * pi * layer.turns;
        if (layer.path == 3) {
            const float frameNumber = std::floor(time * 18);
            const float taper = layer.jitter * std::sin(u * pi);
            return {std::sin(i * 7.7f + frameNumber + layer.salt) * taper, u * layer.ribbonHeight,
                std::cos(i * 11.3f + frameNumber) * taper};
        }
        if (layer.path == 2) {
            const float t = glm::clamp(f, 0.0f, 1.0f);
            const float s = std::sin(t * pi);
            return {(t - 0.5f) * radius * 2, s * layer.ribbonHeight, s * radius * 0.35f};
        }
        const float height = layer.path == 1 ? std::sin(angle * 0.7f) * layer.ribbonHeight * 0.4f : f * layer.ribbonHeight;
        return {std::cos(angle) * radius, height, std::sin(angle) * radius};
    };
    auto previous = point(0);
    auto current = previous;
    for (unsigned i = 0; i <= layer.segments; ++i) {
        const auto next = i < layer.segments ? point(i + 1) : current;
        glm::vec3 side = glm::cross(next - previous, localCamera - current);
        const float length = glm::length(side);
        side = length > 0.000001f ? side * (width * 0.5f / length) : glm::vec3(0.0f);
        layer.ribbonVertices[i * 2].position = current - side;
        layer.ribbonVertices[i * 2 + 1].position = current + side;
        previous = current;
        current = next;
    }
    frame.ribbon->upload(layer.ribbonVertices.data(), layer.ribbonVertices.size() * sizeof(EffectVertex));
}

} // namespace

class RealtimeEffect {
public:
    RealtimeEffect(VulkanContext& context, std::filesystem::path source) : path(std::move(source)), context_(context) {}
    ~RealtimeEffect() {
        const auto device = context_.device();
        destroyPipelines();
        vkDestroyDescriptorPool(device, pool, nullptr);
        vkDestroyPipelineLayout(device, layout, nullptr);
        vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
        for (const auto& layer : layers) for (auto module : layer.modules) vkDestroyShaderModule(device, module, nullptr);
    }
    void load() {
        Reader reader(path);
        require(reader.string(64) == "VulkCoreRealtimeEffect",
            "Select a .vfx3d file exported by Effects Studio.");
        const auto version = reader.uint();
        require(version == 1 || version == 2, "Unsupported .vfx3d version.");
        name = reader.string(1024);
        duration = reader.number();
        require(duration >= 0.1f && duration <= 120);
        loop = reader.uint(1) != 0;
        seed = reader.number();
        bloom = reader.uint(1) != 0;
        bloomStrength = reader.number();
        bloomThreshold = reader.number();
        bloomRadius = reader.number();
        require(bloomStrength >= 0 && bloomThreshold >= 0 && bloomRadius >= 0 && bloomRadius <= 1);
        if (version >= 2) {
            previewPosition = reader.vec3();
            previewTarget = reader.vec3();
            previewFov = reader.number();
            previewAspect = reader.number();
            const float distance = glm::distance(previewPosition, previewTarget);
            require(distance >= 0.001f && distance <= 10000.0f &&
                previewFov > 0.0f && previewFov < 179.0f &&
                previewAspect >= 0.01f && previewAspect <= 100.0f, "Invalid effect preview camera.");
        }
        const auto textureCount = reader.uint(100);
        std::uint64_t pixels = 0;
        for (unsigned i = 0; i < textureCount; ++i) {
            const auto colorSpace = reader.uint(1) ? TextureColorSpace::Srgb : TextureColorSpace::Linear;
            auto bytes = reader.blob(96 * 1024 * 1024);
            int width = 0, height = 0, channels = 0;
            require(stbi_info_from_memory(bytes.data(), int(bytes.size()), &width, &height, &channels) &&
                width > 0 && height > 0 && width <= 8192 && height <= 8192, "Invalid effect texture dimensions.");
            pixels += std::uint64_t(width) * height;
            require(pixels <= 64 * 1024 * 1024, "Effect texture pixel budget exceeded.");
            constexpr auto clamp = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            textures.push_back(std::make_unique<Texture>(context_, std::vector(bytes.begin(), bytes.end()), colorSpace, clamp));
        }
        layers.resize(reader.uint(48));
        require(!layers.empty());
        unsigned particleBudget = 20000;
        for (auto& layer : layers) readLayer(context_, reader, layer, textureCount, particleBudget);
        require(reader.finished());
        createDescriptors();
    }
    bool hasPost() const {
        return std::any_of(layers.begin(), layers.end(), [](const auto& layer) { return layer.kind == Post; });
    }
    void destroyPipelines() {
        for (auto& layer : layers) {
            if (!layer.pipeline) continue;
            vkDestroyPipeline(context_.device(), layer.pipeline, nullptr);
            layer.pipeline = VK_NULL_HANDLE;
        }
    }
    void preparePipelines(VkRenderPass geometryPass, VkRenderPass postPass) {
        for (auto& layer : layers) {
            if (layer.pipeline || layer.kind == Light) continue;
            const auto pass = layer.kind == Post ? postPass : geometryPass;
            if (pass) layer.pipeline = createEffectPipeline(context_.device(), layout, pass, layer);
        }
    }
    void draw(VkCommandBuffer commands, Layer& layer, std::size_t frameIndex, VkBuffer scene,
              const VkDescriptorImageInfo& depth, const VkDescriptorImageInfo& color,
              const Texture& white) const {
        auto& frame = layer.frames[frameIndex];
        std::array<VkDescriptorBufferInfo, 2> buffers{{
            {scene, 0, sizeof(UniformBufferObject)}, {frame.uniform->handle(), 0, sizeof(LayerUniform)}
        }};
        const auto imageInfo = [&](unsigned id) {
            const auto& texture = id ? *textures[id - 1] : white;
            return VkDescriptorImageInfo{texture.sampler(), texture.imageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        };
        std::array<VkDescriptorImageInfo, 8> images{
            imageInfo(layer.textures[0]), depth, imageInfo(layer.textures[1]), imageInfo(layer.textures[2]),
            imageInfo(layer.textures[3]), imageInfo(layer.textures[4]), color, imageInfo(0)
        };
        std::array<VkWriteDescriptorSet, 10> writes{};
        for (unsigned i = 0; i < writes.size(); ++i) {
            auto& write = writes[i];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = frame.set;
            write.dstBinding = i;
            write.descriptorCount = 1;
            write.descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            if (i < 2) write.pBufferInfo = &buffers[i];
            else write.pImageInfo = &images[i - 2];
        }
        vkUpdateDescriptorSets(context_.device(), writes.size(), writes.data(), 0, nullptr);
        const auto vertex = frame.ribbon ? frame.ribbon->handle() : layer.geometry->handle();
        const std::array vertexBuffers{vertex, layer.particles ? layer.particles->handle() : VK_NULL_HANDLE};
        constexpr std::array<VkDeviceSize, 2> offsets{};
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layer.pipeline);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &frame.set, 0, nullptr);
        vkCmdBindVertexBuffers(commands, 0, layer.particles ? 2 : 1, vertexBuffers.data(), offsets.data());
        vkCmdBindIndexBuffer(commands, layer.indices->handle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commands, layer.indexCount, layer.instanceCount, 0, 0, 0);
    }
    std::filesystem::path path;
    std::string name;
    // Older exports use the default Effects Studio orbit camera.
    glm::vec3 previewPosition{6.0f, 4.1f, 7.6f};
    glm::vec3 previewTarget{0.0f, 1.0f, 0.0f};
    float previewFov = 43.0f;
    float previewAspect = 16.0f / 9.0f;
    float duration = 0, seed = 0, time = 0;
    bool playing = true, loop = false, autoPlay = true, bloom = false;
    float bloomStrength = 0, bloomThreshold = 1, bloomRadius = 0;
    std::optional<glm::vec3> position;
    std::vector<Layer> layers;
private:
    void createDescriptors() {
        const auto visible = std::count_if(layers.begin(), layers.end(), [](const auto& l) { return l.kind != Light; });
        if (!visible) return;
        const auto device = context_.device();
        std::array<VkDescriptorSetLayoutBinding, 10> bindings{};
        for (unsigned i = 0; i < bindings.size(); ++i) {
            bindings[i] = {i, i < 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                1, kStages, nullptr};
        }
        const VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0,
            bindings.size(), bindings.data()};
        require(vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &setLayout) == VK_SUCCESS, "Failed to create effect set layout.");
        const VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &setLayout, 0, nullptr};
        require(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout) == VK_SUCCESS, "Failed to create effect pipeline layout.");
        const auto setCount = std::uint32_t(visible * kMaxFramesInFlight);
        const std::array sizes{VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, setCount * 2},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, setCount * 8}};
        const VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, setCount,
            sizes.size(), sizes.data()};
        require(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) == VK_SUCCESS, "Failed to create effect descriptor pool.");
        const std::vector layouts(setCount, setLayout);
        std::vector<VkDescriptorSet> sets(setCount);
        const VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, pool, setCount, layouts.data()};
        require(vkAllocateDescriptorSets(device, &allocate, sets.data()) == VK_SUCCESS, "Failed to allocate effect descriptors.");
        unsigned i = 0;
        for (auto& layer : layers) {
            if (layer.kind == Light) continue;
            for (auto& frame : layer.frames) {
                frame.set = sets[i++];
                frame.uniform = std::make_unique<Buffer>(context_, sizeof(LayerUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kHostMemory);
                if (layer.kind == Ribbon) frame.ribbon = std::make_unique<Buffer>(context_,
                    layer.ribbonVertices.size() * sizeof(EffectVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kHostMemory);
            }
        }
    }
    VulkanContext& context_;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<Texture>> textures;
};

bool Renderer::hasRealtimePostEffects() const {
    return std::any_of(realtimeEffects_.begin(), realtimeEffects_.end(), [](const auto& pair) { return pair.second->hasPost(); });
}

HdrSettings Renderer::realtimeHdrSettings(bool includeInactive) const {
    if (hdrSettings_.enabled) return hdrSettings_;
    HdrSettings settings;
    settings.clearColor = {0.015f, 0.02f, 0.03f};
    settings.bloomIntensity = 0.0f;
    settings.bloomRadius = 0.0f;
    settings.bloomThreshold = std::numeric_limits<float>::max();
    for (const auto& [id, effect] : realtimeEffects_) {
        if (!effect->bloom || (!includeInactive && (!effect->playing || !isSceneNodeRenderEnabled(id)))) continue;
        settings.enabled = true;
        settings.bloomLevels = 5;
        settings.bloomIntensity = std::max(settings.bloomIntensity, effect->bloomStrength);
        settings.bloomThreshold = std::min(settings.bloomThreshold, effect->bloomThreshold);
        settings.bloomRadius = std::max(settings.bloomRadius, effect->bloomRadius);
    }
    return settings;
}

void Renderer::addRealtimeEffect(std::uint32_t id, const std::filesystem::path& path) {
    require(realtimeEffects_.size() < kMaximumEffects, "The scene reached its realtime effect instance limit.");
    require(!findSceneNodeById(meshData_, id) && !realtimeEffects_.contains(id), "Duplicate effect node ID.");
    auto effect = std::make_shared<RealtimeEffect>(*context_, path);
    effect->load();
    ModelNode node;
    node.name = effect->name.empty() ? pathUtf8(path.stem()) : effect->name;
    node.nodeId = id;
    node.objectType = SceneObjectType::RealtimeEffect;
    realtimeEffects_.emplace(id, effect);
    try {
        // Pipeline creation validates the imported shaders against the live render passes.
        if constexpr (kHdrCompositionEnabled) {
            const bool addPost = effect->hasPost() && (!hdrComposition_ || !hdrComposition_->effectPostPass());
            const bool addBloom = realtimeHdrSettings(true).bloomLevels && (!hdrComposition_ || !hdrComposition_->hasBloom());
            if (addPost || addBloom) {
                context_->waitIdle();
                configureHdrComposition();
            }
            require(!effect->hasPost() || (hdrComposition_ && hdrComposition_->effectPostPass()),
                "Effect post-processing requires an HDR-capable Vulkan device.");
        }
        effect->preparePipelines(hdrComposition_ ? hdrComposition_->geometryPass() : renderPass_,
            hdrComposition_ ? hdrComposition_->effectPostPass() : VK_NULL_HANDLE);
        meshData_.additionalNodes.push_back(std::move(node));
    } catch (...) {
        realtimeEffects_.erase(id);
        if (hdrComposition_ &&
            ((hdrComposition_->effectPostPass() && !hasRealtimePostEffects()) ||
             (hdrComposition_->hasBloom() && !realtimeHdrSettings(true).bloomLevels))) {
            configureHdrComposition();
        }
        throw;
    }
}

glm::vec3 Renderer::realtimeEffectSceneUp() const {
    return terrainScene_ ? glm::vec3(0.0f, 1.0f, 0.0f) :
        (authoredView_.enabled ? authoredView_.up : kDefaultSceneUp);
}

void Renderer::restoreSceneCamera() {
    if (!previousSceneCamera_) return;
    cameraPosition_ = previousSceneCamera_->position;
    cameraTarget_ = previousSceneCamera_->target;
    effectCamera_ = previousSceneCamera_->framing;
    previousSceneCamera_.reset();
    cameraBasisInitialized_ = false;
    cameraOrbitDragging_ = false;
}

#ifndef VULKCORE_OHOS
void Renderer::frameRealtimeEffect(const ModelNode& node) {
    const auto& effect = *realtimeEffects_.at(node.nodeId);
    if (!previousSceneCamera_) {
        previousSceneCamera_ = SceneCamera{cameraPosition_, cameraTarget_, effectCamera_};
        if (terrainEditorCamera_ && !playerController_.active()) {
            previousSceneCamera_->position = terrainEditorCamera_->originalPosition;
            previousSceneCamera_->target = terrainEditorCamera_->originalTarget;
        }
    }
    terrainEditorCamera_.reset();
    sceneEditTool_ = SceneEditTool::SelectTransform;

    const auto basis = glm::rotation(glm::vec3(0, 1, 0), realtimeEffectSceneUp());
    const auto rotation = glm::quat(glm::radians(node.transformRotationDegrees)) * basis;
    const float size = node.transformScaleFactor;
    const auto origin = effect.position.value_or(node.transformTranslation);
    const auto toScene = [&](glm::vec3 point) {
        return (origin - modelCenter_ + rotation * (point * size)) * modelScale_;
    };
    cameraTarget_ = toScene(effect.previewTarget);
    cameraPosition_ = cameraTarget_ + rotation * (effect.previewPosition - effect.previewTarget) * (size * modelScale_);
    effectCamera_ = EffectCamera{rotation * glm::vec3(0.0f, 1.0f, 0.0f), effect.previewFov, effect.previewAspect};
    cameraBasisInitialized_ = false;
    cameraOrbitDragging_ = false;
}

void Renderer::importRealtimeEffect(const std::filesystem::path& path) {
    try {
        std::uint32_t id = kParticleSceneNodeBase;
        while (findSceneNodeById(meshData_, id) || removedSceneNodeIds_.contains(id) || particleSystems_.contains(id)) {
            require(++id < kLegacyPlayerEffectNodeBase, "No free effect node ID.");
        }
        addRealtimeEffect(id, normalizedPath(path));
        selectedModelNode_ = &meshData_.additionalNodes.back();
        const auto basis = glm::rotation(glm::vec3(0, 1, 0), realtimeEffectSceneUp());
        // Match the preview's point of interest to the current scene aim.
        selectedModelNode_->transformTranslation = cameraTarget_ / modelScale_ + modelCenter_ -
            basis * realtimeEffects_.at(id)->previewTarget;
        frameRealtimeEffect(*selectedModelNode_);
        editorSelection_ = EditorSelection::Node;
        editorPart_.reset();
        editorDirty_ = true;
        sceneStatusFailed_ = false;
        sceneStatus_ = "Imported realtime effect. Save the scene to retain it.";
    } catch (const std::exception& error) {
        sceneStatusFailed_ = true;
        sceneStatus_ = error.what();
    }
}
#endif

bool Renderer::playRealtimeEffect(std::uint32_t id, std::optional<glm::vec3> worldPosition) {
    const auto found = realtimeEffects_.find(id);
    if (found == realtimeEffects_.end()) return false;
    auto& effect = *found->second;
    effect.position = worldPosition;
    effect.time = 0.0f;
    effect.playing = true;
    return true;
}

bool Renderer::stopRealtimeEffect(std::uint32_t id) {
    const auto found = realtimeEffects_.find(id);
    if (found == realtimeEffects_.end()) return false;
    found->second->playing = false;
    return true;
}

bool Renderer::drawRealtimeEffectControls(ModelNode& node) {
    const auto it = realtimeEffects_.find(node.nodeId);
    if (it == realtimeEffects_.end()) return false;
    auto& effect = *it->second;
    ImGui::SeparatorText("Realtime Effect");
    ImGui::TextDisabled("%zu layers / %.2f s", effect.layers.size(), effect.duration);
    ImGui::TextWrapped("%s", pathUtf8(effect.path).c_str());
    bool edited = ImGui::DragFloat3("Position", &node.transformTranslation.x, 0.05f);
    edited |= ImGui::DragFloat3("Rotation", &node.transformRotationDegrees.x, 1.0f);
    edited |= ImGui::DragFloat("Scale", &node.transformScaleFactor, 0.025f, kMinimumModelScaleFactor, kMaximumRealtimeEffectScale,
        "%.2f", ImGuiSliderFlags_AlwaysClamp);
    const auto skill = std::find_if(playerSkillEffects_.begin(), playerSkillEffects_.end(),
        [&node](const auto& binding) { return binding && binding->nodeId == node.nodeId; });
    if (skill == playerSkillEffects_.end()) {
        edited |= ImGui::Checkbox("Play on awake", &effect.autoPlay);
        edited |= ImGui::Checkbox("Loop", &effect.loop);
    } else {
        ImGui::TextDisabled("Player skill %c", kPlayerSkillKeys[skill - playerSkillEffects_.begin()]);
    }
    if (ImGui::Button("Replay")) playRealtimeEffect(node.nodeId);
    ImGui::SameLine();
    if (ImGui::Button("Stop")) stopRealtimeEffect(node.nodeId);
#ifndef VULKCORE_OHOS
    if (ImGui::Button("Frame Effect")) {
        frameRealtimeEffect(node);
        edited |= !playerController_.active() && !transitionSettings_.enabled;
    }
    if (previousSceneCamera_) {
        ImGui::SameLine();
        if (ImGui::Button("Return to Scene Camera")) {
            restoreSceneCamera();
            edited |= !playerController_.active() && !transitionSettings_.enabled;
        }
    }
#endif
    return edited;
}

void Renderer::saveRealtimeEffects(std::ostream& file,
    const std::function<std::string(const std::filesystem::path&)>& reference) const {
    for (const auto& node : meshData_.additionalNodes) {
        const auto it = realtimeEffects_.find(node.nodeId);
        if (it == realtimeEffects_.end()) continue;
        const auto& effect = *it->second;
        file << "realtime_effect " << node.nodeId << ' ' << std::quoted(reference(effect.path))
             << ' ' << effect.autoPlay << ' ' << effect.loop;
        for (std::size_t index = 0; index < playerSkillEffects_.size(); ++index) {
            const auto& skill = playerSkillEffects_[index];
            if (skill && skill->nodeId == node.nodeId) {
                file << " skill " << kPlayerSkillKeys[index] << ' ' << skill->forwardOffset
                     << ' ' << skill->heightOffset << ' ' << skill->followPlayer;
                break;
            }
        }
        file << '\n';
    }
}

void Renderer::loadRealtimeEffect(std::uint32_t id, const std::filesystem::path& path, bool autoPlay, bool loop) {
    addRealtimeEffect(id, path);
    auto& effect = *realtimeEffects_.at(id);
    effect.autoPlay = effect.playing = autoPlay;
    effect.loop = loop;
}

void Renderer::invalidateRealtimeEffectPipelines() {
    for (auto& [id, effect] : realtimeEffects_) effect->destroyPipelines();
}

void Renderer::updatePlayerSkillEffects() {
    const float yaw = playerController_.facingYawRadians();
    const glm::vec3 forward(std::sin(yaw), 0.0f, std::cos(yaw));
    const auto triggered = static_cast<std::size_t>(playerController_.triggeredSkill());
    for (std::size_t index = 0; index < playerSkillEffects_.size(); ++index) {
        const auto& skill = playerSkillEffects_[index];
        if (!skill || (index != triggered && !skill->followPlayer)) continue;
        const glm::vec3 position = playerController_.position() + forward * skill->forwardOffset +
            glm::vec3(0.0f, skill->heightOffset, 0.0f);
        if (index == triggered) {
            playRealtimeEffect(skill->nodeId, position);
        } else if (const auto it = realtimeEffects_.find(skill->nodeId); it != realtimeEffects_.end()) {
            auto& effect = *it->second;
            // Replay in the editor uses the authored position rather than attaching to the player.
            if (effect.playing && effect.position) effect.position = position;
        }
    }
}

void Renderer::updateRealtimeEffects(float dt) {
    for (auto& [id, effect] : realtimeEffects_) {
        if (!effect->playing) continue;
        effect->time += dt;
        if (effect->time < effect->duration) continue;
        if (effect->loop) effect->time = std::fmod(effect->time, effect->duration);
        else effect->playing = false;
    }
}

void Renderer::prepareRealtimeEffects(UniformBufferObject& scene) {
    if (realtimeEffects_.empty()) return;
    const auto basis = glm::mat4_cast(glm::rotation(glm::vec3(0, 1, 0), realtimeEffectSceneUp()));
    for (const auto& node : meshData_.additionalNodes) {
        const auto it = realtimeEffects_.find(node.nodeId);
        if (it == realtimeEffects_.end()) continue;
        const auto& effect = it->second;
        const auto object = scene.model * glm::translate(glm::mat4(1.0f),
            effect->position.value_or(node.transformTranslation)) * glm::mat4_cast(glm::quat(glm::radians(node.transformRotationDegrees))) *
            basis * glm::scale(glm::mat4(1), glm::vec3(node.transformScaleFactor));
        const bool visible = effect->playing && isSceneNodeRenderEnabled(node.nodeId);
        for (auto& layer : effect->layers) {
            const float time = effect->time - layer.start;
            auto& frame = layer.frames[currentFrame_];
            frame.active = visible && time >= 0 && time <= layer.duration;
            if (!frame.active) continue;
            auto at = [&](Track index) { return layer.tracks[index].at(time); };
            if (layer.kind == Post && (!kHdrCompositionEnabled || at(PostStrength) <= 0.00001f)) {
                frame.active = false;
                continue;
            }
            const auto matrix = object * glm::translate(glm::mat4(1), glm::vec3(at(Px), at(Py), at(Pz))) *
                glm::eulerAngleXYZ(at(Rx), at(Ry), at(Rz)) * glm::scale(glm::mat4(1), glm::vec3(at(Sx), at(Sy), at(Sz)));
            const float scale = std::sqrt((glm::length2(glm::vec3(matrix[0])) + glm::length2(glm::vec3(matrix[1])) +
                glm::length2(glm::vec3(matrix[2]))) / 3.0f);
            if (layer.kind == Light) {
                auto& count = scene.effectLightCount.x;
                if (count < float(scene.effectLightPosition.size()) && at(LightIntensity) > 0.0f) {
                    const auto index = std::size_t(count++);
                    scene.effectLightPosition[index] = {glm::vec3(matrix[3]), layer.lightDistance * scale};
                    scene.effectLightColor[index] = {layer.tint * at(LightIntensity), layer.lightDecay};
                }
                continue;
            }
            auto& uniform = layer.uniforms;
            uniform.model = matrix;
            if (layer.kind == Mesh) {
                const auto r = at(Radius);
                const auto size = layer.shape == 1
                    ? glm::vec3(at(Width), layer.horizontal ? 1.0f : at(Height), layer.horizontal ? at(Height) : 1.0f)
                    : glm::vec3(r, layer.shape == 4 || layer.shape == 5 ? at(Height) : r, r);
                uniform.model = glm::scale(matrix, size);
            }
            const glm::vec3 viewCenter = scene.view * matrix[3];
            const auto projection = scene.projection;
            const float nearPlane = projection[3][2] / projection[2][2];
            const float farPlane = projection[2][3] == 0.0f ? (projection[3][2] - 1.0f) / projection[2][2]
                : projection[3][2] / (projection[2][2] + 1.0f);
            const std::array values{time, effect->time, time / layer.duration, effect->seed,
                at(Intensity), at(Opacity), layer.style, float(layer.kind == Ribbon),
                layer.softness, float(layer.textures[0] != 0), float(bool(layer.flags & OpaqueInput)), scale,
                layer.tint.r, layer.tint.g, layer.tint.b, 0.0f,
                layer.secondary.r, layer.secondary.g, layer.secondary.b, 0.0f,
                at(Speed), at(Gravity), at(Drag), at(Turbulence),
                at(Size), at(Stretch), at(RotationSpeed), layer.continuous,
                layer.cycle, layer.emissionWindow, at(PostStrength), 0.0f,
                nearPlane, farPlane, 0.0f, 0.0f,
                viewCenter.x, viewCenter.y, viewCenter.z, scale * at(PostRadius)};
            static_assert(values.size() == kCustomStart);
            std::copy(values.begin(), values.end(), uniform.values.begin());
            for (auto slot : layer.animatedCustom) uniform.values[slot] = layer.tracks[kTrackCount + slot].at(time);
            frame.uniform->upload(&uniform, sizeof(uniform));
            if (layer.kind == Ribbon) updateRibbon(layer, frame, time, at(RibbonWidth), at(RibbonRadius), matrix, cameraPosition_);
        }
    }
}

void Renderer::drawRealtimeEffects(VkCommandBuffer commands, bool hdr) {
    const auto& target = sceneRenderTargets_[currentFrame_];
    constexpr auto layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkDescriptorImageInfo depth{target.depthSampler, target.depthImageView, layout};
    const VkDescriptorImageInfo color{target.colorSampler, target.colorImageView, layout};
    for (const auto& node : meshData_.additionalNodes) {
        const auto it = realtimeEffects_.find(node.nodeId);
        if (it == realtimeEffects_.end()) continue;
        auto& effect = *it->second;
        const bool drawable = std::any_of(effect.layers.begin(), effect.layers.end(), [&](const auto& layer) {
            return layer.kind <= Ribbon && layer.frames[currentFrame_].active;
        });
        if (!drawable) continue;
        effect.preparePipelines(hdr ? hdrComposition_->geometryPass() : renderPass_,
            hdr ? hdrComposition_->effectPostPass() : VK_NULL_HANDLE);
        for (auto& layer : effect.layers) {
            if (layer.kind > Ribbon || !layer.frames[currentFrame_].active) continue;
            const VkDescriptorImageInfo white{fallbackTexture_->sampler(), fallbackTexture_->imageView(), layout};
            const bool readsOpaque = layer.flags & OpaqueInput;
            effect.draw(commands, layer, currentFrame_, uniformBuffers_[currentFrame_]->handle(),
                readsOpaque ? depth : white, readsOpaque ? color : white, *fallbackTexture_);
        }
    }
}

bool Renderer::realtimeNeedsOpaqueSnapshot() const {
    for (const auto& [id, effect] : realtimeEffects_) {
        // Custom materials can access opaque color/depth even at softness zero.
        for (const auto& layer : effect->layers) {
            if (layer.kind != Light && (layer.flags & OpaqueInput) && layer.frames[currentFrame_].active) return true;
        }
    }
    return false;
}

bool Renderer::hasActiveRealtimePost(bool afterBloom) const {
    for (const auto& [id, effect] : realtimeEffects_) {
        for (const auto& layer : effect->layers) {
            if (layer.kind == Post && layer.frames[currentFrame_].active &&
                bool(layer.flags & AfterBloom) == afterBloom) return true;
        }
    }
    return false;
}

unsigned Renderer::drawRealtimePost(VkCommandBuffer commands, unsigned source, bool afterBloom) {
    if (!hdrComposition_->effectPostPass()) return source;
    unsigned count = 0;
    const auto& target = sceneRenderTargets_[currentFrame_];
    constexpr auto layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkDescriptorImageInfo depth{target.depthSampler, target.depthImageView, layout};
    const VkDescriptorImageInfo white{fallbackTexture_->sampler(), fallbackTexture_->imageView(), layout};
    for (const auto& node : meshData_.additionalNodes) {
        const auto it = realtimeEffects_.find(node.nodeId);
        if (it == realtimeEffects_.end()) continue;
        auto& effect = *it->second;
        for (auto& layer : effect.layers) {
            if (layer.kind != Post || !layer.frames[currentFrame_].active || bool(layer.flags & AfterBloom) != afterBloom) continue;
            if (++count > 8) return source;
            effect.preparePipelines(hdrComposition_->geometryPass(), hdrComposition_->effectPostPass());
            const auto destination = hdrComposition_->beginEffectPost(commands, currentFrame_, source);
            effect.draw(commands, layer, currentFrame_, uniformBuffers_[currentFrame_]->handle(),
                (layer.flags & OpaqueInput) ? depth : white, hdrComposition_->effectPostSource(currentFrame_, source), *fallbackTexture_);
            vkCmdEndRenderPass(commands);
            source = destination;
        }
    }
    return source;
}

} // namespace VulkCore
