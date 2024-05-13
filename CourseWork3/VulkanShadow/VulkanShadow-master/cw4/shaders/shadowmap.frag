#version 450

layout( location = 0 ) in float lp_distance;

void main()
{
    gl_FragDepth = lp_distance;
}
