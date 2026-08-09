#pragma once

// docs/gui/05-widgets.md, "CollapsingSection": Owns children. preferredSize returns just
// the header height when closed, header plus laid-out children when open. Header draws a
// triangle glyph (built from addTriangleFilled, not a font character) plus the title, and
// toggles on click.

#include "CompositeWidget.h"

#include <string>

namespace lightGraphics::ui {

class CollapsingSection : public CompositeWidget {
public:
	explicit CollapsingSection(std::string title, bool openInitially = true);

	void setOpen(bool open) { m_open = open; }
	bool isOpen() const { return m_open; }

	Vec2 preferredSize(const GuiContext& ctx) const override;
	void update(GuiContext& ctx) override;
	void draw(DrawList&, const GuiContext&) const override;
	void layout(const GuiContext& ctx) override;
	Widget* hitTestDeep(Vec2 p) override;  // Block children when closed

private:
	std::string m_title;
	bool m_open;
};

} // namespace lightGraphics::ui
