#include "RendererImplementation.hpp"

namespace VulkCore {

void Renderer::selectModelNode(ModelNode& node) {
    selectedModelNode_ = &node;
    selectedResourceType_ = ResourceSelectionType::Model;
    selectedResourceMaterialIndex_ = -1;
    updateBoundingBoxVertices();
}

glm::mat4 Renderer::nodeManualTransform(const ModelNode& node) const {
    if (!hasNodeTransformSettings(node)) {
        return glm::mat4(1.0f);
    }

    const float safeModelScale = std::max(modelScale_, std::numeric_limits<float>::epsilon());
    const glm::mat4 rotation =
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(node.transformRotationDegrees.z),
            glm::vec3(0.0f, 0.0f, 1.0f)
        ) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(node.transformRotationDegrees.y),
            glm::vec3(0.0f, 1.0f, 0.0f)
        ) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(node.transformRotationDegrees.x),
            glm::vec3(1.0f, 0.0f, 0.0f)
        );
    const glm::mat4 localTransform =
        glm::translate(
            glm::mat4(1.0f),
            node.transformTranslation / safeModelScale
        ) *
        rotation *
        glm::scale(
            glm::mat4(1.0f),
            glm::vec3(std::max(node.transformScaleFactor, kMinimumModelScaleFactor))
        );

    // The imported geometry has already been flattened into model-root
    // coordinates. Conjugating the local transform keeps the selected node
    // as the pivot. The root uses the scene center to retain the previous
    // whole-model transform behavior.
    const glm::mat4 pivot = &node == &meshData_.hierarchy
        ? glm::translate(glm::mat4(1.0f), modelCenter_)
        : node.globalTransform;
    return pivot * localTransform * glm::inverse(pivot);
}

glm::mat4 Renderer::particleObjectTransform(const ModelNode& node) const {
    const glm::mat4 rotation =
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(node.transformRotationDegrees.z),
            glm::vec3(0.0f, 0.0f, 1.0f)
        ) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(node.transformRotationDegrees.y),
            glm::vec3(0.0f, 1.0f, 0.0f)
        ) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(node.transformRotationDegrees.x),
            glm::vec3(1.0f, 0.0f, 0.0f)
        );
    return glm::translate(glm::mat4(1.0f), node.transformTranslation) *
        rotation *
        glm::scale(
            glm::mat4(1.0f),
            glm::vec3(std::max(node.transformScaleFactor, kMinimumModelScaleFactor))
        );
}

glm::mat4 Renderer::nodeAnimationTransform(
    const ModelNode& node,
    float elapsedSeconds
) const {
    if (!node.rotationAnimationEnabled) {
        return glm::mat4(1.0f);
    }

    const glm::vec3 rotationDegrees = node.rotationAnimationInitialDegrees +
        node.rotationAnimationSpeedDegrees * elapsedSeconds;
    const glm::mat4 rotation =
        glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));

    // The flattened vertex buffer is in model-root coordinates. Conjugating
    // the local rotation by the node's global transform makes the selected
    // node's own coordinate system the animation frame.
    return node.globalTransform * rotation * glm::inverse(node.globalTransform);
}

void Renderer::updateSceneNodeTransforms(float elapsedSeconds) {
    sceneNodeTransforms_.clear();
    sceneNodeRenderEnabled_.clear();

    const auto updateNode = [this, elapsedSeconds](
                                const auto& self,
                                const ModelNode& node,
                                const glm::mat4& parentTransform,
                                bool parentRenderEnabled
                            ) -> void {
        glm::mat4 transform = parentTransform * nodeManualTransform(node);
        if (node.rotationAnimationEnabled) {
            // Parent animation is applied after child-local animation, so
            // transforms are accumulated in hierarchy order from root to leaf.
            transform *= nodeAnimationTransform(node, elapsedSeconds);
        }
        const bool renderEnabled = parentRenderEnabled && node.renderEnabled &&
            !removedSceneNodeIds_.contains(node.nodeId);
        sceneNodeTransforms_.insert_or_assign(node.nodeId, transform);
        sceneNodeRenderEnabled_.insert_or_assign(node.nodeId, renderEnabled);
        for (const ModelNode& child : node.children) {
            self(self, child, transform, renderEnabled);
        }
    };

    updateNode(updateNode, meshData_.hierarchy, glm::mat4(1.0f), true);
    for (const ModelNode& node : meshData_.additionalNodes) {
        updateNode(updateNode, node, glm::mat4(1.0f), true);
    }
}

