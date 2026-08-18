#include <lightVulkanGraphics/ui/widgets/Label.h>
#include <lightVulkanGraphics/ui/GuiContext.h>
#include "../TextWrap.h"

namespace lightGraphics::ui {

Label::Label(std::string text) : m_text(std::move(text)) {}

Vec2 Label::preferredSize(const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	if (!m_wordWrap) {
		return ctx.font().measureText(m_text, th.fontSize);
	}

	// m_bounds.w reflects the row width from the PREVIOUS layout pass (0 the very first
	// time this panel ever laid out). GuiContext::endFrame() runs layout twice per frame
	// for exactly this reason: pass 1 gives every widget a first-cut bounds; pass 2 then
	// sees the correct width here and reports the true wrapped height. See docs/gui/05,
	// "Label".
	float lh = ctx.font().lineHeight(th.fontSize);
	if (m_bounds.w <= 0.0f) {
		return { 0.0f, lh };
	}
	auto lines = wrapText(ctx.font(), m_text, th.fontSize, m_bounds.w);
	return { 0.0f, static_cast<float>(lines.size()) * lh };
}

void Label::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	Color color = m_colorOverride ? *m_colorOverride : (effectivelyEnabled() ? th.text : th.textDisabled);

	if (m_wordWrap) {
		auto lines = wrapText(ctx.font(), m_text, th.fontSize, m_bounds.w);
		float lh = ctx.font().lineHeight(th.fontSize);
		float y = m_bounds.y;
		for (auto& line : lines) {
			Rect lineRect{ m_bounds.x, y, m_bounds.w, lh };
			dl.addTextClipped(ctx.font(), th.fontSize, lineRect, color, line, m_align, Align::Start);
			y += lh;
		}
		return;
	}

	dl.addTextClipped(ctx.font(), th.fontSize, m_bounds, color, m_text, m_align, Align::Center);
}

} // namespace lightGraphics::ui
