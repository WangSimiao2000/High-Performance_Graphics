#version 450
#extension GL_KHR_vulkan_glsl:enable

layout( location = 0 ) out vec2 v2fUV;

void main()
{
	// Predefined UV coordinates for the full-screen quad
    vec2 uvCoordinates[4] = vec2[4](
        vec2(0.0, 0.0), // bottom-left
        vec2(2.0, 0.0), // bottom-right
        vec2(0.0, 2.0), // top-left
        vec2(2.0, 2.0)  // top-right
    );

    // Assign UV coordinates based on the vertex index
    v2fUV = uvCoordinates[gl_VertexIndex];

    // Calculate the position in NDC space
    gl_Position = vec4(v2fUV * 2.0 - 1.0, 0.0, 1.0);
}	