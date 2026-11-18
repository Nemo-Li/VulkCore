#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 vertexColor;
layout(location = 3) in vec3 worldPosition;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform Frame {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 time;
    vec4 viewport;
    vec4 ambient;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 cameraPosition;
} frame;

layout(push_constant) uniform Material {
    mat4 model;
    vec4 baseColor;
    uint textureFlags;
    float metallic;
    float roughness;
    float opacity;
    vec4 parameters0;
    vec4 parameters1;
} material;

layout(set = 0, binding = 1) uniform sampler2D baseTexture;

vec3 aces(vec3 color) {
    const mat3 inputMatrix = mat3(0.59719, 0.07600, 0.02840,
                                  0.35458, 0.90834, 0.13383,
                                  0.04823, 0.01566, 0.83777);
    const mat3 outputMatrix = mat3(1.60475, -0.10208, -0.00327,
                                  -0.53108, 1.10813, -0.07276,
                                  -0.07367, -0.00605, 1.07602);
    vec3 v = inputMatrix * color * (frame.time.z / 0.6);
    vec3 fit = (v * (v + 0.0245786) - 0.000090537) /
               (v * (0.983729 * v + 0.4329510) + 0.238081);
    return clamp(outputMatrix * fit, 0.0, 1.0);
}

void main() {
    float progress = material.opacity;
    if (progress >= 0.999999) discard;
    // The plane and width are expressed as fractions of the full sweep.
    float distanceToScan = dot(material.parameters0.xyz, worldPosition) +
                           material.parameters0.w - progress;
    float rim = 0.0;
    if (progress > 0.000001) {
        if (distanceToScan < 0.0) discard;
        rim = 1.0 - smoothstep(0.0, material.parameters1.w, distanceToScan);
    }
    vec4 surface = material.baseColor * vertexColor;
    if ((material.textureFlags & 1u) != 0u) surface *= texture(baseTexture, uv);
    vec3 color = surface.rgb;
    vec3 n = normalize(gl_FrontFacing ? normal : -normal);
    vec3 l = normalize(frame.lightDirection.xyz);
    vec3 v = normalize(frame.cameraPosition.xyz - worldPosition);
    vec3 h = normalize(v + l);
    float nl = max(dot(n, l), 0.0);
    float nv = max(dot(n, v), 0.001);
    float nh = max(dot(n, h), 0.0);
    float vh = max(dot(v, h), 0.0);
    float a = material.roughness * material.roughness;
    float a2 = a * a;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    float distribution = a2 / (3.14159265 * d * d);
    float visibility = 0.5 / max(nl * sqrt(nv * nv * (1.0 - a2) + a2) +
                                 nv * sqrt(nl * nl * (1.0 - a2) + a2), 0.00001);
    vec3 f0 = mix(vec3(0.04), color, material.metallic);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
    vec3 diffuse = color * (1.0 - material.metallic) / 3.14159265;
    vec3 light = frame.lightColor.rgb * frame.lightDirection.w;
    vec3 ambient = frame.ambient.rgb * frame.ambient.a * (0.5 + 0.5 * n.y);
    outColor = vec4(aces(diffuse * ambient + (diffuse + distribution * visibility * fresnel) *
                         light * nl + material.parameters1.rgb * rim),
                    mix(surface.a, 1.0, rim));
}
