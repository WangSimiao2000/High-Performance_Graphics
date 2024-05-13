#version 460

layout( location = 0 ) in vec2 v2f_tc;

layout( location = 0 ) out vec4 oColor;

/*
layout( set = 0, binding = 0 ) uniform UScene
{
	mat4 camera;  // view matrix
	mat4 projection;  // projection matrix
	mat4 projCam;  // projection * camera
	vec3 cameraPos;  // position of camera
	vec3 lightPos;  // position of light
	vec3 lightCol;  // light Color
} uScene;
*/

layout( set = 1, binding = 0 ) uniform sampler2D gPosition;
layout( set = 1, binding = 1 ) uniform sampler2D gNormal;
layout( set = 1, binding = 2 ) uniform sampler2D gTexColor;
layout( set = 1, binding = 3 ) uniform sampler2D gMtlColor;
layout( set = 1, binding = 4 ) uniform sampler2D gRogColor;

void main()
{
   oColor = vec4( texture( gTexColor, v2f_tc).rgb, 1.0f);
   //oColor.rgb = texture( gNormal, v2f_tc).rgb;

}