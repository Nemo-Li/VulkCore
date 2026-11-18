#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D source;
layout(push_constant) uniform Parameters { vec4 value; } parameters;

void main() {
    vec3 color = texture(source, uv).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float threshold = parameters.value.x;
    float smoothing = parameters.value.y;
    float mask = smoothing > 0.0 ? smoothstep(threshold, threshold + smoothing, luminance)
                                : step(threshold, luminance);
    outColor = vec4(color * mask, 1.0);
}
