#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D source;

// 13-tap bloom downsample, with black outside the viewport.
vec3 tap(vec2 coordinate) {
    if (any(lessThan(coordinate, vec2(0.0))) || any(greaterThan(coordinate, vec2(1.0)))) {
        return vec3(0.0);
    }
    return texture(source, coordinate).rgb;
}

void main() {
    vec2 pixel = 1.0 / vec2(textureSize(source, 0));
    vec3 inner = vec3(0.0);
    for (int y = -1; y <= 1; y += 2) {
        for (int x = -1; x <= 1; x += 2) inner += tap(uv + vec2(x, y) * pixel);
    }
    vec3 outer = vec3(0.0);
    for (int y = -2; y <= 2; y += 2) {
        for (int x = -2; x <= 2; x += 2) outer += tap(uv + vec2(x, y) * pixel);
    }
    outColor = vec4(inner * 0.125 + outer * 0.0555555, 1.0);
}
