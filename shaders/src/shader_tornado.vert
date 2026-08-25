#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 time;
    vec4 viewport;
    vec4 ambientLight;
    vec4 sunLightDirection;
    vec4 sunLightColor;
    vec4 cameraPosition;
} ubo;

layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform;
    vec4 baseColor;
    uint textureFlags;
    float metallic;
    float roughness;
    float opacity;
    vec4 waterFlow;
    vec4 waterSurface;
} material;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec4 fragVertexColor;
layout(location = 3) out vec3 fragWorldPosition;
layout(location = 4) out vec3 fragLocalPosition;
layout(location = 5) out vec3 fragLayerMotion;

const float TAU = 6.28318530718;

mat2 rotation2d(float angle) {
    float sine = sin(angle);
    float cosine = cos(angle);
    return mat2(cosine, -sine, sine, cosine);
}

float hash11(float value) {
    return fract(sin(value * 127.1 + 311.7) * 43758.5453123);
}

vec2 analyticFlow(vec2 point) {
    return vec2(
        sin(point.x * 2.31 + point.y * 1.17),
        cos(point.x * 1.43 - point.y * 2.73)
    );
}

vec2 flowSample(
    vec2 coordinate,
    float seed,
    float elapsed,
    vec2 velocity,
    float frequency
) {
    vec2 firstUv = coordinate * frequency + velocity * elapsed +
        vec2(seed * 0.071, seed * 0.113);
    vec2 secondUv = coordinate.yx * (frequency * 1.91) -
        velocity.yx * elapsed * 0.73 + vec2(seed * 0.137, -seed * 0.053);
    // Geometry uses a continuous analytic field. Texture-driven high
    // frequencies are intentionally reserved for the fragment stage so the
    // ribbon silhouette never inherits texel-scale saw teeth.
    return analyticFlow(firstUv * TAU * 0.43) * 0.68 +
        analyticFlow(secondUv * TAU * 0.31) * 0.32;
}

