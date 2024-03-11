#version 450
#extension GL_KHR_vulkan_glsl:enable

layout( location = 0 ) in vec3 v3fColor;
layout( location = 0 ) out vec4 oColor;
layout( location = 1 ) in float density;

void main()
{
	float waste = density;
	oColor = vec4( v3fColor, 1.f );
}