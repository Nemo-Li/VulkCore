#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

#include "vulkcore/Renderer/SceneEffects.hpp"

namespace VulkCore {

class Buffer;
class SwapChain;
class Texture;
class VulkanContext;

// Per-frame linear HDR targets and fullscreen effects. Scene materials and
// scene animation are handled by the caller.
class HdrComposition {
public:
    using ShaderLookup = std::function<VkShaderModule(const char*)>;

    static bool supported(const VulkanContext& context);
    HdrComposition(VulkanContext& context, const SwapChain& swapChain,
                   const HdrSettings& settings,
                   const std::vector<std::unique_ptr<Buffer>>& sceneUniforms,
                   const ShaderLookup& shader);
    ~HdrComposition();

    HdrComposition(const HdrComposition&) = delete;
    HdrComposition& operator=(const HdrComposition&) = delete;

    VkRenderPass geometryPass() const { return geometryPass_; }
    void beginGeometry(VkCommandBuffer commands, std::size_t frame, glm::vec3 background) const;
    void beginPresentation(VkCommandBuffer commands, std::size_t frame,
                           VkFramebuffer framebuffer, const HdrSettings& settings) const;

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    struct Target {
        Image color;
        Image depth;
        VkExtent2D extent{};
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };
    struct Frame {
        Target geometry;
        Target luminance;
        std::vector<Target> down;
        std::vector<Target> up;
        Target edges;
        Target weights;
        VkDescriptorSet extract = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> downSets;
        std::vector<VkDescriptorSet> upSets;
        VkDescriptorSet edgeSet = VK_NULL_HANDLE;
        VkDescriptorSet weightSet = VK_NULL_HANDLE;
        VkDescriptorSet composite = VK_NULL_HANDLE;
    };

    void createImage(Image& image, VkExtent2D extent, VkFormat format, bool depth);
    void createTarget(Target& target, VkExtent2D extent, VkRenderPass pass, VkFormat color,
                      VkFormat depth = VK_FORMAT_UNDEFINED);
    void createLayouts(std::size_t setCount);
    VkPipeline createPipeline(VkRenderPass pass, const ShaderLookup& shader, const char* fragment,
                              const char* vertexShader = "shader_fullscreen.vert");
    VkDescriptorSet descriptors(VkImageView source, VkImageView second = VK_NULL_HANDLE,
                                VkImageView third = VK_NULL_HANDLE, bool lookup = false,
                                VkImageView depth = VK_NULL_HANDLE, VkBuffer sceneUniform = VK_NULL_HANDLE) const;
    void run(VkCommandBuffer commands, const Target& target, VkRenderPass pass, VkPipeline pipeline,
             VkDescriptorSet set, const glm::vec4& parameters = {}) const;
    void draw(VkCommandBuffer commands, VkExtent2D extent, VkPipeline pipeline, VkDescriptorSet set,
              const glm::vec4& parameters) const;
    void destroyTarget(Target& target);
    void cleanup();

    VulkanContext& context_;
    VkExtent2D extent_{};
    bool encodeSrgb_ = false;
    VkRenderPass geometryPass_ = VK_NULL_HANDLE;
    VkRenderPass filterPass_ = VK_NULL_HANDLE;
    VkRenderPass aaPass_ = VK_NULL_HANDLE;
    VkRenderPass presentationPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkSampler linearSampler_ = VK_NULL_HANDLE;
    VkSampler nearestSampler_ = VK_NULL_HANDLE;
    std::unique_ptr<Texture> area_;
    std::unique_ptr<Texture> search_;
    VkPipeline extract_ = VK_NULL_HANDLE;
    VkPipeline down_ = VK_NULL_HANDLE;
    VkPipeline up_ = VK_NULL_HANDLE;
    VkPipeline edges_ = VK_NULL_HANDLE;
    VkPipeline weights_ = VK_NULL_HANDLE;
    VkPipeline composite_ = VK_NULL_HANDLE;
    std::vector<Frame> frames_;
};

} // namespace VulkCore
