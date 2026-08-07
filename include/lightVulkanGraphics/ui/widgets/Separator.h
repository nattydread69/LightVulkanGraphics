#pragma once

// docs/gui/05-widgets.md, "Separator": a 1px horizontal rule, optionally with an inline
// caption ("line, gap, text, gap, line").

#include "../Widget.h"

#include <string>

namespace lightGraphics::ui {

class Separator : public Widget {
public:
	Separator() = default;
	explicit Separator(std::string caption) : m_caption(std::move(caption)) {}

	Vec2 preferredSize(const GuiContext&) const override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsCapture() const override { return false; }
	bool acceptsFocus()   const override { return false; }

private:
	std::string m_caption;
};

} // namespace lightGraphics::ui
