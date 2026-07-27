#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec4 vertexColor;

void main()
{
	gl_Position = vec4(inPosition, 1.0);
	vertexColor = inColor;
}
