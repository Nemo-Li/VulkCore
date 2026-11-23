#include "RendererImplementation.hpp"

#include <stb_image.h>

namespace VulkCore {

void Renderer::importEffectSprite(const std::filesystem::path& atlasPath) {
    try {
        if (particleSystems_.size() >= kMaximumParticleSystems) {
            throw std::runtime_error("The scene is at its particle and effect component limit.");
        }

        const auto source = normalizedPath(atlasPath);
        const auto metadataPath = source.parent_path() / "effect.vfx";
        constexpr auto invalid = "Select atlas.png from an extracted VulkCore effect sprite package.";
        std::error_code sizeError;
        if (std::filesystem::file_size(metadataPath, sizeError) > 4096 || sizeError) {
            throw std::runtime_error(invalid);
        }
        std::ifstream manifest(metadataPath);
        ParticleSystemSettings settings;
        auto& book = settings.flipbook;
        std::string tag;
        unsigned version = 0;
        std::string atlasName;
        if (!(manifest >> tag >> version >> book.columns >> book.rows >> book.frames >> book.cellPixels
              >> book.gutterPixels >> settings.duration >> book.width >> atlasName) ||
            tag != "vulkcore_flipbook" || version != 1 || atlasName != "atlas.png" ||
            source.filename() != atlasName || !book.valid() || !std::isfinite(settings.duration) ||
            settings.duration < 0.1f || settings.duration > 120.0f) {
            throw std::runtime_error(invalid);
        }

        int width = 0, height = 0, channels = 0;
        if (!stbi_info(source.string().c_str(), &width, &height, &channels) ||
            width != static_cast<int>(book.columns * book.cellPixels) ||
            height != static_cast<int>(book.rows * book.cellPixels) || channels != 4) {
            throw std::runtime_error("The sprite atlas dimensions or alpha channel do not match effect.vfx.");
        }
        settings.texturePath = source;
        settings.position = glm::vec3(0.0f);
        settings.maxParticles = 1;
        settings.blendMode = ParticleSystemSettings::BlendMode::Alpha;

        std::uint32_t id = kParticleSceneNodeBase;
        while (findSceneNodeById(meshData_, id) || removedSceneNodeIds_.contains(id) ||
            particleSystems_.contains(id)) {
            if (++id == kPlayerAttackParticleNodeId) throw std::runtime_error("No free effect node ID.");
        }

        auto texture = std::make_unique<Texture>(*context_, source,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        ModelNode node;
        node.name = source.parent_path().filename().string() + " (Effect)";
        node.nodeId = id;
        node.objectType = SceneObjectType::ParticleSystem;
        node.transformTranslation = cameraTarget_;

        particleTextures_.emplace(id, std::move(texture));
        try {
            auto& system = particleSystems_[id];
            system.setSettings(settings);
            system.restart();
            createParticleDescriptorSets(id);
            meshData_.additionalNodes.push_back(std::move(node));
        } catch (...) {
            if (const auto sets = particleDescriptorSets_.find(id); sets != particleDescriptorSets_.end()) {
                vkFreeDescriptorSets(context_->device(), descriptorPool_, kMaxFramesInFlight, sets->second.data());
                particleDescriptorSets_.erase(sets);
            }
            particleSystems_.erase(id);
            particleTextures_.erase(id);
            throw;
        }
        roadBrushEnabled_ = false;
        editorSelection_ = EditorSelection::Node;
        selectedModelNode_ = &meshData_.additionalNodes.back();
        editorPart_.reset();
        editorDirty_ = true;
        sceneStatusFailed_ = false;
        sceneStatus_ = "Imported effect sprite. Save the scene to retain the effect.";
    } catch (const std::exception& error) {
        sceneStatusFailed_ = true;
        sceneStatus_ = error.what();
    }
}

} // namespace VulkCore
