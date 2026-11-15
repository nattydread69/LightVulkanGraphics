#version 450

// ShaderSelection::TerrainSplat: up to eight tiled layer colours blended by two RGBA weight maps.
//   textures[0] / textures[1] = weights of layers 0-3 / 4-7, sampled at the mesh uv (0..1 across the terrain).
//   textures[2..9] = layer colours, sampled at world xz divided by the layer's tile size in metres
//   (parameters.xyzw = layers 0-3, parameters2.xyzw = layers 4-7).
layout(set = 1, binding = 0) uniform sampler2D weightsLow;
layout(set = 1, binding = 1) uniform sampler2D weightsHigh;
layout(set = 1, binding = 2) uniform sampler2D layer0;
layout(set = 1, binding = 3) uniform sampler2D layer1;
layout(set = 1, binding = 4) uniform sampler2D layer2;
layout(set = 1, binding = 5) uniform sampler2D layer3;
layout(set = 1, binding = 6) uniform sampler2D layer4;
layout(set = 1, binding = 7) uniform sampler2D layer5;
layout(set = 1, binding = 8) uniform sampler2D layer6;
layout(set = 1, binding = 9) uniform sampler2D layer7;

layout(push_constant) uniform MaterialConstants
{
	layout(offset = 64) vec4 tileLow;
	layout(offset = 80) vec4 tileHigh;
} material;

layout(location = 0) in vec3 worldNormal;
layout(location = 1) in vec4 vertexColor;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec3 worldPosition;

layout(location = 0) out vec4 outColor;

void main()
{
	vec4 a = texture(weightsLow, uv);
	vec4 b = texture(weightsHigh, uv);
	float total = a.r + a.g + a.b + a.a + b.r + b.g + b.b + b.a;
	vec4 tl = max(material.tileLow, vec4(0.001));
	vec4 th = max(material.tileHigh, vec4(0.001));
	vec2 p = worldPosition.xz;
	// Explicit gradients: the texture reads below are inside branches, where implicit derivatives are undefined.
	vec2 dx = dFdx(p);
	vec2 dy = dFdy(p);
	vec3 colour = vec3(0.0);
	if (total > 1e-5)
	{
		// Each layer is sampled only when its weight is non-zero (cheap, and unused layers cost nothing).
		if (a.r > 0.0) colour += a.r * textureGrad(layer0, p / tl.x, dx / tl.x, dy / tl.x).rgb;
		if (a.g > 0.0) colour += a.g * textureGrad(layer1, p / tl.y, dx / tl.y, dy / tl.y).rgb;
		if (a.b > 0.0) colour += a.b * textureGrad(layer2, p / tl.z, dx / tl.z, dy / tl.z).rgb;
		if (a.a > 0.0) colour += a.a * textureGrad(layer3, p / tl.w, dx / tl.w, dy / tl.w).rgb;
		if (b.r > 0.0) colour += b.r * textureGrad(layer4, p / th.x, dx / th.x, dy / th.x).rgb;
		if (b.g > 0.0) colour += b.g * textureGrad(layer5, p / th.y, dx / th.y, dy / th.y).rgb;
		if (b.b > 0.0) colour += b.b * textureGrad(layer6, p / th.z, dx / th.z, dy / th.z).rgb;
		if (b.a > 0.0) colour += b.a * textureGrad(layer7, p / th.w, dx / th.w, dy / th.w).rgb;
		colour /= total;
	}
	colour *= vertexColor.rgb;
	vec3 lightDirection = normalize(vec3(0.35, 0.8, 0.45));
	float diffuse = max(dot(normalize(worldNormal), lightDirection), 0.0);
	float lighting = 0.25 + 0.75 * diffuse;
	outColor = vec4(colour * lighting, vertexColor.a);
}
