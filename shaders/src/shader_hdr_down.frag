#version 450

// Bloom 金字塔下采样。缩小目标前先做低通滤波，以减轻亮点缩小时的闪烁。

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D source;

// 越过视口的采样按黑色处理，使画面边缘的亮点随滤波衰减。
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
    // 四个近处对角点与间隔 2 像素的九个网格点各占一半权重，图像内部的恒定亮度基本保持。
    outColor = vec4(inner * 0.125 + outer * 0.0555555, 1.0);
}
