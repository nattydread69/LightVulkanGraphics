// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Light Vulkan Graphics
// Copyright (C) 2026 Dr. Nathanael John Inkson
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <lightVulkanGraphics/ui/widgets/DragValue.h>
#include <lightVulkanGraphics/ui/widgets/TextBox.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace lightGraphics::ui {

template <typename T>
DragValueT<T>::DragValueT(std::string label, T initial, float speed)
	: m_value(initial), m_speed(speed) {
	setLabel(std::move(label));
}

template <typename T>
T DragValueT<T>::clampSoft(T value) const {
	if (!m_hasSoftRange) {
		return value;
	}
	return std::clamp(value, std::min(m_softLo, m_softHi), std::max(m_softLo, m_softHi));
}

template <typename T>
std::string DragValueT<T>::formatValue() const {
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
	return std::string(buf);
}

template <typename T>
std::string DragValueT<T>::formatValueForEdit() const {
	char buf[64];
	if constexpr (std::is_integral<T>::value) {
		std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(m_value));
	} else {
		std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(m_value));
	}
	return std::string(buf);
}

template <typename T>
void DragValueT<T>::beginTextEdit(GuiContext&) {
	m_editBox = std::make_unique<TextBox>("", formatValueForEdit());
	m_editBox->setFilter(std::is_integral<T>::value ? TextFilter::Integer : TextFilter::Decimal);
	m_editBox->setForcedFocus(true);
	m_editBox->selectAll();
}

template <typename T>
bool DragValueT<T>::updateTextEdit(GuiContext& ctx) {
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
		endSession = true;
	}

	if (endSession) {
		if (!cancelled) {
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
				setValueInternal(clampSoft(static_cast<T>(parsed)), true, true);
			}
		}
		m_editBox.reset();
	}
	return endSession;
}

template <typename T>
void DragValueT<T>::setValueInternal(T value, bool fireOnChange, bool fireOnCommit) {
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
void DragValueT<T>::setValue(T value, bool fireCallback) {
	setValueInternal(clampSoft(value), fireCallback, false);
}

template <typename T>
Vec2 DragValueT<T>::preferredSize(const GuiContext& ctx) const {
	return { 0.0f, ctx.theme().rowHeight };
}

template <typename T>
void DragValueT<T>::update(GuiContext& ctx) {
	if (m_bindTarget) {
		m_value = *m_bindTarget;
	}
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
	const bool coarseHeld   = (in.modsDown & (Mod::Alt | Mod::Ctrl)) != 0;   // docs/gui/05, "Alt/Ctrl = 10x"

	if (ctx.activeId() == id()) {
		if (leftPressed) {
			// Ctrl held AT PRESS is "Ctrl-click for text entry, as with Slider" (docs/gui/05,
			// "DragValue"); Ctrl/Alt held only once a drag is already underway is instead the
			// 10x coarse-adjustment modifier handled in the drag branch below.
			bool ctrlHeld = (in.modsDown & Mod::Ctrl) != 0;
			if (ctrlHeld) {
				if (m_onCtrlClick) {
					m_onCtrlClick(*this, ctx);
				} else {
					beginTextEdit(ctx);
				}
				m_dragging = false;
			} else {
				m_dragging = true;
				m_dragLastMouseX = in.mousePos.x;
				m_dragAccum = 0.0f;
			}
		} else if (leftDown && m_dragging) {
			float dx = in.mousePos.x - m_dragLastMouseX;
			m_dragLastMouseX = in.mousePos.x;
			float sensitivity = shiftHeld ? 0.1f : (coarseHeld ? 10.0f : 1.0f);
			m_dragAccum += dx * m_speed * sensitivity;

			if constexpr (std::is_integral<T>::value) {
				T whole = static_cast<T>(std::trunc(m_dragAccum));
				if (whole != T{ 0 }) {
					m_dragAccum -= static_cast<float>(whole);
					setValueInternal(clampSoft(static_cast<T>(m_value + whole)), true, false);
				}
			} else if (m_dragAccum != 0.0f) {
				T newVal = clampSoft(static_cast<T>(m_value + static_cast<T>(m_dragAccum)));
				m_dragAccum = 0.0f;
				setValueInternal(newVal, true, false);
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
}

template <typename T>
void DragValueT<T>::draw(DrawList& dl, const GuiContext& ctx) const {
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

	// "Visually identical to a text box, which correctly signals this holds a number you
	// can edit" (docs/gui/05, "DragValue").
	const Rect& box = split.control;
	Color fill;
	if (!effectivelyEnabled()) {
		fill = th.frameBg.withAlpha(0.4f);
	} else if (ctx.activeId() == id()) {
		fill = th.frameBgActive;
	} else if (ctx.hoveredId() == id()) {
		fill = th.frameBgHovered;
	} else {
		fill = th.frameBg;
	}
	dl.addRectFilled(box, fill, th.rounding);

	Color borderColor = (effectivelyEnabled() && ctx.focusedId() == id()) ? th.accent : th.border;
	dl.addRect(box, borderColor, 1.0f, th.rounding);

	std::string text = formatValue();
	Color textColor = effectivelyEnabled() ? th.text : th.textDisabled;
	// Tabular: this is a live-updating numeric readout, exactly the case
	// docs/gui/03-text-and-fonts.md's "Tabular figures" targets -- without it, each
	// digit's own natural (proportional) width means the value visibly jitters
	// horizontally as it changes. The label text above is prose, not digits, so it
	// stays untouched.
	dl.addTextClipped(ctx.font(), th.fontSize, box, textColor, text, Align::Center, Align::Center,
	                   TextFlags::Tabular);
}

template class DragValueT<float>;
template class DragValueT<int>;

} // namespace lightGraphics::ui
