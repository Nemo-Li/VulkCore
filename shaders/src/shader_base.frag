#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;

layout(set = 0, binding = 1) uniform sampler2D textureSampler;

layout(location = 0) out vec4 outColor;

void main() {
    const vec3 lightDirection = normalize(vec3(0.45, 0.55, 0.75));
    const float ambient = 0.25;
    const float diffuse = 0.75;
    const float lighting = ambient + diffuse * max(dot(normalize(fragNormal), lightDirection), 0.0);
    const vec4 baseColor = texture(textureSampler, fragTexCoord);
    outColor = vec4(baseColor.rgb * lighting, baseColor.a);
}
