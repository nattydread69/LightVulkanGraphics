#version 450

// Signed-distance-field text: the atlas red channel is distance to the glyph outline
// remapped so the edge sits at 0.5. The anti-aliasing width comes from the screen-space
// derivative of the distance, so edges stay about one pixel wide at any scale.

layout(set = 0, binding = 0) uniform sampler2D uAtlas;

layout(constant_id = 0) const float kTextContrast = 1.0;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main() {
	float distance = texture(uAtlas, vUV).r;
	float width = max(fwidth(distance) * 0.7, 1.0 / 256.0);
	float coverage = smoothstep(0.5 - width, 0.5 + width, distance);
	float lum = dot(vColor.rgb, vec3(0.299, 0.587, 0.114));
	float exponent = mix(1.0, mix(1.25, 0.8, lum), kTextContrast);
	coverage = pow(coverage, exponent);
	outColor = vec4(vColor.rgb, vColor.a * coverage);
}
