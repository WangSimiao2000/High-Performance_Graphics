#version 450
#extension GL_KHR_vulkan_glsl:enable

layout( location = 0 ) in vec2 v2fTexCoord;
layout( set = 1, binding = 0 ) uniform sampler2D uTexColor;
layout( location = 0 ) out vec4 oColor;

void main()
{
    float lod = textureQueryLod(uTexColor, v2fTexCoord).x;
    float mipmapLevel = floor(lod); // ��������,��Ϊmipmap����
    float fraction = fract(lod); // С������,������ɫ��ֵ
    vec4 colorA;
    vec4 colorB;

    if(mipmapLevel == 0.0) {
        colorA = vec4(1.0, 0.0, 0.0, 1.0); // ��ɫ
        colorB = vec4(1.0, 1.0, 0.0, 1.0); // ��ɫ
    } else if(mipmapLevel == 1.0) {
        colorA = vec4(1.0, 1.0, 0.0, 1.0); // ��ɫ
        colorB = vec4(0.0, 1.0, 0.0, 1.0); // ��ɫ
    } else if(mipmapLevel == 2.0) {
        colorA = vec4(0.0, 1.0, 0.0, 1.0); // ��ɫ
        colorB = vec4(0.0, 1.0, 1.0, 1.0); // ��ɫ
    } else if(mipmapLevel == 3.0) {
        colorA = vec4(0.0, 1.0, 1.0, 1.0); // ��ɫ
        colorB = vec4(0.0, 0.0, 1.0, 1.0); // ��ɫ
    } else if(mipmapLevel == 4.0) {
        colorA = vec4(0.0, 0.0, 1.0, 1.0); // ��ɫ
        colorB = vec4(0.5, 0.0, 0.5, 1.0); // ��ɫ
    } else if(mipmapLevel == 5.0) {
        colorA = vec4(0.5, 0.0, 0.5, 1.0); // ��ɫ
        colorB = vec4(1.0, 0.0, 1.0, 1.0); // Ʒ��ɫ
    } else if(mipmapLevel == 6.0) {
        colorA = vec4(1.0, 0.0, 1.0, 1.0); // Ʒ��ɫ
        colorB = vec4(1.0, 1.0, 1.0, 1.0); // ��ɫ
    } else {
        colorA = vec4(1.0, 1.0, 1.0, 1.0); // ��ɫ
        colorB = vec4(0.0, 0.0, 0.0, 1.0); // ��ɫ
    }

    // ʹ��mix������������ɫ֮���ֵ
    oColor = mix(colorA, colorB, fraction);
}