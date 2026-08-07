#pragma once

// docs/gui/05-widgets.md, "Label": static, non-interactive text.

#include "../Widget.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lightGraphics::ui {

class Font;

class Label : public Widget {
public:
	explicit Label(std::string text);

	void setText(std::string text) { m_text = std::move(text); }
	std::string_view text() const { return m_text; }
	void setAlign(Align h) { m_align = h; }
	void setWordWrap(bool wrap) { m_wordWrap = wrap; }
	void setColor(Color color) { m_colorOverride = color; }

	Vec2 preferredSize(const GuiContext&) const override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsCapture() const override { return false; }
	bool acceptsFocus()   const override { return false; }

private:
	std::vector<std::string_view> wrapLines(const Font&, float pixelSize, float width) const;

	std::string m_text;
	Align m_align = Align::Start;
	bool  m_wordWrap = false;
	std::optional<Color> m_colorOverride;
};

} // namespace lightGraphics::ui
