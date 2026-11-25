// Procedural height fields for the rock finish. The RGB texture still
// supplies the material color. Height and gradient use the same field.
float rockHash(ivec3 cell) {
    uvec3 key = uvec3(cell);
    uint hash = key.x * 0x8da6b343u ^ key.y * 0xd8163841u ^ key.z * 0xcb1ab31fu;
    hash = (hash ^ (hash >> 16u)) * 0x7feb352du;
    hash = (hash ^ (hash >> 15u)) * 0x846ca68bu;
    return float((hash ^ (hash >> 16u)) >> 8u) / 16777215.0;
}

// (height, dHeight/dX, dHeight/dY) on one projection plane.
vec3 rockNoise2D(vec2 position, int seed) {
    ivec3 cell = ivec3(ivec2(floor(position)), seed);
    vec2 f = fract(position);
    vec2 t = f * f * (3.0 - 2.0 * f);
    vec2 dt = 6.0 * f * (1.0 - f);
    float a = rockHash(cell);
    float b = rockHash(cell + ivec3(1, 0, 0));
    float c = rockHash(cell + ivec3(0, 1, 0));
    float d = rockHash(cell + ivec3(1, 1, 0));
    float low = mix(a, b, t.x);
    float high = mix(c, d, t.x);
    return vec3(mix(low, high, t.y), mix(b - a, d - c, t.y) * dt.x, (high - low) * dt.y);
}

vec3 rockProjectionWeights(vec3 normal, float rock) {
    vec3 weights = abs(normal);
    weights *= weights;
    weights *= weights;
    weights /= dot(weights, vec3(1.0));
    weights = mix(vec3(0.0, 1.0, 0.0), weights, rock);
    weights *= smoothstep(vec3(0.005), vec3(0.02), weights);
    return weights / dot(weights, vec3(1.0));
}

vec3 rockGrainGradient(vec3 position, vec3 weights) {
    // Triplanar projection supplies height gradients on vertical cliffs
    // as well as on top-facing surfaces. Convert each to terrain axes.
    vec3 gradient = vec3(0.0);
    if (weights.x > 0.0) {
        vec2 slope = rockNoise2D(position.yz, 11).yz;
        gradient += weights.x * vec3(0.0, slope);
    }
    if (weights.y > 0.0) {
        vec2 slope = rockNoise2D(position.xz, 23).yz;
        gradient += weights.y * vec3(slope.x, 0.0, slope.y);
    }
    if (weights.z > 0.0) {
        vec2 slope = rockNoise2D(position.xy, 37).yz;
        gradient += weights.z * vec3(slope, 0.0);
    }
    return gradient;
}

// (height, gradient) for non-repeating strata in three dimensions.
vec4 rockNoise3D(vec3 position) {
    ivec3 cell = ivec3(floor(position));
    vec3 f = fract(position);
    vec3 t = f * f * (3.0 - 2.0 * f);
    vec3 dt = 6.0 * f * (1.0 - f);
    float a = rockHash(cell);
    float b = rockHash(cell + ivec3(1, 0, 0));
    float c = rockHash(cell + ivec3(0, 1, 0));
    float d = rockHash(cell + ivec3(1, 1, 0));
    float e = rockHash(cell + ivec3(0, 0, 1));
    float f1 = rockHash(cell + ivec3(1, 0, 1));
    float g = rockHash(cell + ivec3(0, 1, 1));
    float h = rockHash(cell + ivec3(1, 1, 1));
    float lowY0 = mix(a, b, t.x);
    float lowY1 = mix(c, d, t.x);
    float highY0 = mix(e, f1, t.x);
    float highY1 = mix(g, h, t.x);
    float lowZ = mix(lowY0, lowY1, t.y);
    float highZ = mix(highY0, highY1, t.y);
    vec3 gradient = vec3(
        mix(mix(b - a, d - c, t.y), mix(f1 - e, h - g, t.y), t.z),
        mix(lowY1 - lowY0, highY1 - highY0, t.z),
        highZ - lowZ
    ) * dt;
    return vec4(mix(lowZ, highZ, t.z), gradient);
}

const vec3 rockStrataFrequency = vec3(0.12, 0.34, 0.12);
const vec3 rockChipsFrequency = vec3(0.38, 0.62, 0.38);
const float rockGrainFrequency = 2.4;

float rockBandFade(vec3 frequency, vec3 dx, vec3 dy) {
    float footprint = max(length(dx * frequency), length(dy * frequency));
    return 1.0 - smoothstep(0.26, 0.8, footprint);
}

void applyRockBand(vec4 field, vec3 frequency, float weight, vec3 strength,
    inout vec3 gradient, inout vec3 albedo, inout float ambientVisibility) {
    gradient += weight * strength.x * field.yzw * frequency;
    albedo *= 1.0 + weight * strength.y * (field.x - 0.5);
    ambientVisibility -= weight * strength.z * (1.0 - smoothstep(0.18, 0.56, field.x));
}
