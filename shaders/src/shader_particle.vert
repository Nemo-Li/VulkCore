#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 time;
    vec4 viewport;
    vec4 ambientLight;
    vec4 sunLightDirection;
    vec4 sunLightColor;
} ubo;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

void main() {
    // Particle positions are emitted in normalized scene space, so they use
    // the camera directly rather than the imported model normalization.
    gl_Position = ubo.projection * ubo.view * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragColor = inColor;
}
