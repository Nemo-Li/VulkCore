#version 450
#extension GL_GOOGLE_include_directive : require

#include "scene_frame.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 6) in vec4 inTerrainMapping;

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

layout(location = 0) out vec2 fragMapUv;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec4 vertexColor;
layout(location = 3) out vec3 fragWorldPosition;
layout(location = 4) out vec2 fragDetailUv;
layout(location = 5) flat out int fragMapLayer;

void main() {
    mat4 model = ubo.model * material.nodeTransform;
    fragWorldPosition = (model * vec4(inPosition, 1.0)).xyz;
    gl_Position = ubo.projection * ubo.view * vec4(fragWorldPosition, 1.0);
    fragNormal = normalize(transpose(inverse(mat3(model))) * inNormal);
    fragMapUv = inTexCoord;
    fragDetailUv = inPosition.xz / 4.0;
    fragMapLayer = int(inTerrainMapping.x);
    vertexColor = vec4(1.0);
}
