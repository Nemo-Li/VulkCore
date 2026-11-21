#version 450

// 通用网格顶点阶段。将节点局部顶点变换到渲染场景空间，并提供多种材质共用的插值量。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;

// 此处只声明 CPU 帧 UBO 的前缀。后续字段可以省略，已声明字段须保持布局一致。
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 time;
    vec4 viewport;
    vec4 ambientLight;
    vec4 sunLightDirection;
    vec4 sunLightColor;
} ubo;

// 与片元材质共用 128 字节的 Push Constants 布局。本阶段只读节点矩阵。
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

void main() {
    mat4 model = ubo.model * pushConstants.nodeTransform;
    // 法线与变换后的切向量仍须正交。非均匀缩放时应使用线性变换的逆转置。
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    vec3 worldPosition = (model * vec4(inPosition, 1.0)).xyz;
    gl_Position = ubo.projection * ubo.view * vec4(worldPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragNormal = normalize(normalMatrix * inNormal);
    fragVertexColor = inColor;
    fragWorldPosition = worldPosition;
}
