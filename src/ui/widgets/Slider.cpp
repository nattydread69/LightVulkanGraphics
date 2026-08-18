#include <lightVulkanGraphics/ui/widgets/Slider.h>
#include <lightVulkanGraphics/ui/widgets/TextBox.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace lightGraphics::ui {

namespace {
	// Not specified in docs/gui/05-widgets.md; 0.35s matches the common native default
	// (Windows/GTK land around 0.3-0.5s) and reads comfortably in testing.
	constexpr float kDoubleClickSeconds = 0.35f;
}

template <typename T>
SliderT<T>::SliderT(std::string label, T minVal, T maxVal, T initial)
	: m_min(minVal), m_max(maxVal), m_initial(initial), m_value(initial) {
	setLabel(std::move(label));
	setValue(initial, false);
}

template <typename T>
T SliderT<T>::applyStepAndClamp(float raw) const {
	float lo = static_cast<float>(m_min);
	float hi = static_cast<float>(m_max);
	float v = raw;
	if (m_step > T{ 0 }) {
		float step = static_cast<float>(m_step);
		// Round to the step BEFORE clamping -- clamping first and then rounding a range
		// of 0-10 with step 3 can produce 12 (docs/gui/05, "Stepping").
		v = lo + std::round((v - lo) / step) * step;
	}
	v = std::clamp(v, std::min(lo, hi), std::max(lo, hi));
	if constexpr (std::is_integral<T>::value) {
		return static_cast<T>(std::lround(v));
	} else {
		return static_cast<T>(v);
	}
}

template <typename T>
float SliderT<T>::normalisedPosition(T value) const {
	float lo = static_cast<float>(m_min);
	float hi = static_cast<float>(m_max);
	if (hi <= lo) {
		return 0.0f;
	}
	if (m_scale == SliderScale::Logarithmic) {
		float llo = std::log(lo);
		float lhi = std::log(hi);
		if (lhi <= llo) {
			return 0.0f;
		}
		float lv = std::log(static_cast<float>(value));
		return (lv - llo) / (lhi - llo);
	}
	return (static_cast<float>(value) - lo) / (hi - lo);
}

template <typename T>
T SliderT<T>::valueFromNormalised(float t) const {
	t = std::clamp(t, 0.0f, 1.0f);
	float lo = static_cast<float>(m_min);
	float hi = static_cast<float>(m_max);
	float raw;
	if (m_scale == SliderScale::Logarithmic) {
		float llo = std::log(lo);
		float lhi = std::log(hi);
		raw = std::exp(llo + t * (lhi - llo));
	} else {
		raw = lo + t * (hi - lo);
	}
	return applyStepAndClamp(raw);
}

template <typename T>
Rect SliderT<T>::trackRect(const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	RowSplit split = splitRow(ctx);
	const Rect& control = split.control;
	float insetY = (control.h - th.sliderTrackHeight) * 0.5f;
	return control.insetXY(0.0f, insetY);
}

template <typename T>
std::string SliderT<T>::formatValue() const {
	std::string fmt = m_format;
	if (fmt.empty()) {
		if constexpr (std::is_integral<T>::value) {
			fmt = "%d";
		} else {
			fmt = "%.3f";
		}
	}
	char buf[64];
	if constexpr (std::is_integral<T>::value) {
		std::snprintf(buf, sizeof(buf), fmt.c_str(), static_cast<int>(m_value));
	} else {
		std::snprintf(buf, sizeof(buf), fmt.c_str(), static_cast<double>(m_value));
	}
	std::string result(buf);
	result += m_unitSuffix;
	return result;
}

template <typename T>
std::string SliderT<T>::formatValueForEdit() const {
	// Deliberately not formatValue(): that includes m_unitSuffix and any custom
	// printf-style precision formatting, neither of which strtod/strtol should have to
	// parse back out. "%.6g"/"%d" round-trip a raw numeric value exactly.
	char buf[64];
	if constexpr (std::is_integral<T>::value) {
		std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(m_value));
	} else {
		std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(m_value));
	}
	return std::string(buf);
}

template <typename T>
void SliderT<T>::beginTextEdit(GuiContext&) {
	m_editBox = std::make_unique<TextBox>("", formatValueForEdit());
	m_editBox->setFilter(std::is_integral<T>::value ? TextFilter::Integer : TextFilter::Decimal);
	m_editBox->setForcedFocus(true);
	m_editBox->selectAll();
}

