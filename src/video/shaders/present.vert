#version 440
layout(location = 0) out vec2 texCoord;
layout(std140, binding = 0) uniform Orientation { vec4 direction; };
// 全屏三角形；方向由 RHI 的 NDC/纹理坐标约定决定，不在 CPU 翻转像素。
void main() {
    texCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(texCoord * 2.0 - 1.0, 0.0, 1.0);
    gl_Position.y *= direction.x;
}
