#version 450

// 法线调试视图。显示几何法线或法线贴图计算出的场景空间方向。

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPosition;
layout(set = 0, binding = 3) uniform sampler2D normalTexture;
layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform; vec4 baseColor; uint textureFlags;
    float metallic; float roughness; float opacity; vec4 waterFlow; vec4 waterSurface;
} material;
layout(location = 0) out vec4 outColor;

// 与基础材质相同，用位置和 UV 的屏幕差分求切线。退化 UV 时保留几何法线。
vec3 mappedNormal(vec3 geometricNormal) {
    vec3 dx = dFdx(fragWorldPosition), dy = dFdy(fragWorldPosition);
    vec2 du = dFdx(fragTexCoord), dv = dFdy(fragTexCoord);
    float determinant = du.x * dv.y - du.y * dv.x;
    if (abs(determinant) < 0.0001) return normalize(geometricNormal);
    vec3 tangent = normalize((dx * dv.y - dy * du.y) / determinant);
    tangent = normalize(tangent - geometricNormal * dot(geometricNormal, tangent));
    vec3 bitangent = normalize(cross(geometricNormal, tangent));
    vec3 tangentNormal = texture(normalTexture, fragTexCoord).xyz * 2.0 - 1.0;
    return normalize(tangent * tangentNormal.x + bitangent * tangentNormal.y + geometricNormal * tangentNormal.z);
}

void main() {
    vec3 normal = normalize(fragNormal);
    if ((material.textureFlags & 2u) != 0u) normal = mappedNormal(normal);
    // 将有符号方向 [-1,1] 映射到可显示的 RGB [0,1]，正负轴可由色彩区分。
    outColor = vec4(normal * 0.5 + 0.5, 1.0);
}
