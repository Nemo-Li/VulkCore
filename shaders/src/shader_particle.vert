#version 450

// 粒子顶点阶段。CPU 已将每个粒子展开为面向相机的四边形。

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
layout(location = 2) out vec3 fragWorldPosition;

void main() {
    // 粒子顶点已处于归一化场景空间，直接应用 View 和 Projection 以避免重复归一化。
    gl_Position = ubo.projection * ubo.view * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    fragWorldPosition = inPosition;
}
