#version 450

// ShaderSelection::TexturedUnlit: textures[0] multiplied by the vertex colour, with no lighting at all, so artwork
// shows in its own colours. Sprites and UI-like quads use it. Shares textured_mesh.vert.
layout(set = 1, binding = 0) uniform sampler2D baseTexture;

layout(push_constant) uniform MaterialConstants
{
	layout(offset = 64) vec4 parameters;
	layout(offset = 80) vec4 parameters2;
} material;

layout(location = 0) in vec3 worldNormal;
layout(location = 1) in vec4 vertexColor;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec3 worldPosition;

layout(location = 0) out vec4 outColor;

void main()
{
	outColor = texture(baseTexture, uv * material.parameters.xy) * vertexColor;
}
