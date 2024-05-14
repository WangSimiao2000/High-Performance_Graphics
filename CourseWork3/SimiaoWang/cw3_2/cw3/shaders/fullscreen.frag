#version 460
#extension GL_KHR_vulkan_glsl:enable

layout( location = 0 ) in vec2 v2f_textureColor;
layout( location = 0 ) out vec4 oColor;
layout( set = 1, binding = 0 ) uniform sampler2D gPosition;
layout( set = 1, binding = 1 ) uniform sampler2D gNormal;
layout( set = 1, binding = 2 ) uniform sampler2D gTexColor;
layout( set = 1, binding = 3 ) uniform sampler2D gMtlColor;
layout( set = 1, binding = 4 ) uniform sampler2D gRogColor;

void main()
{
   vec3 color = texture(gTexColor, v2f_textureColor).rgb;
   vec3 toneMappedColor = color / (1.0 + color);

   //oColor = vec4(color, 1.0f);
   oColor = vec4(toneMappedColor, 1.0f); // Tone Mapping
}