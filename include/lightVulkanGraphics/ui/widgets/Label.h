#pragma once

// docs/gui/05-widgets.md, "Label": static, non-interactive text.

#include "../Widget.h"

#include <optional>
#include <string>
#include <string_view>

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
	// Pads digit advances to the widest digit's own width -- for a Label used to display
	// a live-updating numeric readout, so its digit columns don't jitter horizontally as
	// the value changes (docs/gui/03-text-and-fonts.md, "Tabular figures"). Off by
	// default: most Labels are static prose, not numeric readouts.
	void setTabular(bool tabular) { m_tabular = tabular; }
	// Draws with GuiContext's heading face/size instead of the primary font
	// (docs/gui/03-text-and-fonts.md, "Headings") -- e.g. a section title above a group
	// of widgets. Silently falls back to the primary font if the GuiContext this Label
	// is drawn/measured with was never configured with a heading font
	// (GuiCreateInfo::headingFontSize left at its 0.0f default): NOT every consumer
	// opts in, and a Label should never crash or render nothing just because one didn't.
	void setHeading(bool heading) { m_heading = heading; }

	Vec2 preferredSize(const GuiContext&) const override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsCapture() const override { return false; }
	bool acceptsFocus()   const override { return false; }

private:
	std::string m_text;
	Align m_align = Align::Start;
	bool  m_wordWrap = false;
	bool  m_tabular = false;
	bool  m_heading = false;
	std::optional<Color> m_colorOverride;
};

} // namespace lightGraphics::ui
