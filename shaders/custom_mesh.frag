#version 450

layout(location = 0) in vec3 worldNormal;
layout(location = 1) in vec4 vertexColor;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec4 outColor;

void main()
{
	vec3 lightDirection = normalize(vec3(0.35, 0.8, 0.45));
	float diffuse = max(dot(normalize(worldNormal), lightDirection), 0.0);
	float lighting = 0.25 + 0.75 * diffuse;
	outColor = vec4(vertexColor.rgb * lighting, vertexColor.a);
}
