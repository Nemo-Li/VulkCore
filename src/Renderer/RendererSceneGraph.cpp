#include "RendererImplementation.hpp"

#include <cctype>

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

std::string normalizedAnimationName(const std::string& name) {
    std::string normalized;
    normalized.reserve(name.size());
    for (const unsigned char character : name) {
        if (std::isalnum(character)) {
            normalized.push_back(static_cast<char>(std::tolower(character)));
        }
    }
    return normalized;
}

int playerRunClipScore(const SkeletalAnimationClip& clip) {
    const std::string name = normalizedAnimationName(clip.name);
    if (name == "yasuorun1anm") {
        return 1000;
    }
    if (name.find("run1") != std::string::npos) {
        return 800;
    }
    if (name == "run" || name == "runanm") {
        return 600;
    }
    if (name.find("run") != std::string::npos) {
        return 400;
    }
    return -1;
}

int playerAttackClipScore(
    const SkeletalAnimationClip& clip,
    PlayerAttack attack
) {
    const std::string name = normalizedAnimationName(clip.name);
    switch (attack) {
    case PlayerAttack::Tornado:
        if (name == "attackfirst") return 1000;
        if (name == "attack1") return 900;
        break;
    case PlayerAttack::LightningSlash:
        if (name == "attack2") return 1000;
        if (name == "attack3") return 800;
        break;
    case PlayerAttack::FlameNova:
        if (name == "attack4") return 1000;
        if (name == "attack3") return 900;
        break;
    case PlayerAttack::None:
        return -1;
    }
    if (name.find("attack") != std::string::npos) {
        return 100;
    }
    return -1;
}

int footBoneScore(const std::string& sourceName, bool leftFoot) {
    const std::string name = normalizedAnimationName(sourceName);
    const std::string side = leftFoot ? "l" : "r";
    const std::string longSide = leftFoot ? "left" : "right";
    if (name == side + "foot" || name == longSide + "foot") {
        return 1000;
    }
    if (name.find(longSide + "foot") != std::string::npos) {
        return 900;
    }
    if (name.starts_with(side) && name.find("foot") != std::string::npos) {
        return name.find("buff") == std::string::npos ? 800 : 300;
    }
    if (name.ends_with("foot" + side)) {
        return 700;
    }
    return -1;
}

float smoothFootContact(float height, float minimum, float maximum) {
    const float heightRange = maximum - minimum;
    if (heightRange <= std::numeric_limits<float>::epsilon()) {
        return 1.0f;
    }
    const float normalizedHeight = (height - minimum) / heightRange;
    const float transition = std::clamp(
        (normalizedHeight - 0.05f) / 0.3f,
        0.0f,
        1.0f
    );
    const float smoothTransition = transition * transition *
        (3.0f - 2.0f * transition);
    return 1.0f - smoothTransition;
}

struct EstimatedPlayerRunGait {
    float cycleDurationSeconds = 0.0f;
    float groundSpeed = 0.0f;
    std::vector<float> footTractionSamples;
};

