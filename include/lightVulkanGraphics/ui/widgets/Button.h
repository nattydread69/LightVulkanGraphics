#pragma once

// docs/gui/05-widgets.md, "Button": fires on release-inside-after-press-inside, never on
// press. Focusable; Space and Enter activate it while focused.

#include "../Widget.h"

#include <functional>
#include <string>

namespace lightGraphics::ui {

class Button : public Widget {
public:
	explicit Button(std::string text) { setLabel(std::move(text)); }

	void setOnClick(std::function<void()> onClick) { m_onClick = std::move(onClick); }
	void setToggle(bool isToggle) { m_isToggle = isToggle; }
	bool isPressed() const { return m_pressed; }
	void setPressed(bool pressed) { m_pressed = pressed; }

	Vec2 preferredSize(const GuiContext&) const override;
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsFocus() const override { return true; }

private:
	std::function<void()> m_onClick;
	bool m_isToggle = false;
	bool m_pressed = false;   // toggle state
	bool m_held = false;      // captured AND currently hovered this frame -- drives fill colour
};

} // namespace lightGraphics::ui
