#version 450
#extension GL_GOOGLE_include_directive : require
#include "sun_shadow.glsl"
#include "sun_shadow_coverage.glsl"

// 在网格表面生成沿场景 XZ 平面对齐的程序化网格线。

layout(location = 2) in vec4 vertexColor;
layout(location = 3) in vec3 worldPosition;
SUN_SHADOW_OUTPUT

layout(push_constant) uniform Material {
    mat4 model;
    vec4 baseColor;
    uint textureFlags;
    float metallic;
    float roughness;
    float opacity;
    vec4 parameters0; // x 为场景坐标中的网格间距，y 为像素线宽。
    vec4 parameters1;
} material;

void main() {
    vec2 lattice = worldPosition.xz / max(material.parameters0.x, 0.0001);
    vec2 dx = dFdx(lattice);
    vec2 dy = dFdy(lattice);
    vec2 unitsPerPixel = max(sqrt(dx * dx + dy * dy), vec2(0.00001));
    // fract 找到最近的整数网格线。用屏幕梯度的模长把格距换成像素距离，维持线宽。
    vec2 pixelDistance = abs(fract(lattice + 0.5) - 0.5) / unitsPerPixel;
    vec2 axisCoverage = 1.0 - smoothstep(0.0, max(material.parameters0.y, 0.01), pixelDistance);
    // 单像素覆盖接近一整个格子时淡出，降低地平线附近的摩尔纹。
    axisCoverage *= 1.0 - smoothstep(vec2(0.4), vec2(0.85), unitsPerPixel);
    float sunlightVisibility = sunVisibility(worldPosition, sunReceiverNormal(worldPosition));
    SUN_SHADOW_COLOR(vec4(vertexColor.rgb * material.baseColor.rgb * mix(0.45, 1.0, sunlightVisibility),
        max(axisCoverage.x, axisCoverage.y) * vertexColor.a * material.opacity));
}