EstimatedPlayerRunGait estimatePlayerRunGait(
    const MeshData& mesh,
    const ModelNode& root,
    const SkeletalAnimationInstance& instance,
    const SkeletalAnimationClip& clip,
    float modelAutoScale
) {
    EstimatedPlayerRunGait gait;
    const double ticksPerSecond = clip.ticksPerSecond > 0.0
        ? clip.ticksPerSecond
        : 25.0;
    gait.cycleDurationSeconds = clip.durationTicks > 0.0
        ? static_cast<float>(clip.durationTicks / ticksPerSecond)
        : 0.0f;
    if (!std::isfinite(gait.cycleDurationSeconds) ||
        gait.cycleDurationSeconds <= 0.0f) {
        return gait;
    }

    std::array<std::uint32_t, 2> footNodeIds{
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint32_t>::max()
    };
    std::array<int, 2> footScores{-1, -1};
    const std::size_t boneStart = instance.boneStart;
    const std::size_t boneEnd = std::min<std::size_t>(
        mesh.skeletonBones.size(),
        boneStart + instance.boneCount
    );
    for (std::size_t boneIndex = boneStart; boneIndex < boneEnd; ++boneIndex) {
        const SkeletonBone& bone = mesh.skeletonBones[boneIndex];
        if (bone.nodeId == std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }
        for (std::size_t sideIndex = 0; sideIndex < footNodeIds.size(); ++sideIndex) {
            const int score = footBoneScore(bone.name, sideIndex == 0);
            if (score > footScores[sideIndex]) {
                footScores[sideIndex] = score;
                footNodeIds[sideIndex] = bone.nodeId;
            }
        }
    }

    constexpr std::size_t kGaitSampleCount = 90;
    std::array<std::vector<glm::vec3>, 2> footTracks;
    for (std::vector<glm::vec3>& track : footTracks) {
        track.reserve(kGaitSampleCount);
    }
    std::unordered_map<std::uint32_t, const SkeletalAnimationChannel*> channels;
    channels.reserve(clip.channels.size());
    for (const SkeletalAnimationChannel& channel : clip.channels) {
        if (channel.nodeId != std::numeric_limits<std::uint32_t>::max()) {
            channels[channel.nodeId] = &channel;
        }
    }
    for (std::size_t sampleIndex = 0;
         sampleIndex < kGaitSampleCount;
         ++sampleIndex) {
        const double timeTicks = clip.durationTicks *
            static_cast<double>(sampleIndex) /
            static_cast<double>(kGaitSampleCount);
        std::unordered_map<std::uint32_t, glm::mat4> animatedGlobals;
        animatedGlobals.reserve(channels.size());
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
        evaluateNode(evaluateNode, root, glm::mat4(1.0f));
        for (std::size_t sideIndex = 0; sideIndex < footNodeIds.size(); ++sideIndex) {
            const auto footIt = animatedGlobals.find(footNodeIds[sideIndex]);
            if (footIt != animatedGlobals.end()) {
                footTracks[sideIndex].push_back(glm::vec3(
                    footIt->second[3]
                ));
            }
        }
    }

    std::vector<float> stanceSpeeds;
    std::array<float, 2> minimumFootHeights{0.0f, 0.0f};
    std::array<float, 2> maximumFootHeights{0.0f, 0.0f};
    for (std::size_t sideIndex = 0; sideIndex < footTracks.size(); ++sideIndex) {
        const std::vector<glm::vec3>& track = footTracks[sideIndex];
        if (track.size() != kGaitSampleCount) {
            continue;
        }
        const auto [minimumIt, maximumIt] = std::minmax_element(
            track.begin(),
            track.end(),
            [](const glm::vec3& left, const glm::vec3& right) {
                return left.y < right.y;
            }
        );
        minimumFootHeights[sideIndex] = minimumIt->y;
        maximumFootHeights[sideIndex] = maximumIt->y;
        const float contactHeight = glm::mix(
            minimumIt->y,
            maximumIt->y,
            0.2f
        );
        const float sampleDuration = gait.cycleDurationSeconds /
            static_cast<float>(track.size());
        for (std::size_t sampleIndex = 0; sampleIndex < track.size(); ++sampleIndex) {
            if (track[sampleIndex].y > contactHeight) {
                continue;
            }
            const std::size_t previousIndex =
                (sampleIndex + track.size() - 1) % track.size();
            const std::size_t nextIndex = (sampleIndex + 1) % track.size();
            // Player models use local +Z as forward. A planted foot moves
            // backward relative to an in-place body, revealing ground speed.
            const float groundSpeed = -(
                track[nextIndex].z - track[previousIndex].z
            ) * modelAutoScale / (2.0f * sampleDuration);
            if (std::isfinite(groundSpeed) && groundSpeed > 0.0f) {
                stanceSpeeds.push_back(groundSpeed);
            }
        }
    }

    if (!stanceSpeeds.empty()) {
        std::sort(stanceSpeeds.begin(), stanceSpeeds.end());
        const std::size_t middle = stanceSpeeds.size() / 2;
        gait.groundSpeed = stanceSpeeds.size() % 2 == 0
            ? 0.5f * (stanceSpeeds[middle - 1] + stanceSpeeds[middle])
            : stanceSpeeds[middle];
    }
    const float playerHeight = PlayerController::kBotwLinkHeight;
    if (!std::isfinite(gait.groundSpeed) || gait.groundSpeed <= 0.0f) {
        constexpr float kFallbackStrideHeightRatio = 1.3f;
        gait.groundSpeed = playerHeight * kFallbackStrideHeightRatio /
            gait.cycleDurationSeconds;
    }
    gait.groundSpeed = std::clamp(
        gait.groundSpeed,
        playerHeight * 0.5f,
        playerHeight * 4.0f
    );

    const bool hasLeftTrack = footTracks[0].size() == kGaitSampleCount;
    const bool hasRightTrack = footTracks[1].size() == kGaitSampleCount;
    if (hasLeftTrack || hasRightTrack) {
        gait.footTractionSamples.reserve(kGaitSampleCount);
        for (std::size_t sampleIndex = 0;
             sampleIndex < kGaitSampleCount;
             ++sampleIndex) {
            float traction = 0.0f;
            for (std::size_t sideIndex = 0; sideIndex < footTracks.size(); ++sideIndex) {
                if (footTracks[sideIndex].size() != kGaitSampleCount) {
                    continue;
                }
                traction = std::max(
                    traction,
                    smoothFootContact(
                        footTracks[sideIndex][sampleIndex].y,
                        minimumFootHeights[sideIndex],
                        maximumFootHeights[sideIndex]
                    )
                );
            }
            gait.footTractionSamples.push_back(std::clamp(traction, 0.2f, 1.0f));
        }
    } else {
        gait.footTractionSamples.assign(1, 1.0f);
    }
    return gait;
}

} // namespace

