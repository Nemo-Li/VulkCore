#version 450
#extension GL_GOOGLE_include_directive : require

// 全屏天空顶点阶段。用相机旋转恢复世界方向，天空底色只随观察方向变化。

#include "scene_fog.glsl"

layout(location = 0) out vec3 fragWorldRay;

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 clip = uv * 2.0 - 1.0;
    // 大三角形覆盖视口，裁剪位置取 z = w，对应远平面深度 1。
    // 天空管线以 Equal 匹配清屏深度 1，且关闭深度写入。
    gl_Position = vec4(clip, 1.0, 1.0);
    // 令视线 z = -1 并撤销透视矩阵的 XY 缩放。View 旋转是正交矩阵，转置可直接还原方向。
    // 保持射线长度到片元阶段再归一化，避免先归一化顶点方向造成插值误差。
    vec3 ray = vec3(clip.x / ubo.projection[0][0], clip.y / ubo.projection[1][1], -1.0);
    fragWorldRay = transpose(mat3(ubo.view)) * ray;
}
