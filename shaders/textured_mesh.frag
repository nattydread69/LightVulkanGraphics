#version 450

// ShaderSelection::Textured: custom_mesh.frag's lighting with the colour multiplied by textures[0].
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
	vec4 base = texture(baseTexture, uv * material.parameters.xy) * vertexColor;
	vec3 lightDirection = normalize(vec3(0.35, 0.8, 0.45));
	float diffuse = max(dot(normalize(worldNormal), lightDirection), 0.0);
	float lighting = 0.25 + 0.75 * diffuse;
	outColor = vec4(base.rgb * lighting, base.a);
}
