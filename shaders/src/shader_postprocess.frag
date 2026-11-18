#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model; mat4 view; mat4 projection; vec4 time; vec4 viewport;
    vec4 ambientLight; vec4 sunLightDirection; vec4 sunLightColor;
} ubo;
layout(set = 0, binding = 2) uniform sampler2D sceneColorTexture;
layout(set = 0, binding = 5) uniform sampler2D sceneDepthTexture;

// The first four fields intentionally match MaterialPushConstants so this
// pipeline uses the renderer's existing descriptor/push-constant ABI.
layout(push_constant) uniform PostProcessPushConstants {
    mat4 unusedNodeTransform;
    vec4 unusedBaseColor;
    uint effect;
    float strength;
    float parameter;
    float splitEnabled;
    vec4 options;
    vec4 unusedSurface;
} settings;

vec3 sampleColor(vec2 uv) {
    return texture(sceneColorTexture, clamp(uv, vec2(0.001), vec2(0.999))).rgb;
}

float sampleDepth(vec2 uv) {
    return texture(sceneDepthTexture, clamp(uv, vec2(0.001), vec2(0.999))).r;
}

float viewDistanceFromDepth(float depth) {
    // For the perspective matrix used by the renderer, clip-space z is:
    //   z_ndc = -projection[2][2] - projection[3][2] / viewZ
    // Solving this avoids an inverse matrix per fragment and keeps the blur
    // threshold in scene/view units instead of the non-linear depth range.
    float denominator = -ubo.projection[2][2] - depth;
    return abs(ubo.projection[3][2] / max(abs(denominator), 0.000001));
}

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 blur5(vec2 uv, float radius) {
    vec2 texel = ubo.viewport.zw * radius;
    vec3 color = sampleColor(uv) * 4.0;
    color += sampleColor(uv + vec2(texel.x, 0.0));
    color += sampleColor(uv - vec2(texel.x, 0.0));
    color += sampleColor(uv + vec2(0.0, texel.y));
    color += sampleColor(uv - vec2(0.0, texel.y));
    return color / 8.0;
}

vec3 neighborhoodAverage(vec2 uv, float radius) {
    vec2 texel = ubo.viewport.zw * radius;
    vec3 color = vec3(0.0);
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            color += sampleColor(uv + vec2(x, y) * texel);
        }
    }
    return color / 25.0;
}

