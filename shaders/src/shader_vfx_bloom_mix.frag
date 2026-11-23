#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D source;
layout(binding = 1) uniform sampler2D bloom;
layout(push_constant) uniform Parameters { vec4 value; } parameters;

void main() {
    outColor = vec4(texture(source, uv).rgb + texture(bloom, uv).rgb * parameters.value.x, 1.0);
}
