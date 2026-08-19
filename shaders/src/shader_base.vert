#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
} ubo;

layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform;
    vec4 baseColor;
    uint hasTexture;
} pushConstants;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec4 fragVertexColor;

void main() {
    mat4 model = ubo.model * pushConstants.nodeTransform;
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    gl_Position = ubo.projection * ubo.view * model * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragNormal = normalize(normalMatrix * inNormal);
    fragVertexColor = inColor;
}
