#version 450

layout(set = 0, binding = 0) uniform sampler2D uAtlas;

// 1.0 = compensate for blending glyph coverage in gamma (non-sRGB) space; 0.0 = leave
// coverage untouched (sRGB framebuffers already blend linearly, so correcting again would
// over-embolden light text). Set per pipeline from UiRendererCreateInfo::textContrast.
layout(constant_id = 0) const float kTextContrast = 1.0;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main() {
	float coverage = texture(uAtlas, vUV).r;
	// Gamma-space blending makes light-on-dark text look thin and dark-on-light text look
	// heavy. Bend the coverage curve by the text colour's luminance to even them out.
	// Solid fills sample the atlas's white block (coverage exactly 1.0), which pow()
	// leaves alone.
	float lum = dot(vColor.rgb, vec3(0.299, 0.587, 0.114));
	float exponent = mix(1.0, mix(1.25, 0.8, lum), kTextContrast);
	coverage = pow(coverage, exponent);
	outColor = vec4(vColor.rgb, vColor.a * coverage);
}
