// XZ is the unnormalized terrain coordinate in both the terrain and grass shaders.
layout(std430, set = 0, binding = 11) readonly buffer TerrainRoadIndex {
    uint roadWords[];
};

float roadGrassCell(ivec2 cell) {
    uint seed = uint(cell.x) * 0x9E3779B9u ^ uint(cell.y) * 0x85EBCA6Bu ^ 0xD2B74407u;
    seed = (seed ^ (seed >> 16u)) * 0x7FEB352Du;
    seed = (seed ^ (seed >> 15u)) * 0x846CA68Bu;
    return float((seed ^ (seed >> 16u)) & 0xFFFFu) / 65535.0;
}

float roadGrassNoise(vec2 position) {
    ivec2 cell = ivec2(floor(position));
    vec2 f = smoothstep(vec2(0.0), vec2(1.0), fract(position));
    float low = mix(roadGrassCell(cell), roadGrassCell(cell + ivec2(1, 0)), f.x);
    float high = mix(roadGrassCell(cell + ivec2(0, 1)), roadGrassCell(cell + ivec2(1, 1)), f.x);
    return mix(low, high, f.y);
}

// The green ground patches and the short grass above them use the same field.
float roadGrassClusters(vec2 position) {
    float clumps = roadGrassNoise(position * 1.45 + vec2(16.7, -4.9));
    float edges = roadGrassNoise(position * 4.3 + vec2(-3.2, 9.6));
    return smoothstep(0.37, 0.65, mix(clumps, edges, 0.24));
}

// Keep the verge width in sync with the spatial index padding in RoadPaint.cpp.
const float kRoadGrassVergeWidth = 2.6;

void compositeRoad(inout vec3 weights, inout float remaining, inout float verge,
                   inout float vergeRemaining, uint surface, float coverage, float vergeCoverage,
                   float ochreVergeStrength) {
    if (surface == 0u) {
        vergeRemaining *= 1.0 - vergeCoverage;
    } else {
        weights[surface - 1u] += remaining * coverage;
        float vergeStrength = surface == 1u ? ochreVergeStrength : 1.0;
        verge = max(verge, vergeRemaining * vergeCoverage * vergeStrength);
    }
    remaining *= 1.0 - coverage;
}

// World-space width keeps continuous strokes independent of pointer sampling and scene reloads.
// Keep the profile and its maximum extent in sync with RoadPaint.cpp.
float dirtRoadRadius(float radius, vec2 center) {
    vec2 p = center / max(1.0, 0.65 * radius);
    return radius * (1.0 + 0.12 * sin(dot(p, vec2(0.44, 0.29)))
        + 0.07 * sin(dot(p, vec2(-0.79, 0.91)) + 1.4)
        + 0.03 * sin(dot(p, vec2(0.19, -0.16)) + 2.8));
}

vec3 terrainRoadWeights(vec2 position, bool sampleVerge, float ochreVergeStrength, out float verge) {
    verge = 0.0;
    ivec2 dimensions = ivec2(roadWords[0], roadWords[1]);
    if (dimensions.x == 0) return vec3(0.0);
    vec2 origin = vec2(uintBitsToFloat(roadWords[2]), uintBitsToFloat(roadWords[3]));
    ivec2 cell = ivec2(floor((position - origin) / uintBitsToFloat(roadWords[4])));
    if (any(lessThan(cell, ivec2(0))) || any(greaterThanEqual(cell, dimensions))) return vec3(0.0);
    uint cellOffset = 5u + 2u * uint(cell.y * dimensions.x + cell.x);
    uint first = roadWords[cellOffset];
    uint count = roadWords[cellOffset + 1u];
    vec3 weights = vec3(0.0);
    float remaining = 1.0;
    float vergeRemaining = 1.0;
    float coverage = 0.0;
    float vergeCoverage = 0.0;
    float padding = sampleVerge ? kRoadGrassVergeWidth : 0.0;
    uint stroke = 0xffffffffu;
    uint surface = 0u;
    // Newer strokes cover older ones. Segments of one stroke share a single
    // feathered edge, so pointer sampling rate does not change its opacity.
    for (uint i = 0u; i < count; ++i) {
        uint segment = roadWords[first + i];
        uint id = roadWords[segment + 6u];
        if (id != stroke) {
            compositeRoad(weights, remaining, verge, vergeRemaining, surface, coverage, vergeCoverage,
                          ochreVergeStrength);
            if (remaining <= 0.0 && (!sampleVerge || verge >= vergeRemaining)) return weights;
            stroke = id;
            surface = roadWords[segment + 5u];
            coverage = 0.0;
            vergeCoverage = 0.0;
        }
        if (coverage >= 1.0 && (!sampleVerge || vergeCoverage >= 1.0)) continue;
        vec2 a = vec2(uintBitsToFloat(roadWords[segment]), uintBitsToFloat(roadWords[segment + 1u]));
        vec2 b = vec2(uintBitsToFloat(roadWords[segment + 2u]), uintBitsToFloat(roadWords[segment + 3u]));
        vec2 span = b - a;
        vec2 offset = position - a;
        float t = clamp(dot(offset, span) / max(dot(span, span), 1e-6), 0.0, 1.0);
        float radius = uintBitsToFloat(roadWords[segment + 4u]);
        float distanceToCenter = length(offset - t * span);
        float coreFraction = 0.72;
        if (surface == 1u) {
            if (distanceToCenter >= 1.22 * radius + padding) continue;
            radius = dirtRoadRadius(radius, a + t * span);
            // Leave a wider shoulder for the local terrain and vegetation to show through.
            coreFraction = 0.46;
        }
        coverage = max(coverage, 1.0 - smoothstep(radius * coreFraction, radius, distanceToCenter));
        if (sampleVerge) {
            // Roadside grass uses distance in metres from the physical edge, not paint opacity.
            float edgeDistance = distanceToCenter - radius;
            // Erasing a stroke also fades its verge, including beyond the erased surface.
            float start = surface == 0u ? -0.28 * radius : 0.0;
            vergeCoverage = max(vergeCoverage, 1.0 - smoothstep(start, padding, edgeDistance));
        }
    }
    compositeRoad(weights, remaining, verge, vergeRemaining, surface, coverage, vergeCoverage,
                  ochreVergeStrength);
    return weights;
}

vec3 terrainRoadWeights(vec2 position) {
    float unusedVerge;
    return terrainRoadWeights(position, false, 0.0, unusedVerge);
}
