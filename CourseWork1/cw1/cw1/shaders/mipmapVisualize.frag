#version 450
#extension GL_KHR_vulkan_glsl:enable

layout( location = 0 ) in vec2 v2fTexCoord;
layout( set = 1, binding = 0 ) uniform sampler2D uTexColor;
layout( location = 0 ) out vec4 oColor;

void main()
{
    float lod = textureQueryLod(uTexColor, v2fTexCoord).x;
    float mipmapLevel = floor(lod); // 整数部分,作为mipmap级别
    float fraction = fract(lod); // 小数部分,用于颜色插值
    vec4 colorA;
    vec4 colorB;

    if(mipmapLevel == 0.0) {
        colorA = vec4(1.0, 0.0, 0.0, 1.0); // 红色
        colorB = vec4(1.0, 1.0, 0.0, 1.0); // 黄色
    } else if(mipmapLevel == 1.0) {
        colorA = vec4(1.0, 1.0, 0.0, 1.0); // 黄色
        colorB = vec4(0.0, 1.0, 0.0, 1.0); // 绿色
    } else if(mipmapLevel == 2.0) {
        colorA = vec4(0.0, 1.0, 0.0, 1.0); // 绿色
        colorB = vec4(0.0, 1.0, 1.0, 1.0); // 青色
    } else if(mipmapLevel == 3.0) {
        colorA = vec4(0.0, 1.0, 1.0, 1.0); // 青色
        colorB = vec4(0.0, 0.0, 1.0, 1.0); // 蓝色
    } else if(mipmapLevel == 4.0) {
        colorA = vec4(0.0, 0.0, 1.0, 1.0); // 蓝色
        colorB = vec4(0.5, 0.0, 0.5, 1.0); // 紫色
    } else if(mipmapLevel == 5.0) {
        colorA = vec4(0.5, 0.0, 0.5, 1.0); // 紫色
        colorB = vec4(1.0, 0.0, 1.0, 1.0); // 品红色
    } else if(mipmapLevel == 6.0) {
        colorA = vec4(1.0, 0.0, 1.0, 1.0); // 品红色
        colorB = vec4(1.0, 1.0, 1.0, 1.0); // 白色
    } else {
        colorA = vec4(1.0, 1.0, 1.0, 1.0); // 白色
        colorB = vec4(0.0, 0.0, 0.0, 1.0); // 黑色
    }

    // 使用mix函数在两种颜色之间插值
    oColor = mix(colorA, colorB, fraction);
}