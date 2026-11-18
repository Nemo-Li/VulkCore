#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 2) in vec4 fragVertexColor;
layout(set = 0, binding = 1) uniform sampler2D textureSampler;

layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform;
    vec4 baseColor;
    uint textureFlags;
    float metallic;
    float roughness;
    float opacity;
    vec4 waterFlow;
    vec4 waterSurface;
} material;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 color = material.baseColor * fragVertexColor;
    if ((material.textureFlags & 1u) != 0u) {
        color *= texture(textureSampler, fragTexCoord);
    }
    outColor = vec4(color.rgb, color.a * clamp(material.opacity, 0.0, 1.0));
}
