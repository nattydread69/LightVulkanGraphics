#pragma once

// docs/gui/05-widgets.md, "Slider": the centrepiece value widget. See that section for
// the interaction table (capture, Shift-fine-drag, double-click reset, logarithmic
// scale, stepping) and the drawing recipe this class's draw() follows.

#include "../Widget.h"

#include <functional>
#include <memory>
#include <string>

namespace lightGraphics::ui {

class TextBox;

enum class SliderScale { Linear, Logarithmic };

template <typename T>
class SliderT : public Widget {
public:
	SliderT(std::string label, T minVal, T maxVal, T initial);

	T value() const { return m_value; }
	void setValue(T value, bool fireCallback = false);
	// Sets the value from a normalised [0,1] track position (out-of-range t clamps),
	// applying the same scale/step/clamp pipeline a drag does. docs/gui/08-testing.md's
	// stepping property test drives this directly across an out-of-range sweep, and it is
	// the cleanest way to test scale placement without depending on drag pixel math.
	void setValueFromNormalised(float t);
	// Inverse of the above: where m_value currently sits on the [0,1] track, honouring
	// setScale(). Exposed for the same reason -- verifying logarithmic placement doesn't
	// require reconstructing draw()'s track pixel math in a test.
	float normalisedValue() const { return normalisedPosition(m_value); }
	// The pointer must outlive the widget -- update() reads *target at the start of every
	// frame and writes it back on change, same contract as Checkbox::bind.
	void bind(T* target) { m_bindTarget = target; }

	void setRange(T minVal, T maxVal);
	void setStep(T step) { m_step = step; }             // 0 = continuous
	void setScale(SliderScale scale);
	void setFormat(std::string printfFmt) { m_format = std::move(printfFmt); }
	void setUnitSuffix(std::string suffix) { m_unitSuffix = std::move(suffix); }
	void setShowValue(bool show) { m_showValue = show; }

	void setOnChange(std::function<void(T)> onChange) { m_onChange = std::move(onChange); }
	void setOnCommit(std::function<void(T)> onCommit) { m_onCommit = std::move(onCommit); }

	// docs/gui/05, "Inline text entry": Ctrl-click replaces the control region with a
	// live TextBox for one editing session (see beginTextEdit()) unless this hook is
	// set, in which case the hook runs instead -- a real extension point for a consumer
	// that wants different Ctrl-click behaviour, rather than SliderT duplicating any
	// text-editing logic either way.
	void setCtrlClickHook(std::function<void(SliderT<T>&, GuiContext&)> hook) { m_onCtrlClick = std::move(hook); }

	Vec2 preferredSize(const GuiContext&) const override;
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsFocus()   const override { return true; }
	// True only while the inline TextBox edit session is open -- see beginTextEdit().
	bool wantsTextInput() const override { return m_editBox != nullptr; }
	// docs/gui/05, "Wheel while hovered": steps the value and keeps the panel from also
	// scrolling underneath it.
	bool wantsWheel() const override { return true; }

private:
	float normalisedPosition(T value) const;
	T valueFromNormalised(float t) const;
	T applyStepAndClamp(float raw) const;
	Rect trackRect(const GuiContext&) const;
	std::string formatValue() const;
	std::string formatValueForEdit() const;
	void setValueInternal(T value, bool fireOnChange, bool fireOnCommit);
	void stepBy(float steps, bool shiftFast);
	void beginTextEdit(GuiContext&);
	// Returns true once the edit session (opened by beginTextEdit) has ended this frame
	// -- Enter/Escape, or focus moving elsewhere -- having already parsed and committed
	// or silently discarded the entered text as appropriate.
	bool updateTextEdit(GuiContext&);

	T m_min, m_max, m_initial, m_value;
	T m_step{};
	SliderScale m_scale = SliderScale::Linear;
	std::string m_format;
	std::string m_unitSuffix;
	bool m_showValue = true;

	T* m_bindTarget = nullptr;
	std::function<void(T)> m_onChange;
	std::function<void(T)> m_onCommit;
	std::function<void(SliderT<T>&, GuiContext&)> m_onCtrlClick;

	bool m_dragging = false;
	float m_dragLastMouseX = 0.0f;
	// Seconds since the last press on this slider, accumulated every frame regardless of
	// button state -- what double-click detection compares against on the NEXT press.
	float m_timeSinceLastPress = 1.0e9f;

	// docs/gui/05, "Inline text entry": owned on demand by beginTextEdit(), forwarded
	// update()/draw() while open, destroyed once the session ends. TextBox is only
	// forward-declared above -- SliderT<T>'s destructor is defined where TextBox.h is a
	// complete type (Slider.cpp, at the bottom's explicit `template class` instantiation),
	// same pattern as the `extern template class` declarations below this class rely on.
	std::unique_ptr<TextBox> m_editBox;
};

using Slider    = SliderT<float>;
using SliderInt = SliderT<int>;

extern template class SliderT<float>;
extern template class SliderT<int>;

} // namespace lightGraphics::ui