template <typename T>
bool SliderT<T>::updateTextEdit(GuiContext& ctx) {
	RowSplit split = splitRow(ctx);
	m_editBox->setBounds(split.control);
	m_editBox->updateEmbedded(ctx);

	bool endSession = false, cancelled = false;
	for (const KeyEvent& ev : ctx.input().keyQueue) {
		if (!ev.pressed) {
			continue;
		}
		if (ev.key == Key::Enter) {
			endSession = true;
		} else if (ev.key == Key::Escape) {
			endSession = true;
			cancelled = true;
		}
	}
	if (ctx.focusedId() != id()) {
		endSession = true;   // focus moved elsewhere -- commit like a real field would
	}

	if (endSession) {
		if (!cancelled) {
			// docs/gui/05: "parse with strtod/strtol; on success clamp to range and
			// commit; on failure revert silently and restore the slider."
			std::string text(m_editBox->text());
			const char* start = text.c_str();
			char* end = nullptr;
			double parsed = std::strtod(start, &end);
			bool ok = (end != start);
			while (ok && *end != '\0') {
				if (!std::isspace(static_cast<unsigned char>(*end))) {
					ok = false;
					break;
				}
				++end;
			}
			if (ok) {
				setValueInternal(applyStepAndClamp(static_cast<float>(parsed)), true, true);
			}
		}
		m_editBox.reset();
	}
	return endSession;
}

template <typename T>
void SliderT<T>::setValueInternal(T value, bool fireOnChange, bool fireOnCommit) {
	m_value = value;
	if (m_bindTarget) {
		*m_bindTarget = m_value;
	}
	if (fireOnChange && m_onChange) {
		m_onChange(m_value);
	}
	if (fireOnCommit && m_onCommit) {
		m_onCommit(m_value);
	}
}

template <typename T>
void SliderT<T>::setValue(T value, bool fireCallback) {
	setValueInternal(applyStepAndClamp(static_cast<float>(value)), fireCallback, false);
}

template <typename T>
void SliderT<T>::setValueFromNormalised(float t) {
	setValueInternal(valueFromNormalised(t), false, false);
}

template <typename T>
void SliderT<T>::setRange(T minVal, T maxVal) {
	m_min = minVal;
	m_max = maxVal;
	setValueInternal(applyStepAndClamp(static_cast<float>(m_value)), false, false);
}

template <typename T>
void SliderT<T>::setScale(SliderScale scale) {
	if (scale == SliderScale::Logarithmic && !(m_min > T{ 0 })) {
		std::fprintf(stderr,
			"SliderT(\"%s\"): logarithmic scale requires minVal > 0 (minVal=%g); a range "
			"crossing or touching zero must use SliderScale::Linear.\n",
			m_label.c_str(), static_cast<double>(m_min));
		assert(false && "SliderT: logarithmic scale requires minVal > 0");
	}
	m_scale = scale;
}

template <typename T>
void SliderT<T>::stepBy(float steps, bool shiftFast) {
	float range = static_cast<float>(m_max) - static_cast<float>(m_min);
	float stepAmount = (m_step > T{ 0 }) ? static_cast<float>(m_step) : range * 0.01f;
	if (shiftFast) {
		stepAmount *= 10.0f;
	}
	float raw = static_cast<float>(m_value) + steps * stepAmount;
	setValueInternal(applyStepAndClamp(raw), true, true);
}

template <typename T>
Vec2 SliderT<T>::preferredSize(const GuiContext& ctx) const {
	return { 0.0f, ctx.theme().rowHeight };
}

