#version 450
#extension GL_GOOGLE_include_directive : require

#include "scene_fog.glsl"

layout(location = 0) out vec3 fragWorldRay;

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 clip = uv * 2.0 - 1.0;
    // Interpolate an unnormalized view ray from the fullscreen triangle. A
    // depth of one lets the sky fill only pixels left clear by opaque geometry.
    gl_Position = vec4(clip, 1.0, 1.0);
    vec3 ray = vec3(clip.x / ubo.projection[0][0], clip.y / ubo.projection[1][1], -1.0);
    fragWorldRay = transpose(mat3(ubo.view)) * ray;
}
