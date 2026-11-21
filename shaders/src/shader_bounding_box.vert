#version 450

// 选中对象的包围盒线段顶点。CPU 已将节点变换应用于局部包围盒的角点。

layout(location = 0) in vec3 inPosition;

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

void main() {
    // CPU 生成的线段仍使用归一化前的坐标，须应用与网格一致的全局场景变换。
    gl_Position = ubo.projection * ubo.view * ubo.model * vec4(inPosition, 1.0);
}