template <typename T>
void SliderT<T>::update(GuiContext& ctx) {
	if (m_bindTarget) {
		m_value = *m_bindTarget;
	}
	m_timeSinceLastPress += ctx.input().deltaTime;

	if (!m_enabled) {
		m_dragging = false;
		m_editBox.reset();
		return;
	}

	if (m_editBox) {
		updateTextEdit(ctx);
		return;
	}

	const InputState& in = ctx.input();
	const int leftIdx = static_cast<int>(MouseButton::Left);
	const bool leftPressed  = in.mousePressed[leftIdx];
	const bool leftDown     = in.mouseDown[leftIdx];
	const bool leftReleased = in.mouseReleased[leftIdx];
	const bool shiftHeld    = (in.modsDown & Mod::Shift) != 0;
	const bool ctrlHeld     = (in.modsDown & Mod::Ctrl) != 0;

	if (ctx.activeId() == id()) {
		if (leftPressed) {
			bool isDoubleClick = m_timeSinceLastPress < kDoubleClickSeconds;
			m_timeSinceLastPress = 0.0f;

			if (ctrlHeld) {
				if (m_onCtrlClick) {
					m_onCtrlClick(*this, ctx);
				} else {
					beginTextEdit(ctx);
				}
				m_dragging = false;
			} else if (isDoubleClick) {
				setValueInternal(m_initial, true, true);
				m_dragging = false;
			} else {
				Rect track = trackRect(ctx);
				float t = (track.w > 0.0f) ? (in.mousePos.x - track.x) / track.w : 0.0f;
				setValueInternal(valueFromNormalised(t), true, false);
				m_dragging = true;
				m_dragLastMouseX = in.mousePos.x;
			}
		} else if (leftDown && m_dragging) {
			Rect track = trackRect(ctx);
			float dx = in.mousePos.x - m_dragLastMouseX;
			m_dragLastMouseX = in.mousePos.x;
			if (track.w > 0.0f && dx != 0.0f) {
				float sensitivity = shiftHeld ? 0.1f : 1.0f;   // docs/gui/05, "Shift + drag"
				float t = normalisedPosition(m_value) + (dx / track.w) * sensitivity;
				setValueInternal(valueFromNormalised(t), true, false);
			}
		}

		if (leftReleased) {
			bool wasDragging = m_dragging;
			m_dragging = false;
			if (wasDragging && m_onCommit) {
				m_onCommit(m_value);
			}
		}
	}

	if (ctx.focusedId() == id()) {
		for (const KeyEvent& ev : in.keyQueue) {
			if (!ev.pressed) {
				continue;
			}
			bool shiftFast = (ev.mods & Mod::Shift) != 0;
			if (ev.key == Key::Left) {
				stepBy(-1.0f, shiftFast);
			} else if (ev.key == Key::Right) {
				stepBy(1.0f, shiftFast);
			}
		}
	}

	// Wheel while hovered steps the value and is meant to keep the panel from also
	// scrolling (docs/gui/05, "Wheel while hovered"). Panel scrolling itself is
	// scope, so there is nothing to consume from yet -- this just reacts to the wheel.
	if (ctx.hoveredId() == id() && in.wheelDelta != 0.0f) {
		stepBy(in.wheelDelta > 0.0f ? 1.0f : -1.0f, shiftHeld);
	}
}

template <typename T>
void SliderT<T>::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	RowSplit split = splitRow(ctx);

	if (!m_label.empty()) {
		Color textColor = effectivelyEnabled() ? th.text : th.textDisabled;
		dl.addTextClipped(ctx.font(), th.fontSize, split.label, textColor, m_label, Align::Start, Align::Center);
	}

	if (m_editBox) {
		m_editBox->draw(dl, ctx);
		return;
	}

	Rect track = trackRect(ctx);
	dl.addRectFilled(track, th.frameBg, track.h * 0.5f);

	float t = normalisedPosition(m_value);
	float fillEndX = std::round(track.x + t * track.w);
	if (fillEndX > track.x) {
		Rect fillRect{ track.x, track.y, fillEndX - track.x, track.h };
		dl.addRectFilled(fillRect, effectivelyEnabled() ? th.accent : th.accent.withAlpha(0.4f), track.h * 0.5f);
	}

	// Handle: a rounded rect, not a circle -- crisper without AA (docs/gui/05, "Drawing
	// recipe"). Snapped to integer pixels so it doesn't shimmer while dragging.
	float hw = th.sliderHandleWidth;
	Rect handle{ std::round(fillEndX - hw * 0.5f), std::round(split.control.y + 2.0f), hw,
	             std::round(split.control.h - 4.0f) };

	Color handleFill;
	if (!effectivelyEnabled()) {
		handleFill = th.frameBg.withAlpha(0.4f);
	} else if (ctx.activeId() == id()) {
		handleFill = th.accentActive;
	} else if (ctx.hoveredId() == id()) {
		handleFill = th.accentHovered;
	} else {
		handleFill = th.accent;
	}
	dl.addRectFilled(handle, handleFill, th.rounding);
	dl.addRect(handle, th.border, 1.0f, th.rounding);

	if (effectivelyEnabled() && ctx.focusedId() == id()) {
		dl.addRect(split.control, th.accent, 1.0f, th.rounding);
	}

	if (m_showValue) {
		std::string text = formatValue();
		Color valueColor = effectivelyEnabled() ? th.text : th.textDisabled;
		dl.addTextClipped(ctx.font(), th.fontSize, track, valueColor, text, Align::Center, Align::Center);
	}
}

template class SliderT<float>;
template class SliderT<int>;

} // namespace lightGraphics::ui
