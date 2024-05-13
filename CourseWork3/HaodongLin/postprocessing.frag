#version 450

layout(binding = 0) uniform sampler2D inputTexture; // 中间纹理的绑定位置

layout(location = 0) in vec2 TexCoords;  // 来自顶点着色器的纹理坐标
layout(location = 0) out vec4 outColor;  // 输出到帧缓冲的颜色

void main() {
    vec4 texColor = texture(inputTexture, TexCoords);
    float brightness = 1.2;  // 提高亮度的系数
    outColor = vec4(texColor.rgb * brightness, texColor.a);
}