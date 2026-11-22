#version 450
#extension GL_GOOGLE_include_directive : require

#define VULKCORE_SCENE_FRAME_BINDING 4
#include "solar_flare_vertex.glsl"

layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 3) uniform sampler2D sceneDepth;
layout(location = 0) out vec2 uv;
layout(location = 1) flat out vec4 solarFlare;

void main() {
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    solarFlare = projectSolarFlare(sceneColor, sceneDepth);
}
