#version 450

layout (location = 0) in vec3 position;

layout( set = 0, binding = 0 ) uniform ULightTransform
{
    mat4 M;
    vec3 lightPos;
    float max_depth;
} uTransForm;

layout( location = 0 ) out float lp_distance;

void main()
{
    gl_Position = uTransForm.M * vec4(position, 1.0);
    lp_distance = min(length(uTransForm.lightPos - position) / uTransForm.max_depth, 1.0);
}
