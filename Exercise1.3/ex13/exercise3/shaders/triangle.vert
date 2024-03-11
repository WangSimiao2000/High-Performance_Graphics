#version 450

const vec2 kVertexPositions[3] = vec2[3](
	vec2( 0.0f, -0.8f ),
	vec2( -0.7f, 0.8f ),
	vec2( +0.7f, 0.8f )
);

void main()
{
	vec2 xy = kVertexPositions[gl_VertexIndex];
	gl_Position = vec4(xy, 0.5f, 1.f);
}
