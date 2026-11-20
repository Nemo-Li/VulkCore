#version 450
#extension GL_GOOGLE_include_directive : require

#include "scene_fog.glsl"

layout(early_fragment_tests) in;
layout(location = 0) in vec3 fragWorldRay;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 1) uniform sampler2D skyboxTexture;

const float PI = 3.14159265358979323846;

float cloudDensity(vec2 p, float cover, float footprint, bool highLayer) {
    float detailFade = 1.0 - smoothstep(0.025, 0.16, footprint);
    // Offset the scales separately to let the wisps shear as they travel.
    vec2 shear = ubo.daylight.y * vec2(0.004, -0.002);
    vec2 offset = vec2(7.3, -13.5);
    float billow = cloudNoise(p * 2.09 + offset + shear) - 0.5;
    float breakup = cloudNoise(p * 5.21 - offset - shear * 1.8) - 0.5;
    float wisps = cloudNoise(p * vec2(8.0, 15.1) + offset) - 0.5;
    float shape = cover + 0.30 * billow + detailFade * (0.17 * breakup + 0.10 * wisps);
    return smoothstep(highLayer ? 0.62 : 0.55, highLayer ? 0.88 : 0.83, shape);
}

vec3 compositeCloudLayer(vec3 background, vec3 camera, vec3 ray, vec3 haze,
                         vec3 sun, vec3 sunlight, float sunCosine, bool highLayer) {
    float height = (highLayer ? kHighCloudHeight : kLowCloudHeight) - camera.y;
    float distanceToLayer = height / max(ray.y, 0.001);
    vec2 p = cloudCoordinates(camera.xz + ray.xz * min(distanceToLayer, 16000.0), highLayer);
    float footprint = length(fwidth(p));
    if (ray.y <= 0.0 || distanceToLayer >= 16000.0) return background;

    float cover = cloudCoverage(p);
    if (cover < (highLayer ? 0.33 : 0.26)) return background;
    float density = cloudDensity(p, cover, footprint, highLayer);
    float horizonFade = smoothstep(0.015, 0.075, ray.y) *
        (1.0 - smoothstep(4800.0, 16000.0, distanceToLayer));
    // Beer–Lambert extinction makes thin edges transmit the painted blue.
    float path = mix(1.85, 1.04, sqrt(ray.y));
    float opacity = (1.0 - exp(-density * path)) * horizonFade * (highLayer ? 0.46 : 0.90);

    vec2 sunOffset = vec2(dot(sun.xz, kCloudWind), dot(sun.xz, vec2(-kCloudWind.y, kCloudWind.x)));
    float sunwardCover = cloudCoverage(p + sunOffset * 0.26);
    float edgeLight = smoothstep(0.0, 0.23, cover - sunwardCover) * smoothstep(0.04, 0.25, sun.y);
    float transmission = exp(-2.0 * density);
    float backlight = miePhase(sunCosine, 0.69) * (0.11 + 0.32 * transmission);
    vec3 ambient = vec3(0.28, 0.35, 0.40) + ubo.ambientLight.rgb * ubo.ambientLight.a * 0.48;
    vec3 direct = sunlight * (0.28 + 0.27 * transmission + 0.35 * edgeLight + backlight);
    vec3 cloud = ambient + direct;
    cloud = mix(cloud, haze, 0.58 * (1.0 - exp2(-distanceToLayer / 5300.0)));
    return mix(background, cloud, opacity);
}

void main() {
    vec3 ray = normalize(fragWorldRay);
    vec3 sun = normalize(ubo.sunLightDirection.xyz);
    vec3 sunlight = ubo.sunLightColor.rgb * ubo.sunLightDirection.w;
    vec3 camera = daylightWorldPosition(ubo.cameraPosition.xyz);
    float sunCosine = clamp(dot(ray, sun), -1.0, 1.0);

    // The cloudless texture is constant along longitude. Sample one column
    // to preserve its full latitude palette without a longitude seam.
    float latitude = acos(clamp(ray.y, -1.0, 1.0)) / PI;
    vec3 palette = texture(skyboxTexture, vec2(0.5, latitude)).rgb;
    vec3 scatter = atmosphereInscatter(ray, sun, sunlight);
    vec3 color = palette + scatter;
    vec3 haze = mix(palette, vec3(0.49, 0.69, 0.80), 0.43) + scatter;

    // Draw the solar disk in sky space so clouds can veil both it and its halo.
    float sunDistance = length(ray - sun);
    float edge = max(0.001, fwidth(sunDistance));
    float disk = 1.0 - smoothstep(0.009 - edge, 0.009 + edge, sunDistance);
    float sunVisibility = smoothstep(-0.03, 0.03, sun.y);
    color += sunlight * vec3(1.0, 0.91, 0.73) * sunVisibility *
        (2.8 * disk + 0.18 * miePhase(sunCosine, 0.91));

    color = compositeCloudLayer(color, camera, ray, haze, sun, sunlight * sunVisibility, sunCosine, true);
    color = compositeCloudLayer(color, camera, ray, haze, sun, sunlight * sunVisibility, sunCosine, false);
    outColor = vec4(applySceneFog(color, floatingMistAt(ray, 1600.0)), 1.0);
}
