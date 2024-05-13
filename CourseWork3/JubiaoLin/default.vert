/*
#version 450

layout( location = 0 ) in vec3 iPosition;
layout( location = 1 ) in vec2 iTexCoord;
layout( location = 2 ) in vec3 iNormal;

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


void main()
{
	v2fTexCoord = iTexCoord;
	v2fLightCol = uScene.lightCol;

	vec3 worldPosition = iPosition;  
    v2fNormal = iNormal;

	v2fViewDir = normalize(uScene.cameraPos - worldPosition);

    v2fLightDir = normalize(uScene.lightPos - worldPosition);

	gl_Position = uScene.projection * uScene.camera * vec4(worldPosition, 1.f);

} 
*/

#version 450

layout( location = 0 ) in vec3 iPosition;
layout( location = 1 ) in vec2 iTexCoord;
layout( location = 2 ) in vec3 iNormal;

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
layout( location = 5 ) out vec3 v2fPosition;

void main(){
	v2fPosition = iPosition;
	v2fTexCoord = iTexCoord;
	v2fLightCol = uScene.lightCol;
    v2fNormal = iNormal;

	v2fViewDir = normalize(uScene.cameraPos - iPosition);

    v2fLightDir = normalize(uScene.lightPos - iPosition);

	gl_Position = uScene.projection * uScene.camera * vec4(iPosition, 1.f);
}