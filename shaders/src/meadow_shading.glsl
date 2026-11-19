// Shared linear RGB and diffuse response for mesh blades and textured meadow ground.
const vec3 kMeadowAlbedo = vec3(0.245, 0.435, 0.086);

float meadowDiffuse(float facing) {
    return smoothstep(-0.24, 0.82, facing);
}
