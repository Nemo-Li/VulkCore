#ifndef VULKCORE_SUN_SHADOW_COVERAGE_GLSL
#define VULKCORE_SUN_SHADOW_COVERAGE_GLSL

#ifdef VULKCORE_SHADOW_PASS
void writeSunShadowCoverage(float alpha) {
    // 固定在阴影图像素上的抖动将半透明覆盖率转成部分深度写入。PCF 将结果滤成透光阴影。
    if (alpha >= 0.999) return;
    float threshold = fract(52.9829189 * fract(dot(floor(gl_FragCoord.xy), vec2(0.06711056, 0.00583715))));
    if (alpha <= threshold) discard;
}
#define SUN_SHADOW_OUTPUT
#define SUN_SHADOW_COLOR(value) writeSunShadowCoverage((value).a)
#else
#define SUN_SHADOW_OUTPUT layout(location = 0) out vec4 outColor;
#define SUN_SHADOW_COLOR(value) outColor = (value)
#endif

#endif
