#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;

layout(set = 0, binding = 1) uniform sampler2D textureSampler;

layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform;
    vec4 baseColor;
    uint hasTexture;
} material;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 baseColor = material.baseColor * fragVertexColor;
    if (material.hasTexture != 0u) {
        baseColor *= texture(textureSampler, fragTexCoord);
    }

    vec3 normal = normalize(fragNormal);
    vec3 lightDirection = normalize(vec3(0.45, 0.55, 0.75));
    vec3 viewDirection = normalize(vec3(0.42, 0.48, 0.77));
    vec3 halfwayDirection = normalize(lightDirection + viewDirection);

    const vec3 ambientLight = vec3(0.18, 0.21, 0.24);
    const vec3 diffuseLight = vec3(0.82, 0.86, 0.9);
    const vec3 specularLight = vec3(1.0, 0.98, 0.92);
    float diffuseFactor = max(dot(normal, lightDirection), 0.0);
    float specularFactor = pow(max(dot(normal, halfwayDirection), 0.0), 48.0);

    vec3 litColor = baseColor.rgb * (ambientLight + diffuseLight * diffuseFactor);
    litColor += specularLight * specularFactor * 0.18;
    outColor = vec4(litColor, baseColor.a);
}
