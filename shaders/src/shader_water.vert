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
    // Two slightly different waves avoid a uniform sine-wave look while
    // keeping the displacement stable for arbitrary imported water meshes.
    float time = ubo.time.x;
    float waveA = sin(dot(inPosition.xy, vec2(1.8, 2.6)) + time * 1.35);
    float waveB = sin(dot(inPosition.xy, vec2(-2.7, 1.4)) - time * 0.9 + 1.7);
    float waveC = cos(dot(inPosition.xy, vec2(3.2, -1.1)) + time * 0.55);
    float displacement = (waveA * 0.55 + waveB * 0.3 + waveC * 0.15) * 0.025;

    vec3 displacedPosition = inPosition + normalize(inNormal) * displacement;
    mat4 model = ubo.model * pushConstants.nodeTransform;
    mat3 normalMatrix = transpose(inverse(mat3(model)));

    gl_Position = ubo.projection * ubo.view * model * vec4(displacedPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragNormal = normalize(normalMatrix * inNormal);
    fragVertexColor = inColor;
}
