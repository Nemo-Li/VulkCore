// Shared exposure keeps the supplied albedos in the scene's existing lighting range.
const float kMeadowTextureExposure = 2.8;

float meadowDiffuse(float facing) {
    return smoothstep(-0.24, 0.82, facing);
}
