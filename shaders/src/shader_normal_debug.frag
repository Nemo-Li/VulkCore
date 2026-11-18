#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPosition;
layout(set = 0, binding = 3) uniform sampler2D normalTexture;
layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform; vec4 baseColor; uint textureFlags;
    float metallic; float roughness; float opacity; vec4 waterFlow; vec4 waterSurface;
} material;
layout(location = 0) out vec4 outColor;

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
    outColor = vec4(normal * 0.5 + 0.5, 1.0);
}
