#include "scene_frame.glsl"
#include "solar_disk.glsl"

vec4 projectSolarFlare(sampler2D sceneColor, sampler2D sceneDepth) {
    float intensity = min(ubo.sunLightDirection.w, 1.5) * solarElevationFade(ubo.sunLightDirection.y);
    if (ubo.daylight.x < 0.5 || intensity <= 0.0) return vec4(0.0);

    // The daylight sun is at infinity, so camera translation does not affect its projection.
    vec4 clip = ubo.projection * (ubo.view * vec4(normalize(ubo.sunLightDirection.xyz), 0.0));
    if (clip.w <= 0.0) return vec4(0.0);
    vec2 sun = 0.5 * (clip.xy / clip.w + 1.0);
    float aspect = ubo.viewport.x * ubo.viewport.w;
    vec2 edge = min(sun, 1.0 - sun) * vec2(aspect, 1.0);
    float frameFade = smoothstep(0.012, 0.085, min(edge.x, edge.y));
    if (frameFade <= 0.0) return vec4(0.0);

    // Probe the disk only at the fullscreen vertices. Opaque objects can cover part of the disk,
    // and its luminance fades the effect when clouds or mist cover the remaining sky pixels.
    const vec2 taps[] = vec2[](vec2(0.0), vec2(1.0, 0.0), vec2(-1.0, 0.0),
        vec2(0.0, 1.0), vec2(0.0, -1.0), vec2(0.707, 0.707), vec2(-0.707, -0.707));
    float diskRadius = 0.5 * abs(ubo.projection[1][1]) * solarDiskRadius / clip.w;
    vec2 probeRadius = 0.62 * diskRadius * vec2(1.0 / aspect, 1.0);
    ivec2 size = textureSize(sceneDepth, 0);
    float visibility = 0.0;
    for (int i = 0; i < taps.length(); ++i) {
        ivec2 pixel = clamp(ivec2((sun + taps[i] * probeRadius) * vec2(size)), ivec2(0), size - 1);
        if (texelFetch(sceneDepth, pixel, 0).r >= 1.0) {
            vec3 color = texelFetch(sceneColor, pixel, 0).rgb;
            float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
            visibility += smoothstep(0.70, 0.98, luminance);
        }
    }
    return vec4(sun, aspect, intensity * frameFade * visibility / float(taps.length()));
}
