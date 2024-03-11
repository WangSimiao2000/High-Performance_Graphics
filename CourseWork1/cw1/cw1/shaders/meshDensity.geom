#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout( set = 0, binding = 0 ) uniform UScene
{
	mat4 camera;
	mat4 projection;
	mat4 projCam;
} uScene;

layout(location = 0) in vec2 v2fTexCoord[]; 
layout(location = 1) out float density;     

void main() {
  vec3 v0 = (gl_in[0].gl_Position).xyz;
  vec3 v1 = (gl_in[1].gl_Position).xyz;
  vec3 v2 = (gl_in[2].gl_Position).xyz;

  float a = length(v1 - v0);
  float b = length(v2 - v1);
  float c = length(v0 - v2);

  float avgEdgeLength = (a + b + c) / 3.0;
  
  for(int i = 0; i < 3; ++i) {
	density = avgEdgeLength;
    gl_Position = uScene.projCam * gl_in[i].gl_Position;
    EmitVertex();
  }

  EndPrimitive();
}


