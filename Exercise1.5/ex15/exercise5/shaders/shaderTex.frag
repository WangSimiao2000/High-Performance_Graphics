#version 450

layout( location = 0 ) in vec2 v2fTexCoord;

layout( set = 1, binding = 0 ) uniform sampler2D uTexColor;

layout( location = 0 ) out vec4 oColor;//will be written to the framebuffer¡¯s color attachment with index 0

void main()
{
	oColor = vec4( texture( uTexColor, v2fTexCoord ).rgb, 1.f );
}