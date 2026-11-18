#version 450

layout(location = 3) in vec3 fragWorldPosition;
layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform; vec4 baseColor; uint textureFlags;
    float metallic; float roughness; float opacity; vec4 waterFlow; vec4 waterSurface;
} material;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 positionColor = 0.5 + 0.5 * sin(fragWorldPosition * 1.7 + vec3(0.0, 2.0, 4.0));
    outColor = vec4(positionColor, 1.0);
}
