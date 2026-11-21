// 场景雾与云的共享实现。相机、片元使用归一化场景空间，云雾和距离参数使用原始单位。
#include "scene_frame.glsl"
#include "daylight_atmosphere.glsl"

// 撤销场景归一化中的平移和统一缩放，以便与 CPU 布置的云雾中心比较。
vec3 daylightWorldPosition(vec3 scenePosition) {
    return (scenePosition - ubo.model[3].xyz) / max(ubo.cameraPosition.w, 1e-8);
}

float mistHash(vec2 cell) {
    vec3 p = fract(vec3(cell.xyx) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float mistNoise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    // 整数格点哈希后做三次平滑插值，使噪声在格边界处连续且一阶导数为零。
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mistHash(cell), mistHash(cell + vec2(1, 0)), f.x),
               mix(mistHash(cell + vec2(0, 1)), mistHash(cell + vec2(1, 1)), f.x), f.y);
}

float mistPuffOpacity(vec3 camera, vec3 direction, float distance, vec4 center, vec4 shape, vec2 drift) {
    // 先减中心再乘轴半径的倒数，把椭球变成单位球。射线参数仍是原始场景单位的距离。
    // 投影求最近点和弦长，避免逐步采样整条视线。edge <= 0 表示没有有效交段。
    vec3 origin = (camera - center.xyz) * shape.xyz;
    vec3 rayStep = direction * shape.xyz;
    float speedSquared = dot(rayStep, rayStep);
    float middle = -dot(origin, rayStep) / speedSquared;
    vec3 closest = origin + rayStep * middle;
    float edge = 1.0 - dot(closest, closest);
    if (edge <= 0.0) return 0.0;
    float halfPath = sqrt(edge / speedSquared);
    // 只保留相机到当前表面之间的交段，兼容相机位于雾内的情况。
    float entry = max(middle - halfPath, 0.0);
    float exit = min(middle + halfPath, distance);
    if (exit <= entry) return 0.0;
    float fraction = (exit - entry) / (2.0 * halfPath);

    // 在可见交段内取靠近球心的样本。两层随时间漂移的噪声打破规则的椭球轮廓。
    vec3 samplePoint = origin + rayStep * clamp(middle, entry, exit);
    vec2 noisePosition = samplePoint.xz * vec2(2.2, 2.8) + samplePoint.y * vec2(1.9, -1.2) + shape.w;
    float billows = 0.72 * mistNoise(noisePosition + drift) +
        0.28 * mistNoise(noisePosition * 2.63 - drift - vec2(4.5, 9.4));
    float profile = smoothstep(0.0, 0.58, edge) * smoothstep(0.22, 0.74, billows);
    return profile * smoothstep(0.0, 1.0, fraction) * center.w;
}

// CPU 在上传前裁剪数量。逐团相乘的是剩余透射率，重叠时自然趋近完全遮蔽。
float floatingMistOpacity(vec3 camera, vec3 direction, float distance) {
    int count = int(ubo.floatingMist.x);
    if (count == 0 || distance <= 65.0) return 0.0;
    vec2 drift = ubo.floatingMist.z * vec2(0.014, -0.006);
    float transmission = 1.0;
    for (int i = 0; i < count; ++i) {
        float opacity = mistPuffOpacity(camera, direction, distance, ubo.mistCenters[i], ubo.mistInverseRadii[i], drift);
        transmission *= 1.0 - opacity * ubo.floatingMist.y;
    }
    return (1.0 - transmission) * smoothstep(65.0, 120.0, distance);
}

float cloudMistOpacity(vec3 camera, vec3 direction, float distance) {
    int count = int(ubo.cloudMist.x);
    if (count == 0) return 0.0;
    // 如果视线段完全在云层高度范围之外，就跳过逐椭球的相交计算。
    float endHeight = camera.y + direction.y * distance;
    if (max(camera.y, endHeight) <= ubo.cloudMist.y || min(camera.y, endHeight) >= ubo.cloudMist.z) return 0.0;

    vec2 drift = ubo.daylight.y * vec2(0.014, -0.006);
    float transmission = 1.0;
    for (int i = 0; i < count; ++i) {
        float opacity = mistPuffOpacity(camera, direction, distance, ubo.cloudCenters[i], ubo.cloudInverseRadii[i], drift);
        transmission *= 1.0 - opacity;
    }
    return 1.0 - transmission;
}

