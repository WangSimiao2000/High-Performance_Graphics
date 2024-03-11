#version 450

layout( location = 0 ) out vec4 oColor;

void main()
{
    // (X, Y, Z, W) -> (X/W, Y/W, Z/W, 1)
    float normalizedDepth = (gl_FragCoord.z / gl_FragCoord.w)/100.0;
    vec4 depthColor = vec4(normalizedDepth, normalizedDepth, normalizedDepth, 1.0);

    oColor = depthColor;
}