#ifndef VULKCORE_SOLAR_DISK_GLSL
#define VULKCORE_SOLAR_DISK_GLSL

// Chord radius on the unit sphere, shared by the sky and the visibility probe.
const float solarDiskRadius = 0.014;

float solarElevationFade(float elevation) {
    return smoothstep(-0.03, 0.03, elevation);
}

#endif
