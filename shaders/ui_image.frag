#version 450

// LVGUI colour images (ui::Image / DrawList::addImage with a texture registered through
// VkApp::registerUiTexture()): the texture's own RGBA, tinted by the vertex colour.
// ui.frag stays the coverage shader for the font atlases. UiRenderer picks between the
// two per draw command; both share one pipeline layout and vertex format.

layout(set = 0, binding = 0) uniform sampler2D uImage;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main() {
	outColor = texture(uImage, vUV) * vColor;
}
