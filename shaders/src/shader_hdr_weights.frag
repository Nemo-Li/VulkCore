#version 450

// SMAA Medium 权重阶段。沿水平、垂直边缘寻找端点，输出邻域混合需要的四个权重。

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
// SMAA Medium 使用正交搜索，查找纹理的来源见 assets/textures/smaa/LICENSE.md。
layout(binding = 0) uniform sampler2D edgeMap;
layout(binding = 1) uniform sampler2D areaMap;
layout(binding = 2) uniform sampler2D searchMap;

// 搜索使用 Y 翻转的坐标约定，实际读取 edgeMap 时还原到纹理 UV。
vec2 edgesAt(vec2 p) {
    return texture(edgeMap, vec2(p.x, 1.0 - p.y)).rg;
}

// 将双线性边缘样本和搜索方向编码为 Search LUT 坐标，解出两像素步长下的端点小数偏移。
float searchLength(vec2 edge, float offset) {
    vec2 packed = (vec2(32.0, -32.0) * edge + vec2(66.0 * offset + 0.5, 32.5)) / vec2(64.0, 16.0);
    return texture(searchMap, vec2(packed.x, 1.0 - packed.y)).r;
}

float searchEdge(vec2 start, float end, vec2 pixel, bool vertical, bool positive) {
    vec2 edge = vec2(0.0, 1.0);
    float direction = positive ? 1.0 : -1.0;
    float stepSize = vertical ? pixel.y : pixel.x;
    // 最多 8 步，每步 2 像素。连续边消失或遇到垂直交叉边时停止。
    for (int i = 0; i < 8; ++i) {
        float coordinate = vertical ? start.y : start.x;
        if (direction * (end - coordinate) <= 0.0 || edge.y <= 0.8281 || edge.x != 0.0) break;
        vec2 sampleEdge = edgesAt(start);
        edge = vertical ? sampleEdge.gr : sampleEdge;
        start += (vertical ? vec2(0.0, stepSize) : vec2(stepSize, 0.0)) * (2.0 * direction);
    }
    float offset = -(255.0 / 127.0) * searchLength(edge, positive ? 0.5 : 0.0) + 3.25;
    return (vertical ? start.y : start.x) - direction * stepSize * offset;
}

// Area LUT 将边长与端点交叉形态映射为预计算的覆盖权重。开方对应表内对距离的压缩索引。
vec2 areaWeights(vec2 distances, vec2 crossing) {
    vec2 lookup = (16.0 * round(4.0 * crossing) + sqrt(abs(distances)) + 0.5) / vec2(160.0, 560.0);
    return texture(areaMap, lookup).rg;
}

void main() {
    vec2 p = vec2(uv.x, 1.0 - uv.y);
    vec2 resolution = vec2(textureSize(edgeMap, 0));
    vec2 pixel = 1.0 / resolution;
    vec2 edges = edgesAt(p);
    // 亚像素偏移让线性采样同时编码相邻边缘。横向搜索写 RG，纵向搜索写 BA。
    vec4 weights = vec4(0.0);
    if (edges.y > 0.0) {
        vec2 left = p + pixel * vec2(-0.25, -0.125);
        vec2 right = p + pixel * vec2(1.25, -0.125);
        float xmin = searchEdge(left, left.x - 16.0 * pixel.x, pixel, false, false);
        float xmax = searchEdge(right, right.x + 16.0 * pixel.x, pixel, false, true);
        float crossingY = p.y - 0.25 * pixel.y;
        vec2 crossing = vec2(edgesAt(vec2(xmin, crossingY)).r,
                             edgesAt(vec2(xmax + pixel.x, crossingY)).r);
        weights.rg = areaWeights(round(resolution.x * (vec2(xmin, xmax) - p.x)), crossing);
    }
    if (edges.x > 0.0) {
        vec2 top = p + pixel * vec2(-0.125, -0.25);
        vec2 bottom = p + pixel * vec2(-0.125, 1.25);
        float ymin = searchEdge(top, top.y - 16.0 * pixel.y, pixel, true, false);
        float ymax = searchEdge(bottom, bottom.y + 16.0 * pixel.y, pixel, true, true);
        float crossingX = p.x - 0.25 * pixel.x;
        vec2 crossing = vec2(edgesAt(vec2(crossingX, ymin)).g,
                             edgesAt(vec2(crossingX, ymax + pixel.y)).g);
        weights.ba = areaWeights(round(resolution.y * (vec2(ymin, ymax) - p.y)), crossing);
    }
    outColor = weights;
}
