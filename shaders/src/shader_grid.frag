#version 450

layout(location = 2) in vec4 vertexColor;
layout(location = 3) in vec3 worldPosition;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Material {
    mat4 model;
    vec4 baseColor;
    uint textureFlags;
    float metallic;
    float roughness;
    float opacity;
    vec4 parameters0; // Grid spacing in world units and stroke width in pixels.
    vec4 parameters1;
} material;

void main() {
    vec2 lattice = worldPosition.xz / max(material.parameters0.x, 0.0001);
    vec2 dx = dFdx(lattice);
    vec2 dy = dFdy(lattice);
    vec2 unitsPerPixel = max(sqrt(dx * dx + dy * dy), vec2(0.00001));
    vec2 pixelDistance = abs(fract(lattice + 0.5) - 0.5) / unitsPerPixel;
    vec2 axisCoverage = 1.0 - smoothstep(0.0, max(material.parameters0.y, 0.01), pixelDistance);
    // Fade subpixel cells near the horizon so the grid does not alias into bands.
    axisCoverage *= 1.0 - smoothstep(vec2(0.4), vec2(0.85), unitsPerPixel);
    outColor = vec4(vertexColor.rgb * material.baseColor.rgb,
                    max(axisCoverage.x, axisCoverage.y) * vertexColor.a * material.opacity);
}
