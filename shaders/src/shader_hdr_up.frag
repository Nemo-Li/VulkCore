#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D source;
layout(binding = 1) uniform sampler2D support;
layout(push_constant) uniform Parameters { vec4 value; } parameters;

void main() {
    vec2 pixel = 1.0 / vec2(textureSize(source, 0));
    vec3 blurred = vec3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float weight = float((2 - abs(x)) * (2 - abs(y))) / 16.0;
            blurred += texture(source, uv + vec2(x, y) * pixel).rgb * weight;
        }
    }
    outColor = vec4(mix(texture(support, uv).rgb, blurred, parameters.value.x), 1.0);
}
