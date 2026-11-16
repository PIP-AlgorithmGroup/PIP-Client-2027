#version 440
layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D sceneTexture;
// QML 离屏结果已预乘 alpha；呈交不再重复预乘。
void main() {
    fragColor = texture(sceneTexture, texCoord);
}
