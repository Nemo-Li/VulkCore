#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D source;

// Use the same vertical orientation as the reference SMAA shaders.
float difference(vec3 a, vec3 b) {
    vec3 delta = abs(a - b);
    return max(max(delta.r, delta.g), delta.b);
}

void main() {
    vec2 pixel = vec2(1.0, -1.0) / vec2(textureSize(source, 0));
    vec2 horizontal = vec2(pixel.x, 0.0);
    vec2 vertical = vec2(0.0, pixel.y);
    vec3 center = texture(source, uv).rgb;
    vec3 left = texture(source, uv - horizontal).rgb;
    vec3 top = texture(source, uv - vertical).rgb;
    vec2 delta = vec2(difference(center, left), difference(center, top));
    vec2 edges = step(vec2(0.1), delta);
    if (dot(edges, vec2(1.0)) == 0.0) discard;
    vec2 opposite = vec2(difference(center, texture(source, uv + horizontal).rgb),
                         difference(center, texture(source, uv + vertical).rgb));
    vec2 outside = vec2(difference(left, texture(source, uv - 2.0 * horizontal).rgb),
                        difference(top, texture(source, uv - 2.0 * vertical).rgb));
    vec2 largest = max(max(delta, opposite), outside);
    outColor = vec4(edges * step(vec2(max(largest.x, largest.y)), 2.0 * delta), 0.0, 0.0);
}
