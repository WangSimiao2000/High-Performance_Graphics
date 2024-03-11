#version 450
#extension GL_KHR_vulkan_glsl:enable

layout( location = 0 ) in vec2 g2fTexCoord;
layout( location = 0 ) out vec4 oColor;
layout( location = 1 ) in float density;

void main() {
    float normalized = 1.0 / (1.0 + exp(-1 * density));

    vec3 lowColor = vec3(0, 0, 0);
    vec3 highColor = vec3(0.8, 1.0, 0.8);
    vec3 finalColor = mix(lowColor, highColor, normalized);

    oColor = vec4(finalColor, 1.0);
}