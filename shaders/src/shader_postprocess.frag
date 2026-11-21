#version 450

// 旧版全屏后处理。从场景颜色和深度快照读取数据，同一遍绘制按 effect 选择一个预设。

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model; mat4 view; mat4 projection; vec4 time; vec4 viewport;
    vec4 ambientLight; vec4 sunLightDirection; vec4 sunLightColor;
} ubo;
layout(set = 0, binding = 2) uniform sampler2D sceneColorTexture;
layout(set = 0, binding = 5) uniform sampler2D sceneDepthTexture;

// 复用材质 Pipeline Layout。保留矩阵与颜色的占位，再将材质字段的字节槽位解释为后处理参数。
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

// 将 UV 限在附件内部，避免屏幕边缘的邻域采样读到对侧或边界色。
vec3 sampleColor(vec2 uv) {
    return texture(sceneColorTexture, clamp(uv, vec2(0.001), vec2(0.999))).rgb;
}

float sampleDepth(vec2 uv) {
    return texture(sceneDepthTexture, clamp(uv, vec2(0.001), vec2(0.999))).r;
}

float viewDistanceFromDepth(float depth) {
    // 透视投影中，深度约等于常数加上 1/viewZ 项。
    // 由 z_ndc = -P[2][2] - P[3][2] / viewZ 解出视空间轴向距离，省去逐片元求逆矩阵。
    // 取绝对值使相机前方得到正距离，给分母加下限避免远处的数值放大。
    float denominator = -ubo.projection[2][2] - depth;
    return abs(ubo.projection[3][2] / max(abs(denominator), 0.000001));
}

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// 五点十字滤波，中心占半权重。用视口倒数把像素半径转换为采样 UV 步长。
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

// 累计四邻域的深度差，提取几何边界，即使相邻表面颜色接近仍能检测。
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
    // 分屏左侧直接输出原场景，遮住预设的处理结果并跳过滤波采样。
    if (settings.splitEnabled > 0.5 && uv.x < split.x) {
        outColor = vec4(original, 1.0);
        return;
    }

    float strength = clamp(settings.strength, 0.0, 2.0);
    float parameter = clamp(settings.parameter, 0.0, 1.0);
    float depth = sampleDepth(uv);
    vec3 color = original;
    // 下列编号与渲染器传入的后处理效果 ID 对应。
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
        // 直接用投影深度与纵向渐变近似雾幕。
        float fog = smoothstep(0.18, mix(0.55, 0.98, parameter), depth) * strength;
        vec3 fogColor = mix(vec3(0.08, 0.13, 0.19), vec3(0.55, 0.72, 0.78), uv.y);
        color = mix(original, fogColor, clamp(fog, 0.0, 1.0));
    } else if (settings.effect == BOX_BLUR) {
        color = neighborhoodAverage(uv, mix(1.0, 5.0, parameter) * strength);
    } else if (settings.effect == MEDIAN) {
        // MEDIAN 预设做四点均值再混回原色，以较少采样获得柔化效果。
        vec2 texel = ubo.viewport.zw * mix(1.0, 3.0, parameter);
        vec3 a = sampleColor(uv - texel), b = sampleColor(uv + texel);
        vec3 c = sampleColor(uv + vec2(texel.x, -texel.y));
        color = (a + b + c + original) * 0.25;
        color = mix(color, original, 0.3);
    } else if (settings.effect == KUWAHARA) {
        // 用四个偏移窗口的均值与中心色的差异近似 Kuwahara 的区域选择。
        // 选择更接近中心色的区域，可减少跨边缘的平均，形成平滑色块。
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
        // 旧版 Bloom 直接从一组模糊采样判断亮度并叠回原图。
        vec3 glow = max(neighborhoodAverage(uv, 3.0), vec3(0.0));
        float bright = smoothstep(0.55, 1.0, luminance(glow));
        color = original + glow * bright * (0.25 + strength * 0.75);
    } else if (settings.effect == SSAO) {
        vec2 texel = ubo.viewport.zw * mix(1.0, 5.0, parameter);
        // 取圆周上的深度差作为经验遮蔽信号，采样半径以像素为单位。
        float occlusion = 0.0;
        for (int i = 0; i < 8; ++i) {
            float angle = float(i) * 0.785398 + ubo.time.x * 0.15;
            vec2 sampleUv = uv + vec2(cos(angle), sin(angle)) * texel;
            occlusion += step(0.006 + depth * 0.015, sampleDepth(sampleUv) - depth);
        }
        color = original * (1.0 - occlusion / 8.0 * 0.72 * strength);
    } else if (settings.effect == MOTION) {
        // 时变向量指定模糊方向，沿它积分颜色以模拟运动拖影。
        vec2 velocity = vec2(sin(ubo.time.x * 0.7), cos(ubo.time.x * 0.53)) * ubo.viewport.zw * (2.0 + 12.0 * strength);
        color = vec3(0.0);
        for (int i = -4; i <= 4; ++i) color += sampleColor(uv + velocity * float(i) / 4.0);
        color /= 9.0;
    } else if (settings.effect == CHROMATIC) {
        // 沿远离屏幕中心的方向将红、蓝通道偏向两侧，模拟径向色散。
        vec2 direction = uv - vec2(0.5);
        vec2 offset = direction * (0.002 + 0.035 * strength * parameter);
        color = vec3(sampleColor(uv + offset).r, original.g, sampleColor(uv - offset).b);
    } else if (settings.effect == SSR) {
        // SSR 预设将画面上下镜像，再用当前深度控制偏移和反射权重。
        vec2 reflectionUv = vec2(uv.x, 1.0 - uv.y) + vec2(0.025, -0.012) * (depth - 0.5);
        float reflectionWeight = smoothstep(0.15, 0.85, 1.0 - depth) * strength;
        color = mix(original, sampleColor(reflectionUv), reflectionWeight * 0.55);
    } else if (settings.effect == REFRACTION) {
        // 相互交错的正弦沿屏幕 XY 扭曲场景颜色，时间控制波的传播。
        vec2 wave = vec2(sin(uv.y * 35.0 + ubo.time.x * 2.0), cos(uv.x * 31.0 - ubo.time.x * 1.6));
        color = sampleColor(uv + wave * ubo.viewport.zw * (2.0 + 14.0 * strength * parameter));
    } else if (settings.effect == OUTLINE) {
        // 将颜色差与深度边缘合并。前者捕捉同一表面上的纹样边界，后者捕捉几何边界。
        float edge = max(depthEdge(uv), luminance(abs(original - neighborhoodAverage(uv, 1.0))));
        color = mix(original, vec3(0.015, 0.025, 0.04), smoothstep(0.12, 0.65, edge) * strength);
    } else if (settings.effect == DOF) {
        // 远处的投影深度都挤在 1 附近。先恢复视空间距离，再按与焦面的距离调节模糊。
        float viewDistance = viewDistanceFromDepth(depth);
        float focusDistance = mix(0.3, 1.25, parameter);
        float focusRange = mix(0.06, 0.22, parameter);
        float defocus = smoothstep(
            focusRange,
            focusRange * 3.5,
            abs(viewDistance - focusDistance)
        );
        float blur = clamp(defocus * strength, 0.0, 1.0);
        // 焦区内混合权重为零，远景或近景离焦后才逐步放大采样范围。
        float blurRadius = mix(8.0, 18.0, parameter) * blur;
        color = mix(original, neighborhoodAverage(uv, 1.0 + blurRadius), blur);
    } else if (settings.effect == DISTANT_BLUR) {
        float viewDistance = viewDistanceFromDepth(depth);
        float blurStart = mix(0.45, 1.0, parameter);
        float blurEnd = blurStart + mix(0.45, 0.9, parameter);
        float blur = smoothstep(blurStart, blurEnd, viewDistance) * strength;
        // 排除深度清屏值，保留背景本身的清晰度。
        blur *= 1.0 - step(1.0, depth);
        color = mix(original, blur5(uv, 1.0 + blur * 8.0), blur);
    } else if (settings.effect == POSTERIZE) {
        float levels = mix(2.0, 12.0, parameter);
        color = floor(original * levels + 0.5) / levels;
    } else if (settings.effect == PIXELIZE) {
        float grid = mix(8.0, 160.0, parameter);
        // 将同一格内的像素都指向格子中心，使每格输出一致的采样色。
        vec2 pixelUv = floor(uv * grid) / grid + 0.5 / grid;
        color = sampleColor(pixelUv);
    } else if (settings.effect == SHARPEN) {
        vec2 texel = ubo.viewport.zw * (1.0 + 2.0 * parameter);
        vec3 neighbors = sampleColor(uv + vec2(texel.x, 0.0)) + sampleColor(uv - vec2(texel.x, 0.0)) +
            sampleColor(uv + vec2(0.0, texel.y)) + sampleColor(uv - vec2(0.0, texel.y));
        // 中心乘 4 再减去四邻域近似离散拉普拉斯残差，用它增强高频细节。
        color = original + (original * 4.0 - neighbors) * (0.12 + strength * 0.38);
    } else if (settings.effect == DILATION) {
        vec2 texel = ubo.viewport.zw * (1.0 + parameter * 4.0);
        // 将对角邻域按通道取最大值，模拟形态学膨胀并扩展亮部。
        color = max(max(sampleColor(uv + texel), sampleColor(uv - texel)), original);
        color = mix(original, color, strength);
    } else if (settings.effect == GRAIN) {
        // 像素坐标固定颗粒尺度，时间平移哈希输入使颗粒逐帧变化。
        float grain = hash12(gl_FragCoord.xy + ubo.time.xx * 43.0) - 0.5;
        color = original + grain * (0.06 + 0.24 * strength) * (0.35 + 0.65 * parameter);
    } else if (settings.effect == LUT) {
        // LUT 预设在两套逐通道缩放与偏置之间混合，实现冷暖调色。
        vec3 warm = vec3(color.r * 1.08 + 0.03, color.g * 0.94 + 0.015, color.b * 0.82);
        vec3 cool = vec3(color.r * 0.86, color.g * 0.98 + 0.02, color.b * 1.12 + 0.03);
        color = mix(warm, cool, parameter) * (0.72 + 0.28 * strength);
    } else if (settings.effect == GAMMA) {
        float gamma = mix(0.65, 2.4, parameter);
        color = pow(max(original, vec3(0.0)), vec3(1.0 / gamma));
    } else if (settings.effect == DEPTH) {
        // 用伪彩色放大深度梯度，方便观察投影深度挤在远端的分布。
        color = vec3(depth);
        color = vec3(smoothstep(0.0, 1.0, depth), smoothstep(0.2, 0.8, depth), 1.0 - depth);
    } else if (settings.effect == EDGE) {
        float edge = depthEdge(uv);
        color = vec3(edge) + vec3(0.08, 0.22, 0.38) * depth;
    }

    outColor = vec4(max(color, vec3(0.0)), 1.0);
}