vec4 sceneMistAt(vec3 direction, float floatingMistDistance, float cloudDistance) {
    vec3 camera = daylightWorldPosition(ubo.cameraPosition.xyz);
    float mist = floatingMistOpacity(camera, direction, floatingMistDistance);
    float cloud = cloudMistOpacity(camera, direction, cloudDistance);
    return vec4(vec3(0.76, 0.84, 0.86), 1.0 - (1.0 - mist) * (1.0 - cloud));
}

vec4 distanceFogAt(vec3 direction, float distance, bool terrainFallback) {
    if (terrainFallback && ubo.distanceFogParams.w == 0.0) {
        // 旧场景缺少雾预设时沿用地形薄雾，保持早期场景的远处层次。
        float haze = 1.0 - exp2(-max(distance - 125.0, 0.0) / 1350.0);
        haze *= mix(0.58, 0.34, smoothstep(0.02, 0.48, direction.y));
        return vec4(0.19, 0.34, 0.48, haze);
    }
    float fogDistance = distance - ubo.distanceFogParams.x;
    if (fogDistance <= 0.0 || ubo.distanceFogParams.z <= 0.0) return vec4(0.0);

    // 仰视穿过的低空雾较少。用视线方向同时过渡雾色和有效光程。
    float upper = smoothstep(0.02, 0.48, direction.y);
    vec3 tint = mix(ubo.distanceFogHorizon.rgb, ubo.distanceFogUpper.rgb, upper);
    float opticalDepth = (fogDistance / ubo.distanceFogParams.y) * mix(1.0, 0.65, upper);
    if (ubo.daylight.x > 0.0) {
        float pathHeight = daylightWorldPosition(ubo.cameraPosition.xyz).y +
            direction.y * (distance + ubo.distanceFogParams.x) * 0.5;
        // 用起雾点到表面之间的中点高度近似空气密度，让山谷比山顶更朦胧。
        opticalDepth *= mix(0.45, 1.2, exp2(-max(pathHeight, 0.0) / 520.0));
        tint += atmosphereInscatter(direction, normalize(ubo.sunLightDirection.xyz),
            ubo.sunLightColor.rgb * ubo.sunLightDirection.w);
    }
    // Beer–Lambert 指数消光的底数 2 写法。未加高度和方向修正时，每个参考半衰距离将透射率减半。
    float amount = (1.0 - exp2(-opticalDepth)) * ubo.distanceFogParams.z;
    return vec4(tint, amount);
}

// 返回线性 RGB 雾色和混合权重 A，可直接交给 applySceneFog。
vec4 sceneFogAt(vec3 position, bool terrainFallback) {
    // 用相机到表面的欧氏距离驱动雾量，起雾距离与投影的深度分布无关。
    vec3 ray = position - ubo.cameraPosition.xyz;
    float normalizedDistance = length(ray);
    if (normalizedDistance <= 0.0) return vec4(0.0);
    float distance = normalizedDistance / max(ubo.cameraPosition.w, 1e-8);
    vec3 direction = ray / normalizedDistance;
    vec4 haze = distanceFogAt(direction, distance, terrainFallback);
    vec4 mist = sceneMistAt(direction, distance, distance);
    if (mist.a <= 0.0) return haze;

    // 将浮雾叠在距离雾前面。按覆盖率合成后除以总覆盖率，得到供 mix 使用的非预乘颜色。
    float transmittedHaze = haze.a * (1.0 - mist.a);
    float amount = transmittedHaze + mist.a;
    return vec4((haze.rgb * transmittedHaze + mist.rgb * mist.a) / amount, amount);
}

vec4 sceneFogAt(vec3 position) {
    return sceneFogAt(position, false);
}

vec3 applySceneFog(vec3 color, vec4 fog) {
    return mix(color, fog.rgb, fog.a);
}
