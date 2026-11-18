#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 vertexColor;
layout(location = 3) in vec3 worldPosition;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform Frame {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 time;
    vec4 viewport;
    vec4 ambient;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 cameraPosition;
} frame;

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
    outColor = vec4(vertexColor.rgb * material.baseColor.rgb, vertexColor.a * material.opacity);
}
