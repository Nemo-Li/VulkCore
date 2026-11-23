// XZ is the unnormalized terrain coordinate in both the terrain and grass shaders.
layout(std430, set = 0, binding = 11) readonly buffer TerrainRoadIndex {
    uint roadWords[];
};

void compositeRoad(inout vec3 weights, inout float remaining, uint surface, float coverage) {
    if (surface != 0u) weights[surface - 1u] += remaining * coverage;
    remaining *= 1.0 - coverage;
}

vec3 terrainRoadWeights(vec2 position) {
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
    float coverage = 0.0;
    uint stroke = 0xffffffffu;
    uint surface = 0u;
    // Newer strokes cover older ones. Segments of one stroke share a single
    // feathered edge, so pointer sampling rate does not change its opacity.
    for (uint i = 0u; i < count; ++i) {
        uint segment = roadWords[first + i];
        uint id = roadWords[segment + 6u];
        if (id != stroke) {
            compositeRoad(weights, remaining, surface, coverage);
            if (remaining <= 0.0) return weights;
            stroke = id;
            surface = roadWords[segment + 5u];
            coverage = 0.0;
        }
        if (coverage >= 1.0) continue;
        vec2 a = vec2(uintBitsToFloat(roadWords[segment]), uintBitsToFloat(roadWords[segment + 1u]));
        vec2 b = vec2(uintBitsToFloat(roadWords[segment + 2u]), uintBitsToFloat(roadWords[segment + 3u]));
        vec2 span = b - a;
        vec2 offset = position - a;
        float t = clamp(dot(offset, span) / max(dot(span, span), 1e-6), 0.0, 1.0);
        float radius = uintBitsToFloat(roadWords[segment + 4u]);
        coverage = max(coverage, 1.0 - smoothstep(radius * 0.72, radius, length(offset - t * span)));
    }
    compositeRoad(weights, remaining, surface, coverage);
    return weights;
}
