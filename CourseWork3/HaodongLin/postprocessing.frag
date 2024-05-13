#version 450

layout(binding = 0) uniform sampler2D inputTexture; // �м�����İ�λ��

layout(location = 0) in vec2 TexCoords;  // ���Զ�����ɫ������������
layout(location = 0) out vec4 outColor;  // �����֡�������ɫ

void main() {
    vec4 texColor = texture(inputTexture, TexCoords);
    float brightness = 1.2;  // ������ȵ�ϵ��
    outColor = vec4(texColor.rgb * brightness, texColor.a);
}