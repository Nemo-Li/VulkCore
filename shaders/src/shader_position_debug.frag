#version 450

// 位置调试视图。用颜色的空间变化观察节点变换以及网格的位置连续性。

layout(location = 3) in vec3 fragWorldPosition;
layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform; vec4 baseColor; uint textureFlags;
    float metallic; float roughness; float opacity; vec4 waterFlow; vec4 waterSurface;
} material;
layout(location = 0) out vec4 outColor;

void main() {
    // 用正弦把任意坐标周期性映射到 [0,1]，避免直接输出坐标时发生大面积截断。
    vec3 positionColor = 0.5 + 0.5 * sin(fragWorldPosition * 1.7 + vec3(0.0, 2.0, 4.0));
    outColor = vec4(positionColor, 1.0);
}