void main() {
    float kind = floor(inTexCoord.y * 0.5 + 0.0001);
    float seed = floor(inTexCoord.x * 0.5 + 0.0001);
    float along = fract(inTexCoord.x);
    float across = clamp(inTexCoord.y - kind * 2.0, 0.0, 1.0);
    float heightRatio = clamp(inPosition.y / 6.0, 0.0, 1.0);
    float elapsed = ubo.time.x;
    float randomPhase = hash11(seed) * TAU;

    vec3 localPosition = inPosition;
    vec3 localNormal = inNormal;
    vec2 flow = vec2(0.0);
    float angleWarp = 0.0;
    float brightnessPulse = 0.0;

    if (kind < 0.5) {
        // Volume core: two nested skins continuously shear at different
        // heights. Its centerline wanders while the radius inhales in patches.
        flow = flowSample(
            vec2(fract(inTexCoord.x), heightRatio),
            seed,
            elapsed,
            vec2(0.037 + seed * 0.006, -0.052),
            1.35 + seed * 0.21
        );
        float tallWave = sin(
            heightRatio * TAU * 1.72 - elapsed * 0.91 + randomPhase
        );
        float crossWave = sin(
            heightRatio * TAU * 3.63 + elapsed * 1.27 + flow.y * 1.8
        );
        angleWarp =
            tallWave * 0.19 + crossWave * 0.075 + flow.x * 0.18;
        float breathing = 1.0 +
            flow.y * 0.032 +
            sin(elapsed * 1.43 - heightRatio * 10.0 + randomPhase) * 0.028;
        localPosition.xz *= breathing;
        vec2 centerDrift = vec2(
            sin(elapsed * 0.73 + heightRatio * 5.1 + randomPhase),
            cos(elapsed * 0.61 - heightRatio * 4.2 + randomPhase)
        );
        localPosition.xz += centerDrift * (0.025 + heightRatio * 0.050);
        localPosition.y += flow.y * 0.028 * smoothstep(0.08, 0.92, heightRatio);
        brightnessPulse = 0.5 + 0.5 * tallWave;
    } else if (kind < 1.5) {
        // Hero ribbons: a low-frequency domain warp bends the whole blade,
        // while a travelling high-frequency wave ripples different sections.
        float direction = mod(seed, 2.0) < 0.5 ? -1.0 : 1.0;
        float speedJitter = mix(0.72, 1.28, hash11(seed + 9.0));
        flow = flowSample(
            vec2(along, 0.37 + seed * 0.013),
            seed,
            elapsed,
            vec2(direction * 0.031, 0.021 * speedJitter),
            1.18 + mod(seed, 4.0) * 0.17
        );
        float travellingWave = sin(
            along * TAU * (1.35 + mod(seed, 3.0) * 0.31) -
            elapsed * direction * (1.05 + speedJitter * 0.48) +
            randomPhase
        );
        float secondaryWave = sin(
            along * TAU * 3.7 + elapsed * 1.81 + flow.x * 2.4 + seed
        );
        angleWarp =
            direction * elapsed * (0.055 + hash11(seed + 3.0) * 0.055) +
            travellingWave * 0.17 +
            secondaryWave * 0.055 +
            flow.x * 0.20;
        localPosition.xz *= 1.0 + flow.y * 0.038 + travellingWave * 0.021;
        localPosition.y +=
            flow.y * 0.045 +
            secondaryWave * 0.024 * sin(across * 3.14159265);
        brightnessPulse = 0.5 + 0.5 * secondaryWave;
    } else if (kind < 2.5) {
        // Thin wisps: quick snake-like waves and turbulent radial flutter.
        float direction = mod(seed, 3.0) < 1.0 ? -1.0 : 1.0;
        flow = flowSample(
            vec2(along * 1.7, seed * 0.019),
            seed,
            elapsed,
            vec2(direction * 0.083, -0.046),
            1.72 + mod(seed, 5.0) * 0.13
        );
        float snake = sin(
            along * TAU * (2.6 + mod(seed, 4.0) * 0.37) -
            elapsed * direction * (2.15 + hash11(seed) * 0.8) +
            randomPhase
        );
        float tremor = sin(
            along * TAU * 7.3 + elapsed * 3.4 + flow.y * 3.0
        );
        angleWarp =
            direction * elapsed * 0.035 +
            snake * 0.21 +
            tremor * 0.045 +
            flow.x * 0.25;
        localPosition.xz *= 1.0 + flow.y * 0.045 + snake * 0.025;
        localPosition.y += flow.x * 0.038 + tremor * 0.018;
        brightnessPulse = 0.5 + 0.5 * tremor;
    } else if (kind < 3.5) {
        // Impact rings stay anchored: travelling pressure waves change radius,
        // height and ellipticity instead of spinning the whole decal.
        float polarAngle = atan(inPosition.z, inPosition.x) / TAU + 0.5;
        flow = flowSample(
            vec2(polarAngle, seed * 0.023),
            seed,
            elapsed,
            vec2(0.019, -0.067 - mod(seed, 3.0) * 0.009),
            1.54 + mod(seed, 4.0) * 0.19
        );
        float pressureWave = sin(
            polarAngle * TAU * (3.0 + mod(seed, 3.0)) -
            elapsed * (2.05 + mod(seed, 2.0) * 0.42) +
            randomPhase
        );
        float afterShock = sin(
            polarAngle * TAU * 7.0 + elapsed * 3.1 + flow.x * 2.6
        );
        float radialPulse = 1.0 +
            pressureWave * 0.035 +
            afterShock * 0.014 +
            flow.y * 0.022;
        localPosition.xz *= radialPulse;
        localPosition.x *= 1.0 + flow.x * 0.018;
        localPosition.z *= 1.0 - flow.x * 0.018;
        localPosition.y += pressureWave * 0.020 + flow.y * 0.012;
        angleWarp = flow.x * 0.018;
        brightnessPulse = 0.5 + 0.5 * pressureWave;
    } else {
        // Detached shards flutter independently. Every crossed pair shares its
        // seed, so the spark remains coherent while following a chaotic orbit.
        flow = flowSample(
            vec2(seed * 0.037, seed * 0.071),
            seed,
            elapsed,
            vec2(0.073 + hash11(seed) * 0.041, -0.057),
            2.2
        );
        float flutter = sin(elapsed * (2.2 + hash11(seed + 1.0) * 2.7) + randomPhase);
        float bob = sin(elapsed * (1.1 + hash11(seed + 2.0)) + randomPhase * 1.7);
        angleWarp = flow.x * 0.26 + flutter * 0.075;
        localPosition.xz *= 1.0 + flow.y * 0.045;
        localPosition.y += bob * 0.075 + flow.y * 0.055;
        brightnessPulse = 0.5 + 0.5 * flutter;
    }

    localPosition.xz = rotation2d(angleWarp) * localPosition.xz;
    localNormal.xz = rotation2d(angleWarp) * localNormal.xz;

    mat4 model = ubo.model * material.nodeTransform;
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    vec3 worldPosition = (model * vec4(localPosition, 1.0)).xyz;
    gl_Position = ubo.projection * ubo.view * vec4(worldPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragNormal = normalize(normalMatrix * localNormal);
    fragVertexColor = inColor;
    fragWorldPosition = worldPosition;
    fragLocalPosition = localPosition;
    fragLayerMotion = vec3(flow, brightnessPulse);
}
