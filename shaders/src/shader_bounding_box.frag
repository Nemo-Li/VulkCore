#version 450

// 以固定亮黄色标出选中的包围盒，颜色独立于所选材质和场景灯光。

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(1.0, 0.9, 0.08, 1.0);
}