glm::mat4 Renderer::transformForSceneNode(std::uint32_t nodeId) const {
    const auto transformIt = sceneNodeTransforms_.find(nodeId);
    return transformIt != sceneNodeTransforms_.end()
        ? transformIt->second
        : glm::mat4(1.0f);
}

bool Renderer::isSceneNodeRenderEnabled(std::uint32_t nodeId) const {
    if (removedSceneNodeIds_.contains(nodeId)) {
        return false;
    }
    const auto renderStateIt = sceneNodeRenderEnabled_.find(nodeId);
    if (renderStateIt != sceneNodeRenderEnabled_.end()) {
        return renderStateIt->second;
    }

    bool renderEnabled = true;
    if (findInheritedRenderEnabled(meshData_.hierarchy, nodeId, true, renderEnabled)) {
        return renderEnabled;
    }
    for (const ModelNode& node : meshData_.additionalNodes) {
        if (findInheritedRenderEnabled(node, nodeId, true, renderEnabled)) {
            return renderEnabled;
        }
    }
    return true;
}

void Renderer::updateBoundingBoxVertices() {
    boundingBoxVertices_.fill(Vertex{});
    if (selectedModelNode_ == nullptr || !selectedModelNode_->hasBounds) {
        return;
    }

    const glm::mat4 nodeTransform = transformForSceneNode(selectedModelNode_->nodeId);

    const glm::vec3& minimum = selectedModelNode_->boundsMinimum;
    const glm::vec3& maximum = selectedModelNode_->boundsMaximum;
    const std::array<glm::vec3, 8> corners = {
        glm::vec3(minimum.x, minimum.y, minimum.z),
        glm::vec3(maximum.x, minimum.y, minimum.z),
        glm::vec3(maximum.x, maximum.y, minimum.z),
        glm::vec3(minimum.x, maximum.y, minimum.z),
        glm::vec3(minimum.x, minimum.y, maximum.z),
        glm::vec3(maximum.x, minimum.y, maximum.z),
        glm::vec3(maximum.x, maximum.y, maximum.z),
        glm::vec3(minimum.x, maximum.y, maximum.z)
    };

    for (std::size_t vertexIndex = 0; vertexIndex < kBoundingBoxEdges.size(); ++vertexIndex) {
        boundingBoxVertices_[vertexIndex].position = glm::vec3(
            nodeTransform * glm::vec4(corners[kBoundingBoxEdges[vertexIndex]], 1.0f)
        );
    }
}

std::int32_t Renderer::inheritedMaterialOverrideForSceneNode(std::uint32_t nodeId) const {
    std::int32_t result = -1;
    if (findInheritedMaterialOverride(meshData_.hierarchy, nodeId, -1, result)) {
        return result;
    }
    for (const ModelNode& node : meshData_.additionalNodes) {
        if (findInheritedMaterialOverride(node, nodeId, -1, result)) {
            return result;
        }
    }
    return -1;
}

