#version 450

// HDR 滤波通道共用的顶点阶段。

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // 顶点索引 0、1、2 生成 (0,0)、(2,0)、(0,2)。裁剪后屏幕可见区域的 UV 为 [0,1]。
    // 单个大三角形无需顶点缓冲，屏幕中间也不会出现两个三角形的交界。
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    fragTexCoord = uv;
}
