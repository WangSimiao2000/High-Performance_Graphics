#version 450
#extension GL_KHR_vulkan_glsl:enable

layout( location = 0 ) in vec2 v2fTexCoord;
layout( location = 1 ) in vec3 v2fNormal;
layout( location = 2 ) in vec3 v2fViewDir;
layout( location = 3 ) in vec3 v2fLightDir;
layout( location = 4 ) in vec3 v2fLightCol;
layout( location = 5 ) in vec3 v2fPosition;

layout( set = 1, binding = 0 ) uniform sampler2D uTexColor;
layout( set = 1, binding = 1 ) uniform sampler2D uMtlColor;
layout( set = 1, binding = 2 ) uniform sampler2D uRogColor;


layout( location = 0) out vec3 gPosition;
layout( location = 1) out vec3 gNormal;
layout( location = 2) out vec3 gTexColor;
layout( location = 3) out vec3 gMtlColor;
layout( location = 4) out vec3 gRogColor;


void main(){
    gPosition = v2fPosition;
    gNormal = v2fNormal;
    gTexColor = texture(uTexColor, v2fTexCoord).rgb;
    gMtlColor = texture(uMtlColor, v2fTexCoord).rgb;
    gRogColor = texture(uRogColor, v2fTexCoord).rgb;

}