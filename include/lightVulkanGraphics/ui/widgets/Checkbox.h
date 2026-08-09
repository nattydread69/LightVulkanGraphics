#pragma once

// docs/gui/05-widgets.md, "Checkbox": toggles on mouse PRESS (not release); Space toggles
// while focused. The whole row, label included, is clickable -- Widget::hitTest's default
// (m_bounds.contains) already covers this since m_bounds is the full row, so this class
// does not override hitTest. The box itself is drawn in the CONTROL column, with the
// label in the label column -- a deliberate departure from Dear ImGui, per the doc.

#include "../Widget.h"

#include <functional>
#include <string>

namespace lightGraphics::ui {

class Checkbox : public Widget {
public:
	enum class State { Off, On, Indeterminate };

	explicit Checkbox(std::string label, bool initial = false);

	bool value() const { return m_state == State::On; }
	void setValue(bool value, bool fireCallback = false);
	// The pointer must outlive the widget. update() reads *target at the start of every
	// frame (so external mutation shows up) and writes it back on toggle -- see
	// docs/gui/05, "Checkbox", "Binding".
	void bind(bool* target) { m_bindTarget = target; }
	void setOnChange(std::function<void(bool)> onChange) { m_onChange = std::move(onChange); }
	// Enables a third click state. With tri-state on, clicking cycles Off -> On ->
	// Indeterminate -> Off; a bound bool* only ever observes On/Off (Indeterminate has no
	// bool representation), matching value()'s definition above.
	void setTriState(bool triState) { m_triState = triState; }
	State state() const { return m_state; }

	Vec2 preferredSize(const GuiContext&) const override;
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsFocus() const override { return true; }

private:
	void setState(State state, bool fireCallback);
	void toggle();

	State m_state;
	bool m_triState = false;
	bool* m_bindTarget = nullptr;
	std::function<void(bool)> m_onChange;
};

} // namespace lightGraphics::ui
