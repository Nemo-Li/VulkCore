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

vec2 hash(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float noise2d(vec2 p) {
    const float k1 = 0.366025404;
    const float k2 = 0.211324865;
    vec2 i = floor(p + (p.x + p.y) * k1);
    vec2 a = p - i + (i.x + i.y) * k2;
    vec2 o = a.x > a.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec2 b = a - o + k2;
    vec2 c = a - 1.0 + 2.0 * k2;
    vec3 h = max(0.5 - vec3(dot(a, a), dot(b, b), dot(c, c)), 0.0);
    return dot(h * h * h * h * vec3(dot(a, hash(i)), dot(b, hash(i + o)),
                                     dot(c, hash(i + 1.0))), vec3(70.0));
}

void main() {
    vec4 wave = material.parameters0;
    vec2 point = (uv + vec2(frame.time.y * wave.w, 0.0)) * wave.xy;
    float noiseMask = noise2d(point) * wave.z;
    vec2 mask = smoothstep(vec2(0.05), vec2(0.4), uv) *
                (1.0 - smoothstep(vec2(0.6), vec2(0.95), uv));
    outColor = vec4(clamp(vertexColor.rgb * 2.0, 0.0, 1.0),
                    clamp(noiseMask * mask.x * mask.y * material.opacity, 0.0, 1.0));
}
