#version 450

// Bloom 亮区提取。保留线性 RGB 中超过阈值的颜色和亮度，供后续多级滤波。

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D source;
layout(push_constant) uniform Parameters { vec4 value; } parameters;

void main() {
    vec3 color = texture(source, uv).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float threshold = parameters.value.x;
    float smoothing = parameters.value.y;
    // 以线性 Rec.709 亮度选择发光区域。软阈值让刚达到阈值的像素逐渐进入 Bloom。
    float mask = smoothing > 0.0 ? smoothstep(threshold, threshold + smoothing, luminance)
                                : step(threshold, luminance);
    outColor = vec4(color * mask, 1.0);
}
