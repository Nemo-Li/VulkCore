#version 450

// 水面顶点位移。低频程序波驱动网格轮廓，片元阶段再补充更密的法线和颜色扰动。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 time;
    vec4 viewport;
    vec4 ambientLight;
    vec4 sunLightDirection;
    vec4 sunLightColor;
} ubo;

layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform;
    vec4 baseColor;
    uint textureFlags;
    float metallic;
    float roughness;
    float opacity;
    vec4 waterFlow;
    vec4 waterSurface;
} pushConstants;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec4 fragVertexColor;
layout(location = 3) out vec3 fragWorldPosition;

void main() {
    // waterFlow.xy 为流向，z 为流速，w 为强度。流向过短时回退到默认方向以稳定归一化。
    float time = ubo.time.x;
    float directionLength = length(pushConstants.waterFlow.xy);
    vec2 flowDirection = directionLength > 0.001
        ? pushConstants.waterFlow.xy / directionLength
        : vec2(0.0, 1.0);
    float flowSpeed = max(pushConstants.waterFlow.z, 0.0);
    float flowStrength = max(pushConstants.waterFlow.w, 0.0);
    float turbulence = clamp(pushConstants.waterSurface.x, 0.0, 1.0);
    float waveScale = max(pushConstants.waterSurface.y, 0.1);
    float waveHeight = max(pushConstants.waterSurface.z, 0.0);
    // 用 UV 控制波的相位和密度，在跨度很大的导入水面上也能维持纹样尺度。
    // 多组传播方向和频率叠加，减少单一正弦带来的平行条纹。
    vec2 scaledPosition = inTexCoord * waveScale * 12.0;
    vec2 flowOffset = flowDirection * flowSpeed * time;
    float waveA = sin(dot(scaledPosition, vec2(1.8, 2.6)) +
        dot(flowOffset, vec2(2.2, 1.4)) + time * 0.18);
    float waveB = sin(dot(scaledPosition, vec2(-2.7, 1.4)) +
        dot(flowOffset, vec2(-1.3, 2.1)) + 1.7 - time * 0.12);
    float waveC = cos(dot(scaledPosition, vec2(3.2, -1.1)) +
        dot(flowOffset, vec2(1.6, -1.8)) + time * 0.08);
    float turbulenceWave = sin(dot(scaledPosition, vec2(-1.7, 2.3)) +
        dot(flowOffset, vec2(-1.1, 2.4)) + time * 0.24);
    float displacement = (waveA * 0.55 + waveB * 0.3 + waveC * 0.15 +
        turbulenceWave * turbulence * 0.2) * waveHeight * flowStrength;

    // 沿输入网格的局部法线位移，保留导入水面的朝向。
    // 输出法线仍来自原网格，细波的法线变化由片元阶段近似。
    vec3 displacedPosition = inPosition + normalize(inNormal) * displacement;
    mat4 model = ubo.model * pushConstants.nodeTransform;
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    vec3 worldPosition = (model * vec4(displacedPosition, 1.0)).xyz;

    gl_Position = ubo.projection * ubo.view * vec4(worldPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragNormal = normalize(normalMatrix * inNormal);
    fragVertexColor = inColor;
    fragWorldPosition = worldPosition;
}
