#version 450

layout( location = 0 ) in vec3 iPosition;
layout( location = 1 ) in vec2 iTexCoord;
layout( location = 2 ) in vec3 iNormal;
//layout( location = 3 ) in vec4 iTangent;

layout( set = 0, binding = 0 ) uniform UScene
{
	mat4 camera;  // view matrix
	mat4 projection;  // projection matrix
	mat4 projCam;  // projection * camera
	vec3 cameraPos;  // position of camera
	vec3 lightPos;  // position of light
	vec3 lightCol;  // light Color
} uScene;

layout( location = 0 ) out vec2 v2fTexCoord;
layout( location = 1 ) out vec3 v2fNormal;
layout( location = 2 ) out vec3 v2fViewDir;
layout( location = 3 ) out vec3 v2fLightDir;
layout( location = 4 ) out vec3 v2fLightCol;
//layout( location = 5 ) out mat3 vTBN;


void main()
{
	v2fTexCoord = iTexCoord;

	vec3 worldPosition = iPosition;
	v2fNormal = normalize(iNormal);
	v2fViewDir = normalize(uScene.cameraPos - worldPosition);
	v2fLightDir = normalize(uScene.lightPos - worldPosition);
	v2fLightCol = uScene.lightCol;

	gl_Position = uScene.projCam * vec4( iPosition, 1.f );
} 