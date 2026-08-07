#include <lightVulkanGraphics/ui/widgets/Separator.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

namespace lightGraphics::ui {

Vec2 Separator::preferredSize(const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	if (m_caption.empty()) {
		return { 0.0f, th.itemSpacing * 2.0f + 1.0f };
	}
	return { 0.0f, ctx.font().lineHeight(th.fontSize) };
}

void Separator::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	float midY = m_bounds.y + m_bounds.h * 0.5f;

	if (m_caption.empty()) {
		dl.addLine({ m_bounds.x, midY }, { m_bounds.right(), midY }, th.border, 1.0f);
		return;
	}

	const Font& font = ctx.font();
	float textW = font.measureText(m_caption, th.fontSize).x;
	constexpr float kLeadIn = 12.0f;

	float leftLineEnd = m_bounds.x + kLeadIn;
	dl.addLine({ m_bounds.x, midY }, { leftLineEnd, midY }, th.border, 1.0f);

	float textX = leftLineEnd + th.itemSpacing;
	float lh = font.lineHeight(th.fontSize);
	Rect textRect{ textX, m_bounds.y, textW + 2.0f, lh };
	dl.addTextClipped(font, th.fontSize, textRect, th.text, m_caption, Align::Start, Align::Center);

	float rightLineStart = textX + textW + th.itemSpacing;
	if (rightLineStart < m_bounds.right()) {
		dl.addLine({ rightLineStart, midY }, { m_bounds.right(), midY }, th.border, 1.0f);
	}
}

} // namespace lightGraphics::ui
