// Included after the scene UBO. All heights and wind speeds use the source
// map's units so cloud parallax and projected shadows survive normalization.
const float kLowCloudHeight = 940.0;
const float kHighCloudHeight = 1530.0;
const vec2 kCloudWind = vec2(0.821, 0.571);

vec3 daylightWorldPosition(vec3 scenePosition) {
    return (scenePosition - ubo.model[3].xyz) / max(ubo.cameraPosition.w, 1e-8);
}

float cloudHash(vec2 cell) {
    vec3 p = fract(vec3(cell.xyx) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float cloudNoise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(cloudHash(cell), cloudHash(cell + vec2(1, 0)), f.x),
               mix(cloudHash(cell + vec2(0, 1)), cloudHash(cell + vec2(1, 1)), f.x), f.y);
}

vec2 cloudCoordinates(vec2 worldPosition, bool highLayer) {
    vec2 acrossWind = vec2(-kCloudWind.y, kCloudWind.x);
    vec2 p = vec2(dot(worldPosition, kCloudWind), dot(worldPosition, acrossWind));
    p.x -= ubo.daylight.y * (highLayer ? 19.0 : 13.0);
    if (highLayer) return p / vec2(1140.0, 425.0) + vec2(-39.0, 24.0);
    return p / vec2(650.0, 460.0) + vec2(2.8, 6.5);
}

float cloudCoverage(vec2 p) {
    // A broad weather mask breaks up neighboring cloud banks with blue gaps.
    return cloudNoise(p) * 0.77 + cloudNoise(p * 0.31 + vec2(38.4, 1.7)) * 0.23;
}

float cloudSunVisibility(vec3 scenePosition) {
    if (ubo.daylight.x == 0.0 || ubo.sunLightDirection.y < 0.08 ||
        ubo.sunLightDirection.w <= 0.0) return 1.0;

    vec3 position = daylightWorldPosition(scenePosition);
    float height = kLowCloudHeight - position.y;
    if (height <= 0.0) return 1.0;
    vec2 lightSlope = ubo.sunLightDirection.xz / ubo.sunLightDirection.y;
    // Use the same moving coverage as the low layer and soften the footprint.
    vec2 p = cloudCoordinates(position.xz + lightSlope * height, false);
    float cover = smoothstep(0.54, 0.79, cloudCoverage(p));
    float strength = smoothstep(0.08, 0.26, ubo.sunLightDirection.y);
    return 1.0 - 0.26 * cover * strength;
}
