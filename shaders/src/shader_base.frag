#version 450
#extension GL_GOOGLE_include_directive : require

// 默认前向材质。组合基础色、可选贴图、Blinn–Phong 风格高光、自发光和场景雾。

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;
layout(location = 3) in vec3 fragWorldPosition;

#include "scene_fog.glsl"
#include "effect_lighting.glsl"
#include "sun_shadow.glsl"

layout(set = 0, binding = 1) uniform sampler2D textureSampler;
layout(set = 0, binding = 3) uniform sampler2D normalTexture;
layout(set = 0, binding = 4) uniform sampler2D specularTexture;
layout(set = 0, binding = 8) uniform sampler2D alphaMask;

layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform;
    vec4 baseColor;
    uint textureFlags;
    float metallic;
    float roughness;
    float opacity;
    vec4 waterFlow;
    vec4 waterSurface;
} material;

layout(location = 0) out vec4 outColor;

// 对 P(u,v) 应用链式法则，由屏幕上的位置和 UV 差分解出 ∂P/∂u，省去顶点切线属性。
// UV 雅可比接近奇异时无法稳定求逆，此时回退到插值几何法线。
vec3 applyNormalMap(vec3 geometricNormal, vec3 worldPosition, vec2 uv) {
    vec3 positionDerivativeX = dFdx(worldPosition);
    vec3 positionDerivativeY = dFdy(worldPosition);
    vec2 uvDerivativeX = dFdx(uv);
    vec2 uvDerivativeY = dFdy(uv);
    float determinant = uvDerivativeX.x * uvDerivativeY.y -
        uvDerivativeX.y * uvDerivativeY.x;
    if (abs(determinant) < 0.0001) {
        return normalize(geometricNormal);
    }

    vec3 tangent = normalize(
        (positionDerivativeX * uvDerivativeY.y - positionDerivativeY * uvDerivativeX.y) /
        determinant
    );
    // 正交化切线并用叉积补齐 TBN。纹理通道从 [0,1] 解码为切线空间的有符号方向。
    tangent = normalize(tangent - geometricNormal * dot(geometricNormal, tangent));
    vec3 bitangent = normalize(cross(geometricNormal, tangent));
    vec3 tangentNormal = texture(normalTexture, uv).xyz * 2.0 - 1.0;
    return normalize(
        tangent * tangentNormal.x +
        bitangent * tangentNormal.y +
        geometricNormal * tangentNormal.z
    );
}

void main() {
    vec3 shadowNormal = sunReceiverNormal(fragWorldPosition);
    vec4 baseColor = material.baseColor * fragVertexColor;
    vec4 texel = (material.textureFlags & 1u) != 0u ? texture(textureSampler, fragTexCoord) : vec4(1.0);
    baseColor *= texel;

    // 在遮罩 discard 前计算法线。dFdx/dFdy 和隐式纹理 LOD 依赖片元组中的相邻调用。

    vec3 normal = normalize(fragNormal);
    if ((material.textureFlags & 2u) != 0u) {
        normal = applyNormalMap(normal, fragWorldPosition, fragTexCoord);
    }
    vec3 lightDirection = normalize(ubo.sunLightDirection.xyz);
    // 默认材质用固定方向近似视线，因此旋转相机时镜面高光不会精确跟随。
    vec3 viewDirection = normalize(vec3(0.42, 0.48, 0.77));
    vec3 halfwayDirection = normalize(lightDirection + viewDirection);

    vec3 ambientLight = max(ubo.ambientLight.rgb, vec3(0.0)) * max(ubo.ambientLight.a, 0.0);
    vec3 diffuseLight = max(ubo.sunLightColor.rgb, vec3(0.0)) * max(ubo.sunLightDirection.w, 0.0);
    vec3 specularLight = mix(vec3(1.0, 0.98, 0.92), diffuseLight, 0.35);
    float diffuseFactor = max(dot(normal, lightDirection), 0.0);
    float roughness = clamp(material.roughness, 0.04, 1.0);
    float metallic = clamp(material.metallic, 0.0, 1.0);
    float opacity = clamp(material.opacity, 0.0, 1.0);
    // 粗糙度越高，半角向量的幂指数越小，高光覆盖角越宽。
    float specularPower = mix(96.0, 12.0, roughness);
    float specularFactor = pow(max(dot(normal, halfwayDirection), 0.0), specularPower);
    float specularMapFactor = 1.0;
    if ((material.textureFlags & 4u) != 0u) {
        specularMapFactor = texture(specularTexture, fragTexCoord).r;
    }

    // Alpha Test 将基础色与遮罩的覆盖率同材质阈值比较，适合镂空边缘和深度写入。
    if ((material.textureFlags & 64u) != 0u) {
        float coverage = texel.a * material.baseColor.a * texture(alphaMask, fragTexCoord).r;
        if (coverage < material.waterFlow.x) discard;
    }
    // 金属度在这里调节漫反射和镜面权重。waterSurface 在此材质中复用为自发光 RGB 和强度。
    float sunlightVisibility = sunVisibility(fragWorldPosition, shadowNormal);
    vec3 pointLight = effectPointLighting(fragWorldPosition, normal);
    vec3 litColor = baseColor.rgb * (ambientLight + diffuseLight * diffuseFactor * sunlightVisibility + pointLight) *
        (1.0 - metallic * 0.35);
    litColor += specularLight * specularFactor * mix(0.12, 0.5, metallic) * specularMapFactor * sunlightVisibility;
    litColor += max(material.waterSurface.rgb, vec3(0.0)) * max(material.waterSurface.a, 0.0);
    outColor = vec4(applySceneFog(litColor, sceneFogAt(fragWorldPosition)), baseColor.a * opacity);
}