void Renderer::drawModelHierarchy(ModelNode& node) {
    const bool hasChildren = !node.children.empty();
    const bool isRootNode = &node == &meshData_.hierarchy;
    const bool nodeSelected = selectedModelNode_ == &node;
    std::string label = node.name.empty() ? "<unnamed>" : node.name;
    if (node.objectType == SceneObjectType::ParticleSystem) {
        label += "  [Particle System]";
    } else if (!isRootNode && node.meshCount > 0) {
        label += "  [" + std::to_string(node.meshCount) +
            (node.meshCount == 1 ? " mesh]" : " meshes]");
    }

    ImGui::PushID(&node);
    if (!hasChildren) {
        ImGui::TreeNodeEx(
            label.c_str(),
            ImGuiTreeNodeFlags_Leaf |
                ImGuiTreeNodeFlags_NoTreePushOnOpen |
                ImGuiTreeNodeFlags_SpanAvailWidth |
                (nodeSelected ? ImGuiTreeNodeFlags_Selected : 0)
        );
        if (ImGui::IsItemClicked()) {
            selectModelNode(node);
        }
    } else {
        const bool isOpen = ImGui::TreeNodeEx(
            label.c_str(),
            ImGuiTreeNodeFlags_SpanAvailWidth |
                (nodeSelected ? ImGuiTreeNodeFlags_Selected : 0)
        );
        if (ImGui::IsItemClicked()) {
            selectModelNode(node);
        }
        if (!isOpen) {
            ImGui::PopID();
            return;
        }

        for (ModelNode& child : node.children) {
            drawModelHierarchy(child);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void Renderer::drawResourceModelHierarchy(const ModelNode& node) {
    const bool hasChildren = !node.children.empty();
    std::string label = node.name.empty() ? "<unnamed>" : node.name;
    if (node.meshCount > 0) {
        label += "  [" + std::to_string(node.meshCount) +
            (node.meshCount == 1 ? " mesh]" : " meshes]");
    }

    ImGui::PushID(&node);
    if (!hasChildren) {
        ImGui::TreeNodeEx(
            label.c_str(),
            ImGuiTreeNodeFlags_Leaf |
                ImGuiTreeNodeFlags_NoTreePushOnOpen |
                ImGuiTreeNodeFlags_SpanAvailWidth
        );
    } else {
        const bool isOpen = ImGui::TreeNodeEx(
            label.c_str(),
            ImGuiTreeNodeFlags_SpanAvailWidth
        );
        if (isOpen) {
            for (const ModelNode& child : node.children) {
                drawResourceModelHierarchy(child);
            }
            ImGui::TreePop();
        }
    }
    ImGui::PopID();
}

void Renderer::drawNodeMaterialControls(ModelNode& node, bool& shouldSaveSettings) {
    ImGui::SeparatorText("Node Material");
    ImGui::TextDisabled("A material override applies to this node and its children.");

    const std::int32_t inheritedOverride = inheritedMaterialOverrideForSceneNode(node.nodeId);
    const std::int32_t localOverride = node.materialOverride;
    std::int32_t importedMaterial = -1;
    if (!node.meshPartIndices.empty()) {
        const std::uint32_t partIndex = node.meshPartIndices.front();
        if (partIndex < meshData_.parts.size() &&
            meshData_.parts[partIndex].materialIndex < meshData_.materials.size()) {
            importedMaterial = static_cast<std::int32_t>(
                meshData_.parts[partIndex].materialIndex
            );
        }
    }
    const std::int32_t effectiveOverride = localOverride >= 0
        ? localOverride
        : (inheritedOverride >= 0 ? inheritedOverride : importedMaterial);
    const bool hasEffectiveMaterial = effectiveOverride >= 0 &&
        static_cast<std::size_t>(effectiveOverride) < meshData_.materials.size();
    const char* preview = localOverride >= 0
        ? (hasEffectiveMaterial ? meshData_.materials[effectiveOverride].name.c_str() : "<Invalid material>")
        : (inheritedOverride >= 0
            ? "<Inherited material>"
            : (importedMaterial >= 0
                ? meshData_.materials[importedMaterial].name.c_str()
                : "<Use mesh material>"));

    if (ImGui::BeginCombo("Material", preview)) {
        const bool useMeshMaterial = node.materialOverride < 0;
        if (ImGui::Selectable("<Use mesh material>", useMeshMaterial)) {
            node.materialOverride = -1;
            shouldSaveSettings = true;
        }
        for (std::size_t materialIndex = 0; materialIndex < meshData_.materials.size(); ++materialIndex) {
            const bool selected = node.materialOverride == static_cast<std::int32_t>(materialIndex);
            const std::string label = meshData_.materials[materialIndex].name.empty()
                ? "Material " + std::to_string(materialIndex)
                : meshData_.materials[materialIndex].name;
            if (ImGui::Selectable(label.c_str(), selected)) {
                node.materialOverride = static_cast<std::int32_t>(materialIndex);
                shouldSaveSettings = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Create new Material")) {
        MaterialData material;
        material.name = "CustomMaterial_" + std::to_string(meshData_.materials.size());
        material.userCreated = true;
        meshData_.materials.push_back(material);
        node.materialOverride = static_cast<std::int32_t>(meshData_.materials.size() - 1);
        rebuildMaterialResources();
        shouldSaveSettings = true;
    }
    if (hasEffectiveMaterial) {
        ImGui::SameLine();
        if (ImGui::Button("Duplicate for Node")) {
            MaterialData material = meshData_.materials[effectiveOverride];
            material.name += "_Copy";
            material.userCreated = true;
            meshData_.materials.push_back(std::move(material));
            node.materialOverride = static_cast<std::int32_t>(meshData_.materials.size() - 1);
            rebuildMaterialResources();
            shouldSaveSettings = true;
        }
    }

    if (!hasEffectiveMaterial) {
        ImGui::TextDisabled("Select or create a material to edit its parameters.");
        return;
    }

    if (localOverride < 0 && inheritedOverride < 0 && node.meshPartIndices.size() > 1) {
        ImGui::TextDisabled(
            "This node has multiple imported material bindings; editing the first one."
        );
    }

    drawMaterialControls(
        meshData_.materials[effectiveOverride],
        effectiveOverride,
        shouldSaveSettings
    );
}

void Renderer::drawMaterialControls(
    MaterialData& material,
    std::int32_t materialKey,
    bool& shouldSaveSettings,
    const std::filesystem::path* modelResourcePath
) {
    const std::string editorContext = modelResourcePath == nullptr
        ? "<scene>"
        : normalizedPath(*modelResourcePath).string();
    if (materialEditorContext_ != editorContext) {
        materialEditorContext_ = editorContext;
        materialNameMaterialIndex_ = -1;
        materialTexturePathMaterialIndex_ = -1;
        materialNormalTexturePathMaterialIndex_ = -1;
        materialSpecularTexturePathMaterialIndex_ = -1;
    }
    const auto markMaterialChanged = [&]() {
        material.userModified = true;
        shouldSaveSettings = true;
        if (modelResourcePath != nullptr) {
            synchronizeModelResourceMaterial(*modelResourcePath, materialKey, material);
        }
    };
    ImGui::Text("Editing: %s", material.name.c_str());
    if (materialNameMaterialIndex_ != materialKey) {
        materialNameMaterialIndex_ = materialKey;
        materialNameBuffer_.fill('\0');
        const std::size_t copyCount = std::min(
            material.name.size(),
            materialNameBuffer_.size() - 1
        );
        std::copy_n(material.name.data(), copyCount, materialNameBuffer_.data());
    }
    if (ImGui::InputText("Name", materialNameBuffer_.data(), materialNameBuffer_.size())) {
        material.name = materialNameBuffer_.data();
        markMaterialChanged();
    }
    ImGui::ColorEdit4("Base color", &material.baseColor.x);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        markMaterialChanged();
    }
    ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        markMaterialChanged();
    }
    ImGui::SliderFloat("Roughness", &material.roughness, 0.04f, 1.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        markMaterialChanged();
    }
    ImGui::SliderFloat("Opacity", &material.opacity, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        markMaterialChanged();
    }
    ImGui::SeparatorText("Emission");
    ImGui::ColorEdit3("Emission color", &material.emissionColor.x);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        markMaterialChanged();
    }
    ImGui::SliderFloat("Emission strength", &material.emissionStrength, 0.0f, 20.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        markMaterialChanged();
    }

    const std::array<MaterialData::BlendMode, 4> blendModes = {
        MaterialData::BlendMode::Opaque,
        MaterialData::BlendMode::AlphaBlend,
        MaterialData::BlendMode::Additive,
        MaterialData::BlendMode::Multiply,
    };
    const char* blendPreview = materialBlendModeLabel(material.blendMode);
    if (ImGui::BeginCombo("Blend mode", blendPreview)) {
        for (const MaterialData::BlendMode blendMode : blendModes) {
            const bool selected = material.blendMode == blendMode;
            if (ImGui::Selectable(materialBlendModeLabel(blendMode), selected)) {
                material.blendMode = blendMode;
                markMaterialChanged();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Checkbox("Depth write", &material.depthWrite)) {
        markMaterialChanged();
    }
    if (ImGui::Checkbox("Double-sided", &material.doubleSided)) {
        markMaterialChanged();
    }

    const auto drawTextureCandidates = [&, modelResourcePath](
        const char* label,
        ResourceTextureSlot slot,
        std::array<char, 512>& buffer
    ) {
        if (modelResourcePath == nullptr) {
            return;
        }
        const auto cacheIt = modelResourceCache_.find(
            normalizedPath(*modelResourcePath).string()
        );
        if (cacheIt == modelResourceCache_.end() || cacheIt->second.texturePaths.empty()) {
            return;
        }

        std::filesystem::path currentPath;
        switch (slot) {
        case ResourceTextureSlot::BaseColor:
            currentPath = material.texturePath;
            break;
        case ResourceTextureSlot::Normal:
            currentPath = material.normalTexturePath;
            break;
        case ResourceTextureSlot::Specular:
            currentPath = material.specularTexturePath;
            break;
        }
        const std::string preview = currentPath.empty()
            ? "<Choose a texture>"
            : currentPath.filename().string();
        ImGui::PushID(label);
        if (ImGui::BeginCombo("Texture candidates", preview.c_str())) {
            for (const std::filesystem::path& candidate : cacheIt->second.texturePaths) {
                std::error_code relativeError;
                const std::filesystem::path relativePath = std::filesystem::relative(
                    candidate,
                    modelResourcePath->parent_path() / "tex",
                    relativeError
                );
                const std::string candidateLabel = (!relativeError && !relativePath.empty())
                    ? relativePath.generic_string()
                    : candidate.filename().string();
                const bool selected = pathsEqual(candidate, currentPath);
                if (ImGui::Selectable(candidateLabel.c_str(), selected)) {
                    buffer.fill('\0');
                    const std::string candidateString = candidate.string();
                    const std::size_t copyCount = std::min(
                        candidateString.size(),
                        buffer.size() - 1
                    );
                    std::copy_n(candidateString.data(), copyCount, buffer.data());
                    switch (slot) {
                    case ResourceTextureSlot::BaseColor:
                        material.texturePath = candidate;
                        material.embeddedTexture = EmbeddedTextureData{};
                        break;
                    case ResourceTextureSlot::Normal:
                        material.normalTexturePath = candidate;
                        material.embeddedNormalTexture = EmbeddedTextureData{};
                        break;
                    case ResourceTextureSlot::Specular:
                        material.specularTexturePath = candidate;
                        material.embeddedSpecularTexture = EmbeddedTextureData{};
                        break;
                    }
                    synchronizeModelResourceMaterial(*modelResourcePath, materialKey, material);
                    rebuildMaterialResources();
                    markMaterialChanged();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    };

    if (materialTexturePathMaterialIndex_ != materialKey) {
        materialTexturePathMaterialIndex_ = materialKey;
        materialTexturePathBuffer_.fill('\0');
        const std::string path = material.texturePath.string();
        const std::size_t copyCount = std::min(
            path.size(),
            materialTexturePathBuffer_.size() - 1
        );
        std::copy_n(path.data(), copyCount, materialTexturePathBuffer_.data());
    }
    ImGui::InputText(
        "Base color texture",
        materialTexturePathBuffer_.data(),
        materialTexturePathBuffer_.size()
    );
    drawTextureCandidates(
        "BaseColorCandidates",
        ResourceTextureSlot::BaseColor,
        materialTexturePathBuffer_
    );
    if (ImGui::Button("Load material texture")) {
        material.texturePath = materialTexturePathBuffer_.data();
        material.embeddedTexture = EmbeddedTextureData{};
        if (modelResourcePath != nullptr) {
            synchronizeModelResourceMaterial(*modelResourcePath, materialKey, material);
        }
        rebuildMaterialResources();
        markMaterialChanged();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear texture")) {
        material.texturePath.clear();
        material.embeddedTexture = EmbeddedTextureData{};
        materialTexturePathBuffer_.fill('\0');
        if (modelResourcePath != nullptr) {
            synchronizeModelResourceMaterial(*modelResourcePath, materialKey, material);
        }
        rebuildMaterialResources();
        markMaterialChanged();
    }

    const auto syncTexturePathBuffer = [](
        std::array<char, 512>& buffer,
        std::int32_t& bufferMaterialIndex,
        std::int32_t materialIndex,
        const std::filesystem::path& path
    ) {
        if (bufferMaterialIndex == materialIndex) {
            return;
        }
        bufferMaterialIndex = materialIndex;
        buffer.fill('\0');
        const std::string pathString = path.string();
        const std::size_t copyCount = std::min(pathString.size(), buffer.size() - 1);
        std::copy_n(pathString.data(), copyCount, buffer.data());
    };

    syncTexturePathBuffer(
        materialNormalTexturePathBuffer_,
        materialNormalTexturePathMaterialIndex_,
        materialKey,
        material.normalTexturePath
    );
    ImGui::InputText(
        "Normal texture",
        materialNormalTexturePathBuffer_.data(),
        materialNormalTexturePathBuffer_.size()
    );
    drawTextureCandidates(
        "NormalCandidates",
        ResourceTextureSlot::Normal,
        materialNormalTexturePathBuffer_
    );
    if (ImGui::Button("Load normal texture")) {
        material.normalTexturePath = materialNormalTexturePathBuffer_.data();
        material.embeddedNormalTexture = EmbeddedTextureData{};
        if (modelResourcePath != nullptr) {
            synchronizeModelResourceMaterial(*modelResourcePath, materialKey, material);
        }
        rebuildMaterialResources();
        markMaterialChanged();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear normal")) {
        material.normalTexturePath.clear();
        material.embeddedNormalTexture = EmbeddedTextureData{};
        materialNormalTexturePathBuffer_.fill('\0');
        if (modelResourcePath != nullptr) {
            synchronizeModelResourceMaterial(*modelResourcePath, materialKey, material);
        }
        rebuildMaterialResources();
        markMaterialChanged();
    }

    syncTexturePathBuffer(
        materialSpecularTexturePathBuffer_,
        materialSpecularTexturePathMaterialIndex_,
        materialKey,
        material.specularTexturePath
    );
    ImGui::InputText(
        "Specular texture",
        materialSpecularTexturePathBuffer_.data(),
        materialSpecularTexturePathBuffer_.size()
    );
    drawTextureCandidates(
        "SpecularCandidates",
        ResourceTextureSlot::Specular,
        materialSpecularTexturePathBuffer_
    );
    if (ImGui::Button("Load specular texture")) {
        material.specularTexturePath = materialSpecularTexturePathBuffer_.data();
        material.embeddedSpecularTexture = EmbeddedTextureData{};
        if (modelResourcePath != nullptr) {
            synchronizeModelResourceMaterial(*modelResourcePath, materialKey, material);
        }
        rebuildMaterialResources();
        markMaterialChanged();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear specular")) {
        material.specularTexturePath.clear();
        material.embeddedSpecularTexture = EmbeddedTextureData{};
        materialSpecularTexturePathBuffer_.fill('\0');
        if (modelResourcePath != nullptr) {
            synchronizeModelResourceMaterial(*modelResourcePath, materialKey, material);
        }
        rebuildMaterialResources();
        markMaterialChanged();
    }

    ImGui::SeparatorText("Engine Compatibility");
    ImGui::TextDisabled(
        "VulkCore slots: Base Color / Normal / Specular; default shaders use the engine ABI."
    );
    if (ImGui::Button("Apply engine-safe defaults")) {
        material.metallic = std::clamp(material.metallic, 0.0f, 1.0f);
        material.roughness = std::clamp(material.roughness, 0.04f, 1.0f);
        material.opacity = std::clamp(material.opacity, 0.0f, 1.0f);
        material.blendMode = MaterialData::BlendMode::Opaque;
        material.depthWrite = true;
        material.doubleSided = false;
        material.shaderConfiguration = ShaderConfiguration{};
        markMaterialChanged();
    }

    drawMaterialShaderControls(material, shouldSaveSettings);
    if (modelResourcePath != nullptr) {
        synchronizeModelResourceMaterial(*modelResourcePath, materialKey, material);
    }
}

void Renderer::drawMaterialShaderControls(MaterialData& material, bool& shouldSaveSettings) {
    const auto markMaterialChanged = [&]() {
        material.userModified = true;
        shouldSaveSettings = true;
    };

    ImGui::SeparatorText("Material Shaders");
    ImGui::TextDisabled("Empty stages fall back to the default shader.");

    const std::array<ShaderStage, kShaderStageCount> stages = {
        ShaderStage::Vertex,
        ShaderStage::TessellationControl,
        ShaderStage::TessellationEvaluation,
        ShaderStage::Geometry,
        ShaderStage::Fragment,
        ShaderStage::Compute
    };
    for (const ShaderStage stage : stages) {
        std::string preview = material.shaderConfiguration.shader(stage).empty()
            ? "<Default>"
            : material.shaderConfiguration.shader(stage);
        ImGui::PushID(static_cast<int>(stage));
        if (ImGui::BeginCombo(shaderStageLabel(stage), preview.c_str())) {
            const bool defaultSelected = material.shaderConfiguration.shader(stage).empty();
            if (ImGui::Selectable("<Default>", defaultSelected)) {
                material.shaderConfiguration.shader(stage).clear();
                markMaterialChanged();
            }
            if (defaultSelected) {
                ImGui::SetItemDefaultFocus();
            }

            const auto& shaders = availableShaders_[static_cast<std::size_t>(stage)];
            for (const std::string& shaderName : shaders) {
                const bool selected = material.shaderConfiguration.shader(stage) == shaderName;
                if (ImGui::Selectable(shaderName.c_str(), selected)) {
                    material.shaderConfiguration.shader(stage) = shaderName;
                    markMaterialChanged();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }

    if (ImGui::Button("Apply water shader")) {
        material.shaderConfiguration = ShaderConfiguration{};
        material.shaderConfiguration.shader(ShaderStage::Vertex) = "shader_water.vert";
        material.shaderConfiguration.shader(ShaderStage::Fragment) = "shader_water.frag";
        markMaterialChanged();
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply flowing water")) {
        material.shaderConfiguration = ShaderConfiguration{};
        material.shaderConfiguration.shader(ShaderStage::Vertex) = "shader_water.vert";
        material.shaderConfiguration.shader(ShaderStage::Fragment) = "shader_water_flow.frag";
        markMaterialChanged();
    }

    if (isWaterShaderConfiguration(material.shaderConfiguration)) {
        ImGui::SeparatorText("Water Flow");
        ImGui::TextDisabled("Direction is in the mesh UV space: +X right, +Y up.");
        ImGui::DragFloat2(
            "Flow direction",
            &material.waterShaderSettings.flowDirection.x,
            0.01f,
            -1.0f,
            1.0f,
            "%.2f"
        );
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (glm::length(material.waterShaderSettings.flowDirection) < 0.001f) {
                material.waterShaderSettings.flowDirection = glm::vec2(0.0f, 1.0f);
            }
            markMaterialChanged();
        }
        ImGui::SliderFloat("Flow speed", &material.waterShaderSettings.flowSpeed, 0.0f, 4.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) markMaterialChanged();
        ImGui::SliderFloat("Flow strength", &material.waterShaderSettings.flowStrength, 0.0f, 4.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) markMaterialChanged();
        ImGui::SliderFloat("Flow turbulence", &material.waterShaderSettings.flowTurbulence, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) markMaterialChanged();
        ImGui::SliderFloat("Wave scale", &material.waterShaderSettings.waveScale, 0.1f, 8.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) markMaterialChanged();
        ImGui::SliderFloat("Wave height", &material.waterShaderSettings.waveHeight, 0.0f, 0.2f, "%.3f");
        if (ImGui::IsItemDeactivatedAfterEdit()) markMaterialChanged();
        if (ImGui::Button("Reset water parameters")) {
            material.waterShaderSettings = WaterShaderSettings{};
            markMaterialChanged();
        }
    }

    if (material.shaderConfiguration.hasComputeOverride()) {
        if (computePipelineForMaterial(material) != VK_NULL_HANDLE) {
            ImGui::TextDisabled("Compute pipeline ready (not dispatched by the mesh pass).");
        } else {
            ImGui::TextColored(
                ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                "Compute shader pipeline could not be created."
            );
        }
    }
    if (ImGui::Button("Reset material shaders")) {
        material.shaderConfiguration = ShaderConfiguration{};
        markMaterialChanged();
    }
}


} // namespace VulkCore

