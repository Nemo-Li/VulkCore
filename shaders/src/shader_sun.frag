#version 450

// 渲染场景中的太阳球体网格，用色带、轮廓脉动和自发光表现插画式日面。

layout(location = 1) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPosition;

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

layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform;
    vec4 baseColor;
    uint textureFlags;
    float metallic;
    float roughness;
    float opacity;
    vec4 waterFlow;
    // 复用共享材质末尾的四个浮点数，保存自发光 RGB 和强度。
    vec4 waterSurface;
} material;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 lightDirection = normalize(ubo.sunLightDirection.xyz);
    vec3 viewDirection = normalize(vec3(0.42, 0.48, 0.77));
    float diffuse = max(dot(normal, lightDirection), 0.0);

    // 将漫反射量化为三档。上界略低于 1，使乘以 3 后向下取整的最大档位仍是 2。
    float band = floor(clamp(diffuse * 0.75 + 0.25, 0.0, 0.999) * 3.0) / 2.0;
    vec3 shadowColor = material.baseColor.rgb * vec3(0.72, 0.12, 0.015);
    vec3 highlightColor = material.baseColor.rgb * vec3(1.0, 0.72, 0.08);
    vec3 color = mix(shadowColor, highlightColor, band);

    // 用场景坐标驱动轮廓纹理，使图案附着在球面上。掠射角处的 rim 叠加动画和暖色边光。
    float contour = 0.5 + 0.5 * sin(
        fragWorldPosition.x * 13.0 + fragWorldPosition.y * 9.0 + ubo.time.x * 0.8
    );
    float rim = pow(1.0 - max(dot(normal, viewDirection), 0.0), 2.5);
    color *= mix(0.92, 1.08, contour * rim);
    color += vec3(1.0, 0.18, 0.01) * rim * 0.28;

    vec3 emission = max(material.waterSurface.rgb, vec3(0.0)) *
        max(material.waterSurface.a, 0.0);
    color += emission;
    outColor = vec4(color, material.baseColor.a * clamp(material.opacity, 0.0, 1.0));
}
