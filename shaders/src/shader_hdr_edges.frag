#version 450

// SMAA 的颜色查边阶段。R、G 分别记录当前像素相对左侧和上侧的边缘。

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D source;

// 取绝对色差最大的通道，使亮度相近、色相不同的边界仍能被检测到。
float difference(vec3 a, vec3 b) {
    vec3 delta = abs(a - b);
    return max(max(delta.r, delta.g), delta.b);
}

void main() {
    // 维持 SMAA 的纵向约定，供权重搜索和最终邻域混合使用。
    vec2 pixel = vec2(1.0, -1.0) / vec2(textureSize(source, 0));
    vec2 horizontal = vec2(pixel.x, 0.0);
    vec2 vertical = vec2(0.0, pixel.y);
    vec3 center = texture(source, uv).rgb;
    vec3 left = texture(source, uv - horizontal).rgb;
    vec3 top = texture(source, uv - vertical).rgb;
    vec2 delta = vec2(difference(center, left), difference(center, top));
    vec2 edges = step(vec2(0.1), delta);
    // 目标在绘制前已清零，没有边缘的像素可以跳过。强邻边附近抑制小于其一半的色差。
    if (dot(edges, vec2(1.0)) == 0.0) discard;
    vec2 opposite = vec2(difference(center, texture(source, uv + horizontal).rgb),
                         difference(center, texture(source, uv + vertical).rgb));
    vec2 outside = vec2(difference(left, texture(source, uv - 2.0 * horizontal).rgb),
                        difference(top, texture(source, uv - 2.0 * vertical).rgb));
    vec2 largest = max(max(delta, opposite), outside);
    outColor = vec4(edges * step(vec2(max(largest.x, largest.y)), 2.0 * delta), 0.0, 0.0);
}
