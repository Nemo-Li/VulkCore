// Lightweight single-scattering approximation. Keep the authored blue in
// the sky texture, and use these phase functions to add directional light.
float rayleighPhase(float cosine) {
    return 0.75 * (1.0 + cosine * cosine);
}

float miePhase(float cosine, float anisotropy) {
    // Henyey–Greenstein, normalized to one when looking straight at the sun.
    float forward = 1.0 - anisotropy;
    return pow((forward * forward) /
        max(1.0 + anisotropy * anisotropy - 2.0 * anisotropy * cosine, 1e-4), 1.5);
}

vec3 atmosphereInscatter(vec3 ray, vec3 sunDirection, vec3 sunlight) {
    float cosine = clamp(dot(ray, sunDirection), -1.0, 1.0);
    // The denser aerosol layer gives the horizon more haze than the zenith.
    float elevation = max(ray.y, 0.0);
    float molecularPath = 1.0 / (1.0 + 3.0 * elevation);
    float aerosolPath = 1.0 / (1.0 + 9.0 * elevation);
    float sunVisibility = smoothstep(-0.10, 0.05, sunDirection.y);
    vec3 molecular = vec3(0.013, 0.034, 0.072) * rayleighPhase(cosine) * molecularPath;
    vec3 aerosol = vec3(0.36, 0.28, 0.19) * miePhase(cosine, 0.78) * aerosolPath;
    return (molecular + aerosol) * sunlight * sunVisibility;
}
