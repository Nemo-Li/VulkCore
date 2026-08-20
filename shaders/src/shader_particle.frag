#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

layout(set = 0, binding = 1) uniform sampler2D particleTexture;

layout(push_constant) uniform ParticlePushConstants {
    mat4 unusedNodeTransform;
    vec4 unusedBaseColor;
    uint textureFlags;
} particle;

layout(location = 0) out vec4 outColor;

void main() {
    if ((particle.textureFlags & 1u) != 0u) {
        vec4 sampledColor = texture(particleTexture, fragTexCoord);
        outColor = sampledColor * fragColor;
    } else {
        // Keep the previous soft circular sprite as the no-texture fallback.
        vec2 centered = fragTexCoord * 2.0 - 1.0;
        float distanceFromCenter = length(centered);
        if (distanceFromCenter > 1.0) {
            discard;
        }
        float edgeFade = 1.0 - smoothstep(0.72, 1.0, distanceFromCenter);
        outColor = vec4(fragColor.rgb, fragColor.a * edgeFade);
    }
    if (outColor.a <= 0.001) {
        discard;
    }
}
