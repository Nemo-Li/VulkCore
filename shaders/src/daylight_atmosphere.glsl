// 晴天空气的轻量单次散射近似。在天空贴图的底色上叠加随视线与太阳夹角变化的入射光。
// Rayleigh 相函数的 1 + cos²θ 给出前后对称的角度分布。颜色权重在下方单独控制。
float rayleighPhase(float cosine) {
    return 0.75 * (1.0 + cosine * cosine);
}

float miePhase(float cosine, float anisotropy) {
    // Henyey–Greenstein 前向散射。正的各向异性系数将能量集中到太阳附近，归一化使正对太阳时为 1。
    float forward = 1.0 - anisotropy;
    return pow((forward * forward) /
        max(1.0 + anisotropy * anisotropy - 2.0 * anisotropy * cosine, 1e-4), 1.5);
}

vec3 atmosphereInscatter(vec3 ray, vec3 sunDirection, vec3 sunlight) {
    float cosine = clamp(dot(ray, sunDirection), -1.0, 1.0);
    // 低空的气溶胶更集中，因此随仰角缩短得比空气分子的光程更快。
    // RGB 权重近似 Rayleigh 的蓝色偏移和 Mie 的暖色光晕。
    float elevation = max(ray.y, 0.0);
    float molecularPath = 1.0 / (1.0 + 3.0 * elevation);
    float aerosolPath = 1.0 / (1.0 + 9.0 * elevation);
    float sunVisibility = smoothstep(-0.10, 0.05, sunDirection.y);
    vec3 molecular = vec3(0.013, 0.034, 0.072) * rayleighPhase(cosine) * molecularPath;
    vec3 aerosol = vec3(0.36, 0.28, 0.19) * miePhase(cosine, 0.78) * aerosolPath;
    return (molecular + aerosol) * sunlight * sunVisibility;
}
