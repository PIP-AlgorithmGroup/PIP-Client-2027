#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform VideoUniforms {
    mat4 matrix;
    mat4 colorMatrix;
    vec4 colorOffset;
    vec4 sampling;
    float opacity;
} ubuf;
layout(binding = 1) uniform sampler2D planeY;
layout(binding = 2) uniform sampler2D planeU;
layout(binding = 3) uniform sampler2D planeV;
void main() {
    vec2 chromaUv = uv + ubuf.sampling.xy;
    float y = texture(planeY, uv).r;
    vec2 chroma = texture(planeU, chromaUv).rg;
    float v = ubuf.sampling.z > 0.5 ? chroma.g : texture(planeV, chromaUv).r;
    vec4 rgb = ubuf.colorMatrix * vec4(y, chroma.r, v, 1.0) + ubuf.colorOffset;
    fragColor = vec4(clamp(rgb.rgb, 0.0, 1.0), 1.0) * ubuf.opacity;
}
