#include <lightVulkanGraphics/ui/widgets/Image.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

namespace lightGraphics::ui {

Image::Image(std::string label, TextureId textureId, Vec2 size)
	: m_textureId(textureId), m_size(size) {
	setLabel(std::move(label));
}

Vec2 Image::preferredSize(const GuiContext&) const {
	// Own height, not stretched to theme.rowHeight -- same convention PlotLine's
	// setHeight() already established for a custom-height widget with an optional label.
	return { 0.0f, m_size.y };
}

void Image::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	RowSplit split = splitRow(ctx);

	if (!m_label.empty()) {
		Color labelColor = effectivelyEnabled() ? th.text : th.textDisabled;
		dl.addTextClipped(ctx.font(), th.fontSize, split.label, labelColor, m_label, Align::Start, Align::Center);
	}

	// Drawn at m_size exactly, left-aligned in the control column -- NOT stretched to
	// fill split.control.w the way most controls are (see Image.h's constructor
	// comment): a colour-legend gradient or an icon has a meaningful aspect ratio that
	// stretching to an arbitrary panel width would distort.
	Color tint = effectivelyEnabled() ? m_tint : m_tint.withAlpha(0.4f);
	Rect imageRect{ split.control.x, split.control.y, m_size.x, m_size.y };
	dl.addImage(m_textureId, imageRect, m_uv0, m_uv1, tint);
}

} // namespace lightGraphics::ui
