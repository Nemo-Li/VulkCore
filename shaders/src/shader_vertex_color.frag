#version 450
#extension GL_GOOGLE_include_directive : require
#include "sun_shadow.glsl"

// 以顶点色显示网格，例如线框过渡中的发光线段。可直接保留 HDR 范围的 RGB。

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 vertexColor;
layout(location = 3) in vec3 worldPosition;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Material {
    mat4 model;
    vec4 baseColor;
    uint textureFlags;
    float metallic;
    float roughness;
    float opacity;
    vec4 parameters0;
    vec4 parameters1;
} material;

void main() {
    // RGB 用于给顶点色染色，Alpha 由顶点覆盖率乘材质不透明度控制，可接入过渡进度。
    float sunlightVisibility = sunVisibility(worldPosition, sunReceiverNormal(worldPosition));
    outColor = vec4(vertexColor.rgb * material.baseColor.rgb * mix(0.53, 1.0, sunlightVisibility),
        vertexColor.a * material.opacity);
}
