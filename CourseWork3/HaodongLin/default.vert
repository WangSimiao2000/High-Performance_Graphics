#version 450

layout( location = 0 ) in vec3 iPosition;
layout( location = 1 ) in vec2 iTexCoord;
layout( location = 2 ) in vec3 iNormal;
layout( location = 3 ) in vec4 iTangent;

layout( set = 0, binding = 0 ) uniform UScene
{
	mat4 camera;  // view matrix
	mat4 projection;  // projection matrix
	mat4 projCam;  // projection * camera
	vec3 cameraPos;  // position of camera
	vec3 lightPos;  // position of light
	vec3 lightCol;  // light Color
} uScene;

layout( location = 0 ) out vec3 fragPos;
layout( location = 1 ) out vec2 fragTexCoord;
layout( location = 2 ) out vec3 fragNormal;
layout( location = 3 ) out vec3 viewDir;
layout( location = 4 ) out vec3 lightDir;
layout( location = 5 ) out vec3 lightCol;
layout( location = 6 ) out mat3 vTBN;

void main()
{
	fragTexCoord = iTexCoord;
	lightCol = vec3(1,1,1);

	fragNormal = iNormal;

	viewDir = normalize(uScene.cameraPos - iPosition);

	lightDir = normalize(uScene.lightPos - iPosition);

	vec3 T = normalize(iTangent.xyz);
    vec3 N = normalize(iNormal);
    vec3 B = normalize(cross(T, N)) * iTangent.w;
    vTBN = mat3(T, B, N);

	gl_Position = uScene.projCam * vec4( iPosition, 1.f );
}
