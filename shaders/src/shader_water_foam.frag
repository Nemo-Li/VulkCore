#version 450
#extension GL_GOOGLE_include_directive : require

// 水面泡沫预设。在屏幕颜色折射与伪反射上，通过不透明场景深度差调制泡沫。

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;
layout(location = 3) in vec3 fragWorldPosition;

#include "scene_fog.glsl"
#include "sun_shadow.glsl"
layout(set = 0, binding = 1) uniform sampler2D textureSampler;
layout(set = 0, binding = 2) uniform sampler2D sceneColorTexture;
layout(set = 0, binding = 5) uniform sampler2D sceneDepthTexture;

layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform; vec4 baseColor; uint textureFlags;
    float metallic; float roughness; float opacity; vec4 waterFlow; vec4 waterSurface;
} material;
layout(location = 0) out vec4 outColor;

// 两组方向不同的波用同一时间流偏移，提供低成本的纹理和亮度变化。
float wave(vec2 uv) {
    vec2 direction = normalize(material.waterFlow.xy + vec2(0.0001));
    float speed = max(material.waterFlow.z, 0.0);
    float scale = max(material.waterSurface.y, 0.1);
    vec2 moved = uv * scale + direction * speed * ubo.time.x * 0.12;
    return sin(dot(moved, vec2(14.0, 7.0)) + ubo.time.x * 0.2) * 0.55 +
        cos(dot(moved, vec2(-5.0, 16.0)) - ubo.time.x * 0.12) * 0.45;
}

void main() {
    float w = wave(fragTexCoord);
    vec3 normal = normalize(fragNormal + vec3(w * 0.08, -w * 0.05, 0.0));
    vec3 light = normalize(ubo.sunLightDirection.xyz);
    float sunlightVisibility = sunVisibility(fragWorldPosition, sunReceiverNormal(fragWorldPosition));
    float diffuse = max(dot(normal, light), 0.0) * sunlightVisibility;
    // 逆 View 的平移项给出场景空间相机位置，Fresnel 随实际镜头位置改变。
    vec3 viewDirection = normalize(inverse(ubo.view)[3].xyz - fragWorldPosition);
    float fresnel = pow(1.0 - max(dot(normal, viewDirection), 0.0), 3.0);
    vec2 screenUv = gl_FragCoord.xy * ubo.viewport.zw;
    vec2 flow = material.waterFlow.xy * max(material.waterFlow.z, 0.0) * ubo.time.x * 0.03;
    vec3 movingTexture = vec3(1.0);
    if ((material.textureFlags & 1u) != 0u) movingTexture = texture(textureSampler, fragTexCoord + flow).rgb;
    vec3 water = mix(vec3(0.01, 0.12, 0.2), vec3(0.03, 0.48, 0.62), w * 0.22 + 0.5);
    water *= mix(vec3(0.85), movingTexture, 0.3) * material.baseColor.rgb * fragVertexColor.rgb;
    water *= 0.22 + diffuse * 0.78;
    // 取不透明快照的偏移色和镜像色进行混合。快照已包含场景雾，只给水面局部颜色施雾。
    vec3 refracted = texture(sceneColorTexture, clamp(screenUv + normal.xy * 0.03, vec2(0.001), vec2(0.999))).rgb;
    vec3 reflected = texture(sceneColorTexture, clamp(vec2(screenUv.x, 1.0 - screenUv.y) + normal.xy * 0.02, vec2(0.001), vec2(0.999))).rgb;
    vec4 fog = sceneFogAt(fragWorldPosition);
    water = mix(mix(applySceneFog(water, fog), refracted, 0.42), reflected, 0.18 + fresnel * 0.68);

    // 用未扰动的屏幕 UV 比较同一视线上的背景深度与水面深度。
    // 这里直接比较非线性的投影深度。差值越大越接近完整泡沫权重，阈值依赖投影设置。
    float backgroundDepth = texture(sceneDepthTexture, screenUv).r;
    float depthGap = max(backgroundDepth - gl_FragCoord.z, 0.0);
    // 波形和更细的行波一起调制泡沫，以免深度差形成均匀的白色区域。
    float foam = smoothstep(0.001, 0.035, depthGap) * (0.45 + 0.55 * (0.5 + 0.5 * w));
    foam *= 0.75 + 0.25 * sin(fragTexCoord.x * 45.0 + fragTexCoord.y * 31.0 + ubo.time.x * 2.0);
    vec3 foamLight = vec3(0.92, 0.98, 1.0) * mix(0.34, 1.0, sunlightVisibility);
    vec3 color = mix(water, applySceneFog(foamLight, fog), clamp(foam, 0.0, 1.0) * 0.82);
    outColor = vec4(color, (0.72 + fresnel * 0.2 + foam * 0.12) * material.opacity);
}
