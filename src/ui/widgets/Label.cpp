#include <lightVulkanGraphics/ui/widgets/Label.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

namespace lightGraphics::ui {

Label::Label(std::string text) : m_text(std::move(text)) {}

std::vector<std::string_view> Label::wrapLines(const Font& font, float pixelSize, float width) const {
	std::vector<std::string_view> lines;
	std::string_view text = m_text;
	if (width <= 0.0f || text.empty()) {
		lines.push_back(text);
		return lines;
	}

	std::size_t lineStart = 0;
	std::size_t lastLineEnd = 0;   // byte index of the last space seen on the current line
	std::size_t pos = 0;

	while (pos <= text.size()) {
		bool atEnd = pos == text.size();
		if (atEnd || text[pos] == ' ') {
			float w = font.measureText(text.substr(lineStart, pos - lineStart), pixelSize).x;
			if (w > width && lastLineEnd > lineStart) {
				// Overflowed -- break at the last space seen on this line instead, and
				// re-evaluate the SAME pos against the new, shorter line.
				lines.push_back(text.substr(lineStart, lastLineEnd - lineStart));
				lineStart = lastLineEnd + 1;
				continue;
			}
			lastLineEnd = pos;
			if (atEnd) {
				lines.push_back(text.substr(lineStart, pos - lineStart));
				break;
			}
		}
		++pos;
	}
	return lines;
}

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
	auto lines = wrapLines(ctx.font(), th.fontSize, m_bounds.w);
	return { 0.0f, static_cast<float>(lines.size()) * lh };
}

void Label::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	Color color = m_colorOverride ? *m_colorOverride : (m_enabled ? th.text : th.textDisabled);

	if (m_wordWrap) {
		auto lines = wrapLines(ctx.font(), th.fontSize, m_bounds.w);
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
