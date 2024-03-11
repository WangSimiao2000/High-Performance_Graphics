#version 450

layout( location = 0 ) out vec4 oColor;

void main()
{
    // ��ȡƬ�ε����ֵ
    float depth = gl_FragCoord.z * 500;

    // �������ֵ����Ļ�ռ�x��y�����ϵ�ƫ����, z�ǹ̶���
    float depthDerivativeX = dFdx(depth);
    float depthDerivativeY = dFdy(depth);

    vec3 depthGradient = vec3(depthDerivativeX, depthDerivativeY, 0.0);

    //����ֵ
    vec3 color = abs(depthGradient);

    // ���Ƶ�0��1֮��
    color = clamp(color, 0.0, 1.0);

    oColor = vec4(color, 1.0);
}