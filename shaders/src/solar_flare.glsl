// All lengths use screen height as the unit, keeping disks round at any aspect ratio.
float softGlow(float radius, float scale) {
    float t = radius / scale;
    return exp2(-t * t);
}

float flareGhost(vec2 offset, float radius) {
    float r = length(offset) / radius;
    float fill = 1.0 - smoothstep(0.72, 1.02, r);
    float rim = smoothstep(0.76, 0.93, r) * (1.0 - smoothstep(0.94, 1.06, r));
    return fill * (0.38 + 0.62 * softGlow(r, 0.54)) + 0.19 * rim;
}

vec3 solarFlareRadiance(vec2 uv, vec4 flare) {
    if (flare.w <= 0.0) return vec3(0.0);
    vec2 metric = vec2(flare.z, 1.0);
    vec2 fromSun = (uv - flare.xy) * metric;
    vec2 toCenter = (vec2(0.5) - flare.xy) * metric;
    float radius = length(fromSun);

    // A warm veiling halo surrounds the overexposed core and softens the roots of the spikes.
    float glare = 0.41 * softGlow(radius, 0.031) + 0.22 * softGlow(radius, 0.090);
    if (radius < 0.37) {
        // Angle (radians), length, angular spread, and amplitude of each diffraction spike.
        const vec4 spikes[] = vec4[](
            vec4(-2.81, 0.205, 0.088, 0.24), vec4(-2.20, 0.180, 0.106, 0.49),
            vec4(-1.42, 0.197, 0.074, 0.39), vec4(-0.59, 0.205, 0.132, 0.41),
            vec4( 0.23, 0.176, 0.113, 0.47), vec4( 0.92, 0.194, 0.046, 0.25),
            vec4( 1.77, 0.216, 0.099, 0.47), vec4( 2.45, 0.162, 0.083, 0.32));
        float rays = 0.0;
        for (int i = 0; i < spikes.length(); ++i) {
            vec4 spike = spikes[i];
            vec2 axis = vec2(cos(spike.x), sin(spike.x));
            float along = max(dot(fromSun, axis), 0.0);
            float across = abs(dot(fromSun, vec2(-axis.y, axis.x)));
            float taper = 1.0 - smoothstep(0.025, spike.y + 0.12, along);
            rays += spike.w * smoothstep(0.0, 0.025, along) * taper *
                softGlow(across, 0.004 + spike.z * along) * softGlow(along, spike.y);
        }
        glare += rays;
    }
    vec3 light = vec3(1.00, 0.91, 0.67) * glare;
    light += vec3(1.00, 0.81, 0.56) * (0.37 * softGlow(radius, 0.274));

    // Internal lens reflections move along the optical axis as the camera turns.
    float eccentricity = smoothstep(0.025, 0.19, length(toCenter));
    vec3 ghosts = vec3(0.14, 0.49, 0.61) * flareGhost(fromSun - toCenter * 0.38, 0.052);
    ghosts += vec3(0.25, 0.21, 0.52) * flareGhost(fromSun - toCenter * 0.59, 0.038);
    ghosts += vec3(0.10, 0.28, 0.23) * flareGhost(fromSun - toCenter * 0.85, 0.053);
    ghosts += vec3(0.16, 0.11, 0.22) * flareGhost(fromSun - toCenter * 1.25, 0.030);
    return (light + ghosts * (0.40 * eccentricity)) * flare.w;
}
