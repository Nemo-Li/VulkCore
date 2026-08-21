#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

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

layout(set = 0, binding = 1) uniform sampler2D skyboxTexture;

const float PI = 3.14159265358979323846;

void main() {
    // Reconstruct a view ray from the full-screen triangle. Only the camera
    // rotation is applied, so the sky remains infinitely far away when the
    // camera moves through the scene.
    vec2 clipPosition = fragTexCoord * 2.0 - 1.0;
    vec4 viewPosition = inverse(ubo.projection) * vec4(clipPosition, 1.0, 1.0);
    vec3 viewDirection = normalize(viewPosition.xyz / viewPosition.w);
    vec3 worldDirection = normalize(transpose(mat3(ubo.view)) * viewDirection);

    // Convert the world-space direction to equirectangular UV coordinates.
    // U wraps around the horizon while V is clamped half a texel away from
    // the poles to prevent the texture's repeating sampler from bleeding.
    float u = atan(worldDirection.z, worldDirection.x) / (2.0 * PI) + 0.5;
    float v = acos(clamp(worldDirection.y, -1.0, 1.0)) / PI;
    vec2 textureDimensions = vec2(textureSize(skyboxTexture, 0));
    float verticalInset = 0.5 / max(textureDimensions.y, 1.0);
    vec2 skyUv = vec2(fract(u), clamp(v, verticalInset, 1.0 - verticalInset));

    outColor = vec4(texture(skyboxTexture, skyUv).rgb, 1.0);
}
