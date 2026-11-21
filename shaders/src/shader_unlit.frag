#version 450
#extension GL_GOOGLE_include_directive : require
#include "sun_shadow.glsl"

// 平涂材质。基础色直接决定输出，并用太阳可见度调制阴影区域的亮度。

layout(location = 0) in vec2 fragTexCoord;
layout(location = 2) in vec4 fragVertexColor;
layout(location = 3) in vec3 fragWorldPosition;
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
    // 缺少贴图时相当于乘白色，保留材质与顶点携带的颜色和覆盖率。
    if ((material.textureFlags & 1u) != 0u) {
        color *= texture(textureSampler, fragTexCoord);
    }
    outColor = vec4(color.rgb * mix(0.47, 1.0, sunVisibility(fragWorldPosition, sunReceiverNormal(fragWorldPosition))),
        color.a * clamp(material.opacity, 0.0, 1.0));
}
