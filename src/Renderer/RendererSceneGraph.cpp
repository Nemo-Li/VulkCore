#include "RendererImplementation.hpp"

namespace VulkCore {

namespace {

double animationTicksForTime(
    double timeTicks,
    const std::vector<AnimationVectorKey>& keys,
    AnimationBehavior behavior
) {
    if (keys.size() < 2 || (timeTicks >= keys.front().timeTicks &&
        timeTicks <= keys.back().timeTicks)) {
        return timeTicks;
    }
    const double first = keys.front().timeTicks;
    const double last = keys.back().timeTicks;
    const double span = last - first;
    if (behavior == AnimationBehavior::Repeat && span > std::numeric_limits<double>::epsilon()) {
        double wrapped = std::fmod(timeTicks - first, span);
        if (wrapped < 0.0) {
            wrapped += span;
        }
        return first + wrapped;
    }
    return std::clamp(timeTicks, first, last);
}

glm::vec3 sampleVectorKeys(
    const std::vector<AnimationVectorKey>& keys,
    double timeTicks,
    AnimationBehavior preBehavior,
    AnimationBehavior postBehavior,
    const glm::vec3& fallback
) {
    if (keys.empty()) {
        return fallback;
    }
    const AnimationBehavior behavior = timeTicks < keys.front().timeTicks
        ? preBehavior
        : postBehavior;
    const double sampleTime = animationTicksForTime(timeTicks, keys, behavior);
    if (sampleTime <= keys.front().timeTicks) {
        return keys.front().value;
    }
    if (sampleTime >= keys.back().timeTicks) {
        return keys.back().value;
    }
    const auto upperIt = std::upper_bound(
        keys.begin(),
        keys.end(),
        sampleTime,
        [](double value, const AnimationVectorKey& key) {
            return value < key.timeTicks;
        }
    );
    const std::size_t upperIndex = static_cast<std::size_t>(upperIt - keys.begin());
    if (upperIndex == 0 || upperIndex >= keys.size()) {
        return keys.back().value;
    }
    const AnimationVectorKey& left = keys[upperIndex - 1];
    const AnimationVectorKey& right = keys[upperIndex];
    const double span = right.timeTicks - left.timeTicks;
    const float factor = span <= std::numeric_limits<double>::epsilon()
        ? 0.0f
        : static_cast<float>((sampleTime - left.timeTicks) / span);
    return glm::mix(left.value, right.value, factor);
}

double animationTicksForTime(
    double timeTicks,
    const std::vector<AnimationRotationKey>& keys,
    AnimationBehavior behavior
) {
    if (keys.size() < 2 || (timeTicks >= keys.front().timeTicks &&
        timeTicks <= keys.back().timeTicks)) {
        return timeTicks;
    }
    const double first = keys.front().timeTicks;
    const double last = keys.back().timeTicks;
    const double span = last - first;
    if (behavior == AnimationBehavior::Repeat && span > std::numeric_limits<double>::epsilon()) {
        double wrapped = std::fmod(timeTicks - first, span);
        if (wrapped < 0.0) {
            wrapped += span;
        }
        return first + wrapped;
    }
    return std::clamp(timeTicks, first, last);
}

glm::quat sampleRotationKeys(
    const std::vector<AnimationRotationKey>& keys,
    double timeTicks,
    AnimationBehavior preBehavior,
    AnimationBehavior postBehavior,
    const glm::quat& fallback
) {
    if (keys.empty()) {
        return fallback;
    }
    const AnimationBehavior behavior = timeTicks < keys.front().timeTicks
        ? preBehavior
        : postBehavior;
    const double sampleTime = animationTicksForTime(timeTicks, keys, behavior);
    if (sampleTime <= keys.front().timeTicks) {
        return glm::normalize(glm::quat(
            keys.front().value.w,
            keys.front().value.x,
            keys.front().value.y,
            keys.front().value.z
        ));
    }
    if (sampleTime >= keys.back().timeTicks) {
        return glm::normalize(glm::quat(
            keys.back().value.w,
            keys.back().value.x,
            keys.back().value.y,
            keys.back().value.z
        ));
    }
    const auto upperIt = std::upper_bound(
        keys.begin(),
        keys.end(),
        sampleTime,
        [](double value, const AnimationRotationKey& key) {
            return value < key.timeTicks;
        }
    );
    const std::size_t upperIndex = static_cast<std::size_t>(upperIt - keys.begin());
    if (upperIndex == 0 || upperIndex >= keys.size()) {
        return fallback;
    }
    const AnimationRotationKey& left = keys[upperIndex - 1];
    const AnimationRotationKey& right = keys[upperIndex];
    const double span = right.timeTicks - left.timeTicks;
    const float factor = span <= std::numeric_limits<double>::epsilon()
        ? 0.0f
        : static_cast<float>((sampleTime - left.timeTicks) / span);
    const glm::quat leftRotation = glm::normalize(glm::quat(
        left.value.w, left.value.x, left.value.y, left.value.z
    ));
    glm::quat rightRotation = glm::normalize(glm::quat(
        right.value.w, right.value.x, right.value.y, right.value.z
    ));
    // Assimp/FBX may encode the same orientation as q and -q. Keep every
    // interpolation on the short arc explicitly instead of relying on the
    // math library to repair the sign at this call site.
    if (glm::dot(leftRotation, rightRotation) < 0.0f) {
        rightRotation = -rightRotation;
    }
    return glm::normalize(glm::slerp(leftRotation, rightRotation, factor));
}

glm::mat4 animatedLocalTransform(
    const ModelNode& node,
    const SkeletalAnimationChannel* channel,
    double timeTicks
) {
    if (channel == nullptr) {
        return node.localTransform;
    }

    glm::vec3 baseScale(1.0f);
    glm::vec3 baseTranslation(0.0f);
    glm::vec3 skew(0.0f);
    glm::vec4 perspective(0.0f);
    glm::quat baseRotation(1.0f, 0.0f, 0.0f, 0.0f);
    glm::decompose(
        node.localTransform,
        baseScale,
        baseRotation,
        baseTranslation,
        skew,
        perspective
    );

    const glm::vec3 translation = sampleVectorKeys(
        channel->positionKeys,
        timeTicks,
        channel->preBehavior,
        channel->postBehavior,
        baseTranslation
    );
    const glm::vec3 scale = sampleVectorKeys(
        channel->scalingKeys,
        timeTicks,
        channel->preBehavior,
        channel->postBehavior,
        baseScale
    );
    const glm::quat rotation = sampleRotationKeys(
        channel->rotationKeys,
        timeTicks,
        channel->preBehavior,
        channel->postBehavior,
        baseRotation
    );
    return glm::translate(glm::mat4(1.0f), translation) *
        glm::mat4_cast(rotation) *
        glm::scale(glm::mat4(1.0f), scale);
}

} // namespace

void Renderer::selectModelNode(ModelNode& node) {
    selectedModelNode_ = &node;
    selectedResourceType_ = ResourceSelectionType::Model;
    selectedResourceMaterialIndex_ = -1;
    updateBoundingBoxVertices();
}

void Renderer::drawSkeletalAnimationControls(
    ModelNode& node,
    bool& shouldSaveSettings
) {
    (void)shouldSaveSettings;
    SkeletalAnimationInstance* instance = nullptr;
    for (SkeletalAnimationInstance& candidate : meshData_.skeletalAnimationInstances) {
        if (node.nodeId >= candidate.rootNodeId &&
            node.nodeId - candidate.rootNodeId < kSceneModelNodeStride) {
            instance = &candidate;
            break;
        }
    }
    if (instance == nullptr || instance->animationCount == 0) {
        return;
    }

    ImGui::SeparatorText("Skeletal Animation");
    ImGui::TextWrapped(
        "CPU skinning is active for this imported model. Changes apply to the whole model instance."
    );
    ImGui::Checkbox("Animation Enabled", &instance->enabled);
    ImGui::Checkbox("Loop", &instance->looping);
    ImGui::DragFloat(
        "Playback Speed",
        &instance->playbackSpeed,
        0.01f,
        -4.0f,
        4.0f,
        "%.2fx"
    );
    if (instance->playbackSpeed == 0.0f) {
        instance->playbackSpeed = 1.0f;
    }

    const std::size_t animationStart = instance->animationStart;
    const std::size_t animationEnd = animationStart + instance->animationCount;
    if (animationStart >= meshData_.skeletalAnimations.size() ||
        animationEnd > meshData_.skeletalAnimations.size()) {
        ImGui::TextUnformatted("Animation data is incomplete for this instance.");
        return;
    }
    instance->activeClip = std::min<std::uint32_t>(
        instance->activeClip,
        instance->animationCount - 1
    );
    const SkeletalAnimationClip& activeClip = meshData_.skeletalAnimations[
        animationStart + instance->activeClip
    ];
    const char* activeClipName = activeClip.name.empty()
        ? "Unnamed clip"
        : activeClip.name.c_str();
    if (ImGui::BeginCombo("Clip", activeClipName)) {
        for (std::uint32_t clipIndex = 0;
             clipIndex < instance->animationCount;
             ++clipIndex) {
            const SkeletalAnimationClip& clip = meshData_.skeletalAnimations[
                animationStart + clipIndex
            ];
            const bool selected = clipIndex == instance->activeClip;
            const std::string label = clip.name.empty()
                ? "Animation " + std::to_string(clipIndex)
                : clip.name;
            if (ImGui::Selectable(label.c_str(), selected)) {
                instance->activeClip = clipIndex;
                instance->timeSeconds = 0.0f;
                instance->playing = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (instance->playing) {
        if (ImGui::Button("Pause")) {
            instance->playing = false;
        }
    } else if (ImGui::Button("Play")) {
        instance->playing = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        instance->timeSeconds = 0.0f;
        instance->playing = true;
    }

    const double ticksPerSecond = activeClip.ticksPerSecond > 0.0
        ? activeClip.ticksPerSecond
        : 25.0;
    const float durationSeconds = activeClip.durationTicks > 0.0
        ? static_cast<float>(activeClip.durationTicks / ticksPerSecond)
        : 0.0f;
    if (durationSeconds > 0.0f) {
        instance->timeSeconds = std::clamp(instance->timeSeconds, 0.0f, durationSeconds);
        ImGui::SliderFloat(
            "Time",
            &instance->timeSeconds,
            0.0f,
            durationSeconds,
            "%.3f s"
        );
        ImGui::Text(
            "Clip %.3f s / %zu channels",
            durationSeconds,
            activeClip.channels.size()
        );
    }
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

void Renderer::updateSkeletalAnimations(float deltaSeconds) {
    if (meshData_.skeletalAnimationInstances.empty() ||
        meshData_.bindPoseVertices.size() != meshData_.vertices.size() ||
        meshData_.skinSourceVertices.size() != meshData_.vertices.size()) {
        return;
    }

    meshData_.vertices = meshData_.bindPoseVertices;
    bool hasAnimatedInstance = false;
    for (SkeletalAnimationInstance& instance : meshData_.skeletalAnimationInstances) {
        if (instance.animationCount == 0 || instance.boneCount == 0 || !instance.enabled) {
            continue;
        }
        const std::size_t animationStart = instance.animationStart;
        const std::size_t animationEnd = animationStart + instance.animationCount;
        if (animationStart >= meshData_.skeletalAnimations.size() ||
            animationEnd > meshData_.skeletalAnimations.size()) {
            continue;
        }
        const std::size_t clipIndex = std::min<std::size_t>(
            instance.activeClip,
            instance.animationCount - 1
        );
        const SkeletalAnimationClip& clip = meshData_.skeletalAnimations[
            animationStart + clipIndex
        ];
        const double ticksPerSecond = clip.ticksPerSecond > 0.0
            ? clip.ticksPerSecond
            : 25.0;
        const double durationSeconds = clip.durationTicks > 0.0
            ? clip.durationTicks / ticksPerSecond
            : 0.0;
        if (instance.playing) {
            instance.timeSeconds += deltaSeconds * instance.playbackSpeed;
        }
        if (durationSeconds > 0.0) {
            if (instance.looping) {
                double wrappedTime = std::fmod(
                    static_cast<double>(instance.timeSeconds),
                    durationSeconds
                );
                if (wrappedTime < 0.0) {
                    wrappedTime += durationSeconds;
                }
                instance.timeSeconds = static_cast<float>(wrappedTime);
            } else {
                instance.timeSeconds = std::clamp(
                    instance.timeSeconds,
                    0.0f,
                    static_cast<float>(durationSeconds)
                );
            }
        } else {
            instance.timeSeconds = 0.0f;
        }
        const double timeTicks = static_cast<double>(instance.timeSeconds) * ticksPerSecond;

        const ModelNode* root = findModelNodeById(meshData_.hierarchy, instance.rootNodeId);
        if (root == nullptr) {
            continue;
        }
        std::unordered_map<std::uint32_t, const SkeletalAnimationChannel*> channels;
        channels.reserve(clip.channels.size());
        for (const SkeletalAnimationChannel& channel : clip.channels) {
            if (channel.nodeId != std::numeric_limits<std::uint32_t>::max()) {
                channels[channel.nodeId] = &channel;
            }
        }
        std::unordered_map<std::uint32_t, glm::mat4> animatedGlobals;
        const auto evaluateNode = [&channels, timeTicks, &animatedGlobals](
                                       const auto& self,
                                       const ModelNode& node,
                                       const glm::mat4& parentTransform
                                   ) -> void {
            const auto channelIt = channels.find(node.nodeId);
            const SkeletalAnimationChannel* channel = channelIt == channels.end()
                ? nullptr
                : channelIt->second;
            const glm::mat4 globalTransform = parentTransform * animatedLocalTransform(
                node,
                channel,
                timeTicks
            );
            animatedGlobals[node.nodeId] = globalTransform;
            for (const ModelNode& child : node.children) {
                self(self, child, globalTransform);
            }
        };
        evaluateNode(evaluateNode, *root, glm::mat4(1.0f));

        std::unordered_map<std::uint32_t, glm::mat4> boneMatrices;
        const std::size_t boneStart = instance.boneStart;
        const std::size_t boneEnd = boneStart + instance.boneCount;
        for (std::size_t boneIndex = boneStart; boneIndex < boneEnd; ++boneIndex) {
            if (boneIndex >= meshData_.skeletonBones.size()) {
                break;
            }
            const SkeletonBone& bone = meshData_.skeletonBones[boneIndex];
            const auto globalIt = animatedGlobals.find(bone.nodeId);
            const glm::mat4 animatedGlobal = globalIt == animatedGlobals.end()
                ? bone.bindPoseGlobalTransform
                : globalIt->second;
            boneMatrices.emplace(
                static_cast<std::uint32_t>(boneIndex),
                instance.globalInverseTransform * animatedGlobal * bone.offsetMatrix
            );
        }

        for (Vertex& vertex : meshData_.vertices) {
            glm::vec4 skinnedPosition(0.0f);
            glm::vec3 skinnedNormal(0.0f);
            float totalWeight = 0.0f;
            for (int influenceIndex = 0; influenceIndex < 4; ++influenceIndex) {
                const float weight = vertex.boneWeights[influenceIndex];
                const std::uint32_t boneIndex = static_cast<std::uint32_t>(
                    std::max(vertex.boneIndices[influenceIndex], 0)
                );
                if (weight <= 0.0f || boneIndex < boneStart || boneIndex >= boneEnd) {
                    continue;
                }
                const auto matrixIt = boneMatrices.find(boneIndex);
                if (matrixIt == boneMatrices.end()) {
                    continue;
                }
                const glm::mat4& boneMatrix = matrixIt->second;
                const std::size_t vertexIndex = static_cast<std::size_t>(
                    &vertex - meshData_.vertices.data()
                );
                skinnedPosition += boneMatrix * glm::vec4(
                    meshData_.skinSourceVertices[vertexIndex].position,
                    1.0f
                ) * weight;
                const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(boneMatrix)));
                skinnedNormal += normalMatrix * meshData_.skinSourceVertices[vertexIndex].normal * weight;
                totalWeight += weight;
            }
            if (totalWeight > std::numeric_limits<float>::epsilon()) {
                vertex.position = glm::vec3(skinnedPosition) / totalWeight;
                vertex.normal = glm::normalize(skinnedNormal / totalWeight);
                hasAnimatedInstance = true;
            }
        }
    }

    if ((!meshData_.skeletalAnimationInstances.empty() || hasAnimatedInstance) &&
        currentFrame_ < vertexBuffers_.size()) {
        vertexBuffers_[currentFrame_]->upload(
            meshData_.vertices.data(),
            sizeof(Vertex) * meshData_.vertices.size()
        );
    }
}

void Renderer::updateSceneNodeTransforms(float elapsedSeconds, float deltaSeconds) {
    updateSkeletalAnimations(deltaSeconds);
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
    ImGui::TextWrapped("A material override applies to this node and its children.");

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
        ImGui::TextWrapped("Select or create a material to edit its parameters.");
        return;
    }

    if (localOverride < 0 && inheritedOverride < 0 && node.meshPartIndices.size() > 1) {
        ImGui::TextWrapped(
            "This node has multiple imported material bindings; editing the first one."
        );
    }

    const SceneModelResource* modelResource = sceneModelResourceForNode(node);
    const std::filesystem::path* modelResourcePath = nullptr;
    std::int32_t materialKey = effectiveOverride;
    if (modelResource != nullptr) {
        const std::size_t sceneMaterialIndex = static_cast<std::size_t>(effectiveOverride);
        const std::size_t materialStart = modelResource->materialStart;
        if (sceneMaterialIndex >= materialStart &&
            sceneMaterialIndex - materialStart < modelResource->materialCount) {
            materialKey = static_cast<std::int32_t>(sceneMaterialIndex - materialStart);
            modelResourcePath = &modelResource->path;
        }
    }

    drawMaterialControls(
        meshData_.materials[effectiveOverride],
        materialKey,
        shouldSaveSettings,
        modelResourcePath
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
    ImGui::TextWrapped("Editing: %s", material.name.c_str());
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
    ImGui::TextWrapped(
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
    ImGui::TextWrapped("Empty stages fall back to the default shader.");

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
        ImGui::TextWrapped("Direction is in the mesh UV space: +X right, +Y up.");
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
            ImGui::TextWrapped("Compute pipeline ready (not dispatched by the mesh pass).");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.25f, 1.0f));
            ImGui::TextWrapped(
                "Compute shader pipeline could not be created."
            );
            ImGui::PopStyleColor();
        }
    }
    if (ImGui::Button("Reset material shaders")) {
        material.shaderConfiguration = ShaderConfiguration{};
        markMaterialChanged();
    }
}


} // namespace VulkCore
