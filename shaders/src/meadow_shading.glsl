// Shared exposure keeps the supplied albedos in the scene's existing lighting range.
const float kMeadowTextureExposure = 2.8;

float meadowDiffuse(float facing) {
    return smoothstep(-0.24, 0.82, facing);
}

float meadowCanopyDiffuse(vec3 normal, vec3 lightDirection) {
    return mix(0.25, 0.84, meadowDiffuse(dot(normal, lightDirection)));
}

vec3 meadowCanopyAlbedo(vec3 mapColor) {
    // Mean leaf brightness and root-to-tip shade for the distant canopy.
    return mapColor * (kMeadowTextureExposure * 0.92);
}
