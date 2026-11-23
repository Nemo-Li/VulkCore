#version 450
#extension GL_GOOGLE_include_directive : require
#include "sun_shadow.glsl"
#include "sun_shadow_coverage.glsl"

// 粒子片元阶段。使用 CPU 计算的生命周期颜色调制贴图或程序化圆形精灵。

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

layout(location = 2) in vec3 fragWorldPosition;

layout(set = 0, binding = 1) uniform sampler2D particleTexture;

// 前两个字段保留材质布局中的偏移，使纹理标志与共享 Pipeline Layout 兼容。
layout(push_constant) uniform ParticlePushConstants {
    mat4 unusedNodeTransform;
    vec4 unusedBaseColor;
    uint textureFlags;
} particle;

SUN_SHADOW_OUTPUT

void main() {
    vec4 color;
    if ((particle.textureFlags & 1u) != 0u) {
        vec4 sampledColor = texture(particleTexture, fragTexCoord);
        color = sampledColor * fragColor;
    } else {
        // 用半径裁掉四边形角落，再在靠近外圈时平滑衰减 Alpha。
        vec2 centered = fragTexCoord * 2.0 - 1.0;
        float distanceFromCenter = length(centered);
        if (distanceFromCenter > 1.0) {
            discard;
        }
        float edgeFade = 1.0 - smoothstep(0.72, 1.0, distanceFromCenter);
        color = vec4(fragColor.rgb, fragColor.a * edgeFade);
    }
    if (color.a <= 0.001) discard;
    if ((particle.textureFlags & 2u) == 0u) {
        color.rgb *= mix(0.58, 1.0, sunVisibility(fragWorldPosition));
    }
    SUN_SHADOW_COLOR(color);
}
