#version 450

// LVGUI colour images (ui::Image / DrawList::addImage with a texture registered through
// VkApp::registerUiTexture()): the texture's own RGBA, tinted by the vertex colour.
// ui.frag stays the coverage shader for the font atlases. UiRenderer picks between the
// two per draw command; both share one pipeline layout and vertex format.

layout(set = 0, binding = 0) uniform sampler2D uImage;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

// 8-bit vertex colours and 8-bit framebuffers band on slow, dark gradients: the interpolated
// colour is float, but each channel is rounded to one of 256 levels on write, so a gradient
// spanning a few levels shows hard steps. Round stochastically instead (interleaved gradient
// noise, Jimenez 2014) so the steps average out. Values already on an 8-bit level are left
// untouched, so flat fills stay exact. kSrgbOutput = 1.0 when the target is *_SRGB, where the
// hardware encodes after the shader and the 8-bit steps live in sRGB space.
layout(constant_id = 1) const float kSrgbOutput = 0.0;

vec3 toSrgb(vec3 c) {
	return mix(c * 12.92, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
vec3 fromSrgb(vec3 c) {
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 ditherRgb(vec3 c) {
	float u = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
	vec3 v = (kSrgbOutput > 0.5 ? toSrgb(c) : c) * 255.0;
	v = floor(v + 0.0005 + 0.999 * u) / 255.0;
	return kSrgbOutput > 0.5 ? fromSrgb(v) : v;
}

void main() {
	vec4 c = texture(uImage, vUV) * vColor;
	outColor = vec4(ditherRgb(c.rgb), c.a);
}
