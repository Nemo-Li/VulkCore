#version 450

// 地形顶点阶段。同时传递归一化后的渲染位置与原始地形坐标。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;
// xy 为地形瓦片的原始尺寸，zw 为整片地形的控制图 UV。
layout(location = 6) in vec4 inTerrainMapping;

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
} pushConstants;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec4 fragVertexColor;
layout(location = 3) out vec3 fragWorldPosition;
layout(location = 4) out vec3 fragTerrainPosition;
layout(location = 5) out vec4 fragTerrainMapping;

void main() {
    mat4 model = ubo.model * pushConstants.nodeTransform;
    // 非均匀变换后仍要保证法线与切平面正交，供片元阶段计算坡度和三平面权重。
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    vec3 worldPosition = (model * vec4(inPosition, 1.0)).xyz;
    gl_Position = ubo.projection * ubo.view * vec4(worldPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragNormal = normalize(normalMatrix * inNormal);
    fragVertexColor = inColor;
    fragWorldPosition = worldPosition;
    // 纹理尺度和海拔分层固定在原始地形坐标中，避免场景归一化改变地貌。
    fragTerrainPosition = inPosition;
    // 范围在片元阶段用作除数，此处先加下限。
    fragTerrainMapping = vec4(max(inTerrainMapping.xy, vec2(0.0001)), inTerrainMapping.zw);
}
