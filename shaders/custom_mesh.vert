#version 450

layout(set = 0, binding = 0) uniform CameraUniform
{
	mat4 model;
	mat4 view;
	mat4 projection;
} camera;

layout(push_constant) uniform MeshPushConstants
{
	mat4 model;
} pushConstants;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inUv;

layout(location = 0) out vec3 worldNormal;
layout(location = 1) out vec4 vertexColor;
layout(location = 2) out vec2 uv;

void main()
{
	vec4 worldPosition = pushConstants.model * vec4(inPosition, 1.0);
	gl_Position = camera.projection * camera.view * worldPosition;
	worldNormal = normalize(mat3(transpose(inverse(pushConstants.model))) * inNormal);
	vertexColor = inColor;
	uv = inUv;
}