float depthEdge(vec2 uv) {
    vec2 texel = ubo.viewport.zw * 1.5;
    float center = sampleDepth(uv);
    float edge = 0.0;
    edge += abs(center - sampleDepth(uv + vec2(texel.x, 0.0)));
    edge += abs(center - sampleDepth(uv - vec2(texel.x, 0.0)));
    edge += abs(center - sampleDepth(uv + vec2(0.0, texel.y)));
    edge += abs(center - sampleDepth(uv - vec2(0.0, texel.y)));
    return clamp(edge * 18.0, 0.0, 1.0);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec2 uv = fragTexCoord;
    vec2 split = vec2(clamp(settings.options.x, 0.05, 0.95), 0.0);
    vec3 original = sampleColor(uv);
    if (settings.splitEnabled > 0.5 && uv.x < split.x) {
        outColor = vec4(original, 1.0);
        return;
    }

    float strength = clamp(settings.strength, 0.0, 2.0);
    float parameter = clamp(settings.parameter, 0.0, 1.0);
    float depth = sampleDepth(uv);
    vec3 color = original;
    const uint FOG = 1u;
    const uint BOX_BLUR = 2u;
    const uint MEDIAN = 3u;
    const uint KUWAHARA = 4u;
    const uint BLOOM = 5u;
    const uint SSAO = 6u;
    const uint MOTION = 7u;
    const uint CHROMATIC = 8u;
    const uint SSR = 9u;
    const uint REFRACTION = 10u;
    const uint OUTLINE = 11u;
    const uint DOF = 12u;
    const uint POSTERIZE = 13u;
    const uint PIXELIZE = 14u;
    const uint SHARPEN = 15u;
    const uint DILATION = 16u;
    const uint GRAIN = 17u;
    const uint LUT = 18u;
    const uint GAMMA = 19u;
    const uint DEPTH = 20u;
    const uint EDGE = 21u;
    const uint DISTANT_BLUR = 22u;

    if (settings.effect == FOG) {
        float fog = smoothstep(0.18, mix(0.55, 0.98, parameter), depth) * strength;
        vec3 fogColor = mix(vec3(0.08, 0.13, 0.19), vec3(0.55, 0.72, 0.78), uv.y);
        color = mix(original, fogColor, clamp(fog, 0.0, 1.0));
    } else if (settings.effect == BOX_BLUR) {
        color = neighborhoodAverage(uv, mix(1.0, 5.0, parameter) * strength);
    } else if (settings.effect == MEDIAN) {
        vec2 texel = ubo.viewport.zw * mix(1.0, 3.0, parameter);
        vec3 a = sampleColor(uv - texel), b = sampleColor(uv + texel);
        vec3 c = sampleColor(uv + vec2(texel.x, -texel.y));
        color = (a + b + c + original) * 0.25;
        color = mix(color, original, 0.3);
    } else if (settings.effect == KUWAHARA) {
        vec3 q0 = neighborhoodAverage(uv + vec2(-1.0, -1.0) * ubo.viewport.zw * 2.0, 1.0);
        vec3 q1 = neighborhoodAverage(uv + vec2(1.0, -1.0) * ubo.viewport.zw * 2.0, 1.0);
        vec3 q2 = neighborhoodAverage(uv + vec2(-1.0, 1.0) * ubo.viewport.zw * 2.0, 1.0);
        vec3 q3 = neighborhoodAverage(uv + vec2(1.0, 1.0) * ubo.viewport.zw * 2.0, 1.0);
        float v0 = luminance(abs(q0 - original));
        float v1 = luminance(abs(q1 - original));
        float v2 = luminance(abs(q2 - original));
        float v3 = luminance(abs(q3 - original));
        color = q0;
        if (v1 < v0) color = q1;
        if (v2 < min(v0, v1)) color = q2;
        if (v3 < min(min(v0, v1), v2)) color = q3;
    } else if (settings.effect == BLOOM) {
        vec3 glow = max(neighborhoodAverage(uv, 3.0), vec3(0.0));
        float bright = smoothstep(0.55, 1.0, luminance(glow));
        color = original + glow * bright * (0.25 + strength * 0.75);
    } else if (settings.effect == SSAO) {
        vec2 texel = ubo.viewport.zw * mix(1.0, 5.0, parameter);
        float occlusion = 0.0;
        for (int i = 0; i < 8; ++i) {
            float angle = float(i) * 0.785398 + ubo.time.x * 0.15;
            vec2 sampleUv = uv + vec2(cos(angle), sin(angle)) * texel;
            occlusion += step(0.006 + depth * 0.015, sampleDepth(sampleUv) - depth);
        }
        color = original * (1.0 - occlusion / 8.0 * 0.72 * strength);
    } else if (settings.effect == MOTION) {
        vec2 velocity = vec2(sin(ubo.time.x * 0.7), cos(ubo.time.x * 0.53)) * ubo.viewport.zw * (2.0 + 12.0 * strength);
        color = vec3(0.0);
        for (int i = -4; i <= 4; ++i) color += sampleColor(uv + velocity * float(i) / 4.0);
        color /= 9.0;
    } else if (settings.effect == CHROMATIC) {
        vec2 direction = uv - vec2(0.5);
        vec2 offset = direction * (0.002 + 0.035 * strength * parameter);
        color = vec3(sampleColor(uv + offset).r, original.g, sampleColor(uv - offset).b);
    } else if (settings.effect == SSR) {
        vec2 reflectionUv = vec2(uv.x, 1.0 - uv.y) + vec2(0.025, -0.012) * (depth - 0.5);
        float reflectionWeight = smoothstep(0.15, 0.85, 1.0 - depth) * strength;
        color = mix(original, sampleColor(reflectionUv), reflectionWeight * 0.55);
    } else if (settings.effect == REFRACTION) {
        vec2 wave = vec2(sin(uv.y * 35.0 + ubo.time.x * 2.0), cos(uv.x * 31.0 - ubo.time.x * 1.6));
        color = sampleColor(uv + wave * ubo.viewport.zw * (2.0 + 14.0 * strength * parameter));
    } else if (settings.effect == OUTLINE) {
        float edge = max(depthEdge(uv), luminance(abs(original - neighborhoodAverage(uv, 1.0))));
        color = mix(original, vec3(0.015, 0.025, 0.04), smoothstep(0.12, 0.65, edge) * strength);
    } else if (settings.effect == DOF) {
        // Depth-buffer values are non-linear and become almost identical at
        // the far end of this scene's very large view range. Use reconstructed
        // view distance so a far object is actually treated as out of focus.
        float viewDistance = viewDistanceFromDepth(depth);
        float focusDistance = mix(0.3, 1.25, parameter);
        float focusRange = mix(0.06, 0.22, parameter);
        float defocus = smoothstep(
            focusRange,
            focusRange * 3.5,
            abs(viewDistance - focusDistance)
        );
        float blur = clamp(defocus * strength, 0.0, 1.0);
        // Use a wider footprint for distant terrain silhouettes. The blur is
        // still zero inside the focus range, so this does not soften nearby
        // gameplay geometry.
        float blurRadius = mix(8.0, 18.0, parameter) * blur;
        color = mix(original, neighborhoodAverage(uv, 1.0 + blurRadius), blur);
    } else if (settings.effect == DISTANT_BLUR) {
        float viewDistance = viewDistanceFromDepth(depth);
        float blurStart = mix(0.45, 1.0, parameter);
        float blurEnd = blurStart + mix(0.45, 0.9, parameter);
        float blur = smoothstep(blurStart, blurEnd, viewDistance) * strength;
        // Do not smear the clear color into the horizon. Rendered geometry
        // can still reach the far plane, so only exclude an exact depth clear.
        blur *= 1.0 - step(1.0, depth);
        color = mix(original, blur5(uv, 1.0 + blur * 8.0), blur);
    } else if (settings.effect == POSTERIZE) {
        float levels = mix(2.0, 12.0, parameter);
        color = floor(original * levels + 0.5) / levels;
    } else if (settings.effect == PIXELIZE) {
        float grid = mix(8.0, 160.0, parameter);
        vec2 pixelUv = floor(uv * grid) / grid + 0.5 / grid;
        color = sampleColor(pixelUv);
    } else if (settings.effect == SHARPEN) {
        vec2 texel = ubo.viewport.zw * (1.0 + 2.0 * parameter);
        vec3 neighbors = sampleColor(uv + vec2(texel.x, 0.0)) + sampleColor(uv - vec2(texel.x, 0.0)) +
            sampleColor(uv + vec2(0.0, texel.y)) + sampleColor(uv - vec2(0.0, texel.y));
        color = original + (original * 4.0 - neighbors) * (0.12 + strength * 0.38);
    } else if (settings.effect == DILATION) {
        vec2 texel = ubo.viewport.zw * (1.0 + parameter * 4.0);
        color = max(max(sampleColor(uv + texel), sampleColor(uv - texel)), original);
        color = mix(original, color, strength);
    } else if (settings.effect == GRAIN) {
        float grain = hash12(gl_FragCoord.xy + ubo.time.xx * 43.0) - 0.5;
        color = original + grain * (0.06 + 0.24 * strength) * (0.35 + 0.65 * parameter);
    } else if (settings.effect == LUT) {
        vec3 warm = vec3(color.r * 1.08 + 0.03, color.g * 0.94 + 0.015, color.b * 0.82);
        vec3 cool = vec3(color.r * 0.86, color.g * 0.98 + 0.02, color.b * 1.12 + 0.03);
        color = mix(warm, cool, parameter) * (0.72 + 0.28 * strength);
    } else if (settings.effect == GAMMA) {
        float gamma = mix(0.65, 2.4, parameter);
        color = pow(max(original, vec3(0.0)), vec3(1.0 / gamma));
    } else if (settings.effect == DEPTH) {
        color = vec3(depth);
        color = vec3(smoothstep(0.0, 1.0, depth), smoothstep(0.2, 0.8, depth), 1.0 - depth);
    } else if (settings.effect == EDGE) {
        float edge = depthEdge(uv);
        color = vec3(edge) + vec3(0.08, 0.22, 0.38) * depth;
    }

    outColor = vec4(max(color, vec3(0.0)), 1.0);
}