void Renderer::configurePlayerRunAnimation() {
    playerRunAnimationClipIndex_.reset();
    playerAttackAnimationClipIndex_.reset();
    playerLightningAnimationClipIndex_.reset();
    playerFlameAnimationClipIndex_.reset();
    SkeletalAnimationInstance* playerInstance = nullptr;
    for (SkeletalAnimationInstance& instance : meshData_.skeletalAnimationInstances) {
        if (instance.rootNodeId == kPlayerSceneNodeId) {
            playerInstance = &instance;
            break;
        }
    }
    if (playerInstance == nullptr) {
        return;
    }

    // A Player never starts an imported animation automatically. Keyboard
    // movement below is the sole owner of its Run clip playback state.
    playerInstance->enabled = false;
    playerInstance->playing = false;
    playerInstance->looping = true;
    playerInstance->timeSeconds = 0.0f;
    playerInstance->bindPoseApplied = true;
    playerInstance->bindPoseUploadFramesRemaining = 0;

    const std::size_t animationStart = playerInstance->animationStart;
    const std::size_t animationEnd = animationStart + playerInstance->animationCount;
    if (playerInstance->animationCount == 0 ||
        animationStart >= meshData_.skeletalAnimations.size() ||
        animationEnd > meshData_.skeletalAnimations.size()) {
        return;
    }
    int bestScore = -1;
    int bestAttackScore = -1;
    int bestLightningScore = -1;
    int bestFlameScore = -1;
    std::uint32_t bestClipIndex = 0;
    std::uint32_t bestAttackClipIndex = 0;
    std::uint32_t bestLightningClipIndex = 0;
    std::uint32_t bestFlameClipIndex = 0;
    for (std::uint32_t clipIndex = 0;
         clipIndex < playerInstance->animationCount;
         ++clipIndex) {
        const int score = playerRunClipScore(
            meshData_.skeletalAnimations[animationStart + clipIndex]
        );
        if (score > bestScore) {
            bestScore = score;
            bestClipIndex = clipIndex;
        }
        const int attackScore = playerAttackClipScore(
            meshData_.skeletalAnimations[animationStart + clipIndex],
            PlayerAttack::Tornado
        );
        if (attackScore > bestAttackScore) {
            bestAttackScore = attackScore;
            bestAttackClipIndex = clipIndex;
        }
        const int lightningScore = playerAttackClipScore(
            meshData_.skeletalAnimations[animationStart + clipIndex],
            PlayerAttack::LightningSlash
        );
        if (lightningScore > bestLightningScore) {
            bestLightningScore = lightningScore;
            bestLightningClipIndex = clipIndex;
        }
        const int flameScore = playerAttackClipScore(
            meshData_.skeletalAnimations[animationStart + clipIndex],
            PlayerAttack::FlameNova
        );
        if (flameScore > bestFlameScore) {
            bestFlameScore = flameScore;
            bestFlameClipIndex = clipIndex;
        }
    }
    if (bestScore < 0) {
        return;
    }

    const ModelNode* root = nullptr;
    for (ModelNode& node : meshData_.additionalNodes) {
        root = findModelNodeById(node, playerInstance->rootNodeId);
        if (root != nullptr) {
            break;
        }
    }
    if (root == nullptr) {
        return;
    }
    playerRunAnimationClipIndex_ = bestClipIndex;
    const auto configureAttack = [this, animationStart](
                                     PlayerAttack attack,
                                     int score,
                                     std::uint32_t clipIndex,
                                     std::optional<std::uint32_t>& destination,
                                     const char* key
                                 ) {
        if (score < 0) {
            return;
        }
        destination = clipIndex;
        const SkeletalAnimationClip& attackClip = meshData_.skeletalAnimations[
            animationStart + clipIndex
        ];
        const double attackTicksPerSecond = attackClip.ticksPerSecond > 0.0
            ? attackClip.ticksPerSecond
            : 25.0;
        playerController_.configureAttackAnimation(
            attack,
            static_cast<float>(attackClip.durationTicks / attackTicksPerSecond)
        );
        std::cerr << "Configured Player attack clip " << attackClip.name
                  << " (" << key << " key)\n";
    };
    configureAttack(
        PlayerAttack::Tornado,
        bestAttackScore,
        bestAttackClipIndex,
        playerAttackAnimationClipIndex_,
        "R"
    );
    configureAttack(
        PlayerAttack::LightningSlash,
        bestLightningScore,
        bestLightningClipIndex,
        playerLightningAnimationClipIndex_,
        "Q"
    );
    configureAttack(
        PlayerAttack::FlameNova,
        bestFlameScore,
        bestFlameClipIndex,
        playerFlameAnimationClipIndex_,
        "E"
    );
    playerInstance->activeClip = bestClipIndex;
    const SkeletalAnimationClip& clip = meshData_.skeletalAnimations[
        animationStart + bestClipIndex
    ];
    const EstimatedPlayerRunGait gait = estimatePlayerRunGait(
        meshData_,
        *root,
        *playerInstance,
        clip,
        playerModelAutoScale_
    );
    playerController_.configureRunGait(
        gait.cycleDurationSeconds,
        gait.groundSpeed,
        gait.footTractionSamples
    );
    std::cerr << "Configured Player run gait from " << clip.name
              << ": " << gait.cycleDurationSeconds << " s cycle, "
              << gait.groundSpeed << " m/s planted-foot speed\n";
}

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
        const bool belongsToNode = mode_ == Mode::ModelViewer
            ? candidate.rootNodeId == node.nodeId
            : node.nodeId >= candidate.rootNodeId &&
                node.nodeId - candidate.rootNodeId < kSceneModelNodeStride;
        if (belongsToNode) {
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
    if (instance->rootNodeId == kPlayerSceneNodeId) {
        ImGui::TextWrapped(
            "Player animation is input-driven: movement plays Run1; Q, E, and R play distinct attacks."
        );
        const char* attackState = "Attack";
        if (playerController_.activeAttack() == PlayerAttack::LightningSlash) {
            attackState = "Lightning Slash (Q)";
        } else if (playerController_.activeAttack() == PlayerAttack::FlameNova) {
            attackState = "Flame Nova (E)";
        } else if (playerController_.activeAttack() == PlayerAttack::Tornado) {
            attackState = "Tornado (R)";
        }
        ImGui::Text(
            "State: %s / %.3f s",
            playerController_.attacking() ? attackState :
                (playerController_.moving() ? "Run1" : "Bind pose"),
            playerController_.attacking()
                ? playerController_.attackAnimationTimeSeconds()
                : playerController_.runAnimationTimeSeconds()
        );
        return;
    }
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

glm::mat4 Renderer::playerVisualTransform() const {
    if (!playerController_.active()) {
        return glm::mat4(1.0f);
    }
    const float safeAutoScale = std::max(
        playerModelAutoScale_,
        std::numeric_limits<float>::epsilon()
    );
    return glm::translate(
               glm::mat4(1.0f),
               playerController_.position()
           ) *
        glm::rotate(
            glm::mat4(1.0f),
            playerController_.facingYawRadians(),
            glm::vec3(0.0f, 1.0f, 0.0f)
        ) *
        glm::scale(glm::mat4(1.0f), glm::vec3(safeAutoScale)) *
        glm::translate(
            glm::mat4(1.0f),
            glm::vec3(0.0f, playerModelGroundOffset_, 0.0f)
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

    struct UpdatedVertexRange {
        std::size_t start = 0;
        std::size_t count = 0;
    };
    std::vector<UpdatedVertexRange> updatedVertexRanges;
    updatedVertexRanges.reserve(meshData_.skeletalAnimationInstances.size());
    for (SkeletalAnimationInstance& instance : meshData_.skeletalAnimationInstances) {
        if (instance.rootNodeId == kPlayerSceneNodeId) {
            const bool hasRunClip = playerRunAnimationClipIndex_.has_value() &&
                *playerRunAnimationClipIndex_ < instance.animationCount;
            const std::optional<std::uint32_t>* activeAttackClip = nullptr;
            if (playerController_.activeAttack() == PlayerAttack::Tornado) {
                activeAttackClip = &playerAttackAnimationClipIndex_;
            } else if (playerController_.activeAttack() == PlayerAttack::LightningSlash) {
                activeAttackClip = &playerLightningAnimationClipIndex_;
            } else if (playerController_.activeAttack() == PlayerAttack::FlameNova) {
                activeAttackClip = &playerFlameAnimationClipIndex_;
            }
            const bool hasAttackClip = activeAttackClip != nullptr &&
                activeAttackClip->has_value() &&
                **activeAttackClip < instance.animationCount;
            instance.enabled = (hasAttackClip && playerController_.attacking()) ||
                (hasRunClip && playerController_.moving());
            instance.playing = false;
            instance.looping = !playerController_.attacking();
            if (hasAttackClip && playerController_.attacking()) {
                instance.timeSeconds = playerController_.attackAnimationTimeSeconds();
                instance.activeClip = **activeAttackClip;
            } else if (hasRunClip) {
                instance.timeSeconds = playerController_.runAnimationTimeSeconds();
                instance.activeClip = *playerRunAnimationClipIndex_;
            }
        }
        const std::size_t vertexStart = instance.vertexStart;
        const std::size_t vertexCount = instance.vertexCount;
        if (vertexCount == 0 || vertexStart >= meshData_.vertices.size() ||
            vertexCount > meshData_.vertices.size() - vertexStart) {
            continue;
        }
        const std::size_t vertexEnd = vertexStart + vertexCount;
        if (!instance.enabled) {
            if (!instance.bindPoseApplied) {
                std::copy_n(
                    meshData_.bindPoseVertices.begin() + vertexStart,
                    vertexCount,
                    meshData_.vertices.begin() + vertexStart
                );
                instance.bindPoseApplied = true;
                instance.bindPoseUploadFramesRemaining =
                    static_cast<std::uint32_t>(vertexBuffers_.size());
            }
            if (instance.bindPoseUploadFramesRemaining > 0) {
                updatedVertexRanges.push_back({vertexStart, vertexCount});
                --instance.bindPoseUploadFramesRemaining;
            }
            continue;
        }
        std::copy_n(
            meshData_.bindPoseVertices.begin() + vertexStart,
            vertexCount,
            meshData_.vertices.begin() + vertexStart
        );
        updatedVertexRanges.push_back({vertexStart, vertexCount});
        instance.bindPoseApplied = true;

        if (instance.animationCount == 0 || instance.boneCount == 0) {
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
            for (ModelNode& additionalNode : meshData_.additionalNodes) {
                root = findModelNodeById(additionalNode, instance.rootNodeId);
                if (root != nullptr) {
                    break;
                }
            }
        }
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

        const std::size_t boneStart = instance.boneStart;
        if (boneStart >= meshData_.skeletonBones.size()) {
            continue;
        }
        const std::size_t boneEnd = std::min<std::size_t>(
            meshData_.skeletonBones.size(),
            boneStart + instance.boneCount
        );
        const std::size_t boneMatrixCount = boneEnd - boneStart;
        std::vector<glm::mat4> boneMatrices(boneMatrixCount, glm::mat4(1.0f));
        std::vector<glm::mat3> boneNormalMatrices(boneMatrixCount, glm::mat3(1.0f));
        std::vector<bool> validBoneMatrices(boneMatrixCount, false);
        for (std::size_t boneIndex = boneStart; boneIndex < boneEnd; ++boneIndex) {
            const SkeletonBone& bone = meshData_.skeletonBones[boneIndex];
            const auto globalIt = animatedGlobals.find(bone.nodeId);
            const glm::mat4 animatedGlobal = globalIt == animatedGlobals.end()
                ? bone.bindPoseGlobalTransform
                : globalIt->second;
            const std::size_t localBoneIndex = boneIndex - boneStart;
            boneMatrices[localBoneIndex] =
                instance.globalInverseTransform * animatedGlobal * bone.offsetMatrix;
            boneNormalMatrices[localBoneIndex] = glm::transpose(
                glm::inverse(glm::mat3(boneMatrices[localBoneIndex]))
            );
            validBoneMatrices[localBoneIndex] = true;
        }

        for (std::size_t vertexIndex = vertexStart;
             vertexIndex < vertexEnd;
             ++vertexIndex) {
            Vertex& vertex = meshData_.vertices[vertexIndex];
            const Vertex& sourceVertex = meshData_.skinSourceVertices[vertexIndex];
            glm::vec4 skinnedPosition(0.0f);
            glm::vec3 skinnedNormal(0.0f);
            float totalWeight = 0.0f;
            for (int influenceIndex = 0; influenceIndex < 4; ++influenceIndex) {
                const float weight = vertex.boneWeights[influenceIndex];
                const int signedBoneIndex = vertex.boneIndices[influenceIndex];
                if (weight <= 0.0f || signedBoneIndex < 0) {
                    continue;
                }
                const std::size_t boneIndex = static_cast<std::size_t>(signedBoneIndex);
                if (boneIndex < boneStart || boneIndex >= boneEnd) {
                    continue;
                }
                const std::size_t localBoneIndex = boneIndex - boneStart;
                if (!validBoneMatrices[localBoneIndex]) {
                    continue;
                }
                skinnedPosition += boneMatrices[localBoneIndex] * glm::vec4(
                    sourceVertex.position,
                    1.0f
                ) * weight;
                skinnedNormal += boneNormalMatrices[localBoneIndex] *
                    sourceVertex.normal * weight;
                totalWeight += weight;
            }
            if (totalWeight > std::numeric_limits<float>::epsilon()) {
                vertex.position = glm::vec3(skinnedPosition) / totalWeight;
                vertex.normal = glm::normalize(skinnedNormal / totalWeight);
            }
        }
        instance.bindPoseApplied = false;
        instance.bindPoseUploadFramesRemaining = 0;
    }

    if (currentFrame_ < vertexBuffers_.size()) {
        for (const UpdatedVertexRange& range : updatedVertexRanges) {
            vertexBuffers_[currentFrame_]->upload(
                meshData_.vertices.data() + range.start,
                sizeof(Vertex) * range.count,
                sizeof(Vertex) * range.start
            );
        }
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
        glm::mat4 objectTransform = parentTransform;
        if (node.objectType == SceneObjectType::Player) {
            objectTransform *= playerVisualTransform();
        }
        const bool isTornadoMesh = node.nodeId == kPlayerTornadoMeshNodeId;
        glm::mat4 transform = isTornadoMesh
            ? tornadoMeshTransform_
            : objectTransform * nodeManualTransform(node);
        if (!isTornadoMesh && node.rotationAnimationEnabled) {
            // Parent animation is applied after child-local animation, so
            // transforms are accumulated in hierarchy order from root to leaf.
            transform *= nodeAnimationTransform(node, elapsedSeconds);
        }
        const bool renderEnabled = parentRenderEnabled && node.renderEnabled &&
            !removedSceneNodeIds_.contains(node.nodeId) &&
            (!isTornadoMesh || tornadoMeshVisible_);
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

    if (modelResourcePath != nullptr) {
        synchronizeModelResourceMaterial(*modelResourcePath, materialKey, material);
    }
}


} // namespace VulkCore
