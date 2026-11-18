#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D source;
layout(binding = 1) uniform sampler2D bloom;
layout(binding = 2) uniform sampler2D weights;
layout(push_constant) uniform Parameters { vec4 value; } parameters;

vec3 neighborhood(vec3 center) {
    vec2 pixel = vec2(1.0, -1.0) / vec2(textureSize(source, 0));
    vec4 here = texture(weights, uv);
    float right = texture(weights, uv + vec2(pixel.x, 0.0)).a;
    float top = texture(weights, uv + vec2(0.0, pixel.y)).g;
    if (right + here.b + top + here.r < 0.00001) return center;

    bool horizontal = max(right, here.b) > max(top, here.r);
    vec2 weight = horizontal ? vec2(right, here.b) : vec2(top, here.r);
    vec2 direction = horizontal ? vec2(pixel.x, 0.0) : vec2(0.0, pixel.y);
    vec3 first = texture(source, uv + direction * weight.x).rgb;
    vec3 second = texture(source, uv - direction * weight.y).rgb;
    return (first * weight.x + second * weight.y) / (weight.x + weight.y);
}

vec3 linearToSrgb(vec3 color) {
    return mix(12.92 * color, 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055,
               greaterThan(color, vec3(0.0031308)));
}

void main() {
    vec3 color = texture(source, uv).rgb;
    if (parameters.value.y > 0.5) color = neighborhood(color);
    color = clamp(color + texture(bloom, uv).rgb * parameters.value.x, 0.0, 1.0);
    if (parameters.value.z > 0.5) color = linearToSrgb(color);
    outColor = vec4(color, 1.0);
}
