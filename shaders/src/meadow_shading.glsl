// 实例草叶与地表共用的线性 RGB 底色、地表纹理和漫反射曲线。
const vec3 kMeadowAlbedo = vec3(0.245, 0.435, 0.086);
const uint kGrassGroundTexture = 64u;
layout(set = 0, binding = 9) uniform sampler2D grassGroundTexture;

float meadowBladeDetail(float distanceWorld, float fadeStart) {
    return 1.0 - smoothstep(1.5, fadeStart, distanceWorld);
}

vec3 meadowGroundAlbedo(vec2 groundPosition, float distanceWorld, vec2 fadeRange) {
    // 近景保留 2×2 平铺，随草叶淡出过渡到 8×8，再过渡到 24×24。
    float lod0Weight = 1.0 - smoothstep(fadeRange.x, fadeRange.y, distanceWorld);
    float coarseWeight = smoothstep(fadeRange.y, 2.0 * fadeRange.y, distanceWorld);
    float nearWeight = 1.0 - lod0Weight - coarseWeight;
    vec2 texels = vec2(textureSize(grassGroundTexture, 0));
    float longEdge = max(texels.x, texels.y);
    vec2 uv = groundPosition * (longEdge / texels);
    // 用相机距离选择 mip，地表和草叶在同一位置取到相同的颜色。
    float mip = log2(max(2.0 * distanceWorld * longEdge / max(ubo.viewport.y, 1.0), 1.0));
    vec3 color = vec3(0.0);
    if (lod0Weight > 0.0) {
        color += lod0Weight * textureLod(grassGroundTexture, uv / 2.0, max(0.0, mip - 1.0)).rgb;
    }
    if (nearWeight > 0.0) {
        color += nearWeight * textureLod(grassGroundTexture, uv / 8.0, max(0.0, mip - 3.0)).rgb;
    }
    if (coarseWeight > 0.0) {
        color += coarseWeight * textureLod(grassGroundTexture, uv / 24.0, max(0.0, mip - log2(24.0))).rgb;
    }
    // 在线性空间校正高亮地表贴图，避免接近直射光时草甸过曝。
    return color * vec3(0.53, 0.58, 0.67);
}

vec3 meadowGroundTint(vec3 albedo, float detail) {
    return albedo * mix(vec3(1.0), vec3(0.85, 0.88, 0.63), detail);
}

// 把明暗交界扩展到略微背光的方向，近似草丛透光和柔和的漫反射过渡。
float meadowDiffuse(float facing) {
    return smoothstep(-0.24, 0.82, facing);
}

vec3 meadowGroundLighting(float facing, vec3 ambient, vec3 sunlight) {
    return ambient + sunlight * meadowDiffuse(facing);
}
