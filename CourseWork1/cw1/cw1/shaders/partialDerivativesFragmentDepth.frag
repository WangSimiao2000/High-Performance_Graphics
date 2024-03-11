#version 450

layout( location = 0 ) out vec4 oColor;

void main()
{
    // 获取片段的深度值
    float depth = gl_FragCoord.z * 500;

    // 计算深度值在屏幕空间x和y方向上的偏导数, z是固定的
    float depthDerivativeX = dFdx(depth);
    float depthDerivativeY = dFdy(depth);

    vec3 depthGradient = vec3(depthDerivativeX, depthDerivativeY, 0.0);

    //绝对值
    vec3 color = abs(depthGradient);

    // 限制到0到1之间
    color = clamp(color, 0.0, 1.0);

    oColor = vec4(color, 1.0);
}