#version 450

layout(set = 0, binding = 0) uniform CameraUniform
{
	mat4 model;
	mat4 view;
	mat4 projection;
} camera;

layout(push_constant) uniform VolumePushConstants
{
	vec4 cameraWorld;
	vec4 volumeMinimum;
	vec4 volumeMaximum;
	vec4 clipPlane;
	vec4 clipBoxMinimum;
	vec4 clipBoxMaximum;
	vec4 settings;
	ivec4 flags;
} volume;

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 texturePosition;

void main()
{
	vec3 worldPosition = mix(
		volume.volumeMinimum.xyz,
		volume.volumeMaximum.xyz,
		inPosition);
	gl_Position = camera.projection * camera.view * vec4(worldPosition, 1.0);
	texturePosition = inPosition;
}
