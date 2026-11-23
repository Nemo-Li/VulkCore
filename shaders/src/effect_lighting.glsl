#include "scene_frame.glsl"

vec3 effectPointLighting(vec3 position, vec3 normal) {
    vec3 result = vec3(0.0);
    float sceneScale = max(ubo.cameraPosition.w, 1e-6);
    for (int i = 0; i < int(ubo.effectLightCount.x); ++i) {
        vec4 light = ubo.effectLightPosition[i];
        vec3 offset = light.xyz - position;
        float distanceToLight = length(offset);
        if (light.w > 0.0 && distanceToLight >= light.w) continue;
        float cosine = max(dot(normal, offset / max(distanceToLight, 1e-6)), 0.0);
        vec4 radiance = ubo.effectLightColor[i];
        float attenuation = 1.0 / max(pow(distanceToLight / sceneScale, radiance.w), 0.01);
        if (light.w > 0.0) {
            float cutoff = clamp(1.0 - pow(distanceToLight / light.w, 4.0), 0.0, 1.0);
            attenuation *= cutoff * cutoff;
        }
        result += radiance.rgb * (cosine * attenuation / 3.14159265);
    }
    return result;
}
