#include <lightVulkanGraphics/ui/widgets/Button.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

namespace lightGraphics::ui {

Vec2 Button::preferredSize(const GuiContext& ctx) const {
	return { 0.0f, ctx.theme().rowHeight };
}

void Button::update(GuiContext& ctx) {
	if (!m_enabled) {
		m_held = false;
		return;
	}

	const InputState& in = ctx.input();
	const bool leftReleased = in.mouseReleased[static_cast<int>(MouseButton::Left)];

	// Mouse: docs/gui/05, "Button" -- capture is already established generically by
	// GuiContext::update() before this call, so this just observes it.
	if (ctx.activeId() == id()) {
		m_held = hitTest(in.mousePos);
		if (leftReleased && m_held) {
			if (m_isToggle) {
				m_pressed = !m_pressed;
			}
			if (m_onClick) {
				m_onClick();
			}
		}
	} else {
		m_held = false;
	}

	// Keyboard: Space/Enter activate while focused.
	if (ctx.focusedId() == id()) {
		for (const KeyEvent& ev : in.keyQueue) {
			if (!ev.pressed || ev.repeat) {
				continue;
			}
			if (ev.key == Key::Enter || ev.key == Key::Space) {
				if (m_isToggle) {
					m_pressed = !m_pressed;
				}
				if (m_onClick) {
					m_onClick();
				}
			}
		}
	}
}

void Button::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();

	Color fill;
	Color textColor = th.text;
	if (!m_enabled) {
		fill = th.frameBg.withAlpha(0.4f);
		textColor = th.textDisabled;
	} else if (ctx.activeId() == id() && m_held) {
		fill = th.accentActive;
	} else if (ctx.hoveredId() == id()) {
		fill = th.accentHovered;
	} else if (m_isToggle && m_pressed) {
		fill = th.accent;
	} else {
		fill = th.frameBg;
	}

	dl.addRectFilled(m_bounds, fill, th.rounding);

	if (m_enabled && ctx.focusedId() == id()) {
		dl.addRect(m_bounds, th.accent, 1.0f, th.rounding);
	}

	dl.addTextClipped(ctx.font(), th.fontSize, m_bounds, textColor, m_label, Align::Center, Align::Center);
}

} // namespace lightGraphics::ui
