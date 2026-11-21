#version 450
#extension GL_GOOGLE_include_directive : require
#include "sun_shadow.glsl"
#include "sun_shadow_coverage.glsl"

// 实体网格到线框的过渡。沿扫描平面裁切实体，在保留侧的切口附近发光。

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;
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
    vec4 parameters0;
    vec4 parameters1;
} material;

layout(set = 0, binding = 1) uniform sampler2D baseTexture;

// ACES 拟合先在输入矩阵变换后的空间应用曝光与有理曲线，再转回输出 RGB。
// 压缩亮部，使扫描带能够在增加能量时仍保留层次。
vec3 aces(vec3 color) {
    const mat3 inputMatrix = mat3(0.59719, 0.07600, 0.02840,
                                  0.35458, 0.90834, 0.13383,
                                  0.04823, 0.01566, 0.83777);
    const mat3 outputMatrix = mat3(1.60475, -0.10208, -0.00327,
                                  -0.53108, 1.10813, -0.07276,
                                  -0.07367, -0.00605, 1.07602);
    vec3 v = inputMatrix * color * (ubo.time.z / 0.6);
    vec3 fit = (v * (v + 0.0245786) - 0.000090537) /
               (v * (0.983729 * v + 0.4329510) + 0.238081);
    return clamp(outputMatrix * fit, 0.0, 1.0);
}

void main() {
    vec3 shadowNormal = sunReceiverNormal(worldPosition);
    float progress = material.opacity;
    if (progress >= 0.999999) discard;
    // opacity 槽位承载场景过渡进度。扫描坐标的斜率和偏置将整段行程映射到 [0,1]，
    // parameters1.w 也是该行程的比例。扫描带只画在尚未裁掉的一侧。
    float distanceToScan = dot(material.parameters0.xyz, worldPosition) +
                           material.parameters0.w - progress;
    float rim = 0.0;
    if (progress > 0.000001) {
        if (distanceToScan < 0.0) discard;
        rim = 1.0 - smoothstep(0.0, material.parameters1.w, distanceToScan);
    }
    vec4 surface = material.baseColor * vertexColor;
    if ((material.textureFlags & 1u) != 0u) surface *= texture(baseTexture, uv);
    vec3 color = surface.rgb;
    // 双面绘制时将背面的法线翻转，使保留下来的内侧也能正确受光。
    vec3 n = normalize(gl_FrontFacing ? normal : -normal);
    vec3 l = normalize(ubo.sunLightDirection.xyz);
    vec3 v = normalize(ubo.cameraPosition.xyz - worldPosition);
    vec3 h = normalize(v + l);
    float nl = max(dot(n, l), 0.0);
    float nv = max(dot(n, v), 0.001);
    float nh = max(dot(n, h), 0.0);
    float vh = max(dot(v, h), 0.0);
    // Cook–Torrance 镜面项由 GGX 分布、相关 Smith 可见性、Schlick Fresnel 相乘。
    // 将美术粗糙度平方后作为 GGX 的 α，使较低粗糙度的区间有更细的高光宽度控制。
    float a = material.roughness * material.roughness;
    float a2 = a * a;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    float distribution = a2 / (3.14159265 * d * d);
    float visibility = 0.5 / max(nl * sqrt(nv * nv * (1.0 - a2) + a2) +
                                 nv * sqrt(nl * nl * (1.0 - a2) + a2), 0.00001);
    // 绝缘体基准反射率为 0.04。金属度越高，反射越接近底色，漫反射越弱。
    vec3 f0 = mix(vec3(0.04), color, material.metallic);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
    vec3 diffuse = color * (1.0 - material.metallic) / 3.14159265;
    vec3 light = ubo.sunLightColor.rgb * ubo.sunLightDirection.w * sunVisibility(worldPosition, shadowNormal);
    vec3 ambient = ubo.ambientLight.rgb * ubo.ambientLight.a * (0.5 + 0.5 * n.y);
    // 扫描带作为自发光参与曝光与色调映射，并在靠近切口时提高表面覆盖率。
    SUN_SHADOW_COLOR(vec4(aces(diffuse * ambient + (diffuse + distribution * visibility * fresnel) *
                         light * nl + material.parameters1.rgb * rim),
                    mix(surface.a, 1.0, rim)));
}
