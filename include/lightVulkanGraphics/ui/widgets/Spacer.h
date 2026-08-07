#pragma once

// docs/gui/05-widgets.md, "Spacer": invisible, non-interactive, fixed height.

#include "../Widget.h"

namespace lightGraphics::ui {

class Spacer : public Widget {
public:
	explicit Spacer(float height) : m_height(height) {}

	Vec2 preferredSize(const GuiContext&) const override { return { 0.0f, m_height }; }
	void draw(DrawList&, const GuiContext&) const override {}

	bool acceptsCapture() const override { return false; }
	bool acceptsFocus()   const override { return false; }
	bool hitTest(Vec2) const override { return false; }

private:
	float m_height;
};

} // namespace lightGraphics::ui
