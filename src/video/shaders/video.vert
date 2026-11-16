#version 440
layout(location = 0) in vec4 position;
layout(location = 1) in vec2 texCoord;
layout(location = 0) out vec2 uv;
layout(std140, binding = 0) uniform VideoUniforms {
    mat4 matrix;
    mat4 colorMatrix;
    vec4 colorOffset;
    vec4 sampling;
    float opacity;
} ubuf;
void main() {
    uv = texCoord;
    gl_Position = ubuf.matrix * position;
}
