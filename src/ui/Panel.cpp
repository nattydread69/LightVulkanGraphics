#include <lightVulkanGraphics/ui/Panel.h>
#include <lightVulkanGraphics/ui/DrawList.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>
#include <cmath>

namespace lightGraphics::ui {

namespace {
	Rect snapToPixels(const Rect& r) {
		return { std::round(r.x), std::round(r.y), std::round(r.w), std::round(r.h) };
	}

	constexpr float kDragThreshold = 4.0f;   // logical pixels, docs/gui/05 "Panel"
}

Panel::Panel(GuiContext& context, std::string title, Rect bounds, PanelFlags flags)
	: m_context(context), m_title(std::move(title)), m_bounds(bounds), m_flags(flags) {}

Panel::~Panel() = default;

void Panel::remove(Widget* widget) {
	m_widgets.erase(
		std::remove_if(m_widgets.begin(), m_widgets.end(),
			[widget](const std::unique_ptr<Widget>& w) { return w.get() == widget; }),
		m_widgets.end());
}

void Panel::clear() {
	m_widgets.clear();
}

void Panel::bringToFront() {
	m_context.bringPanelToFront(this);
}

bool Panel::hitTest(Vec2 p) const {
	if (!m_visible) {
		return false;
	}
	return m_bounds.contains(p);
}

Rect Panel::titleBarRect(const GuiContext& ctx) const {
	if (!hasTitleBar()) {
		return {};
	}
	return { m_bounds.x, m_bounds.y, m_bounds.w, ctx.theme().titleBarHeight };
}

void Panel::update(GuiContext& ctx) {
	const InputState& in = ctx.input();
	const bool leftDown     = in.mouseDown[static_cast<int>(MouseButton::Left)];
	const bool leftPressed  = in.mousePressed[static_cast<int>(MouseButton::Left)];

	if (hasTitleBar() && hasFlag(m_flags, PanelFlags::Movable)) {
		Rect titleRect = titleBarRect(ctx);

		if (!m_dragging && leftPressed && titleRect.contains(in.mousePos)) {
			m_dragging = true;
			m_dragArmed = false;
			m_dragStartMouse = in.mousePos;
			m_dragStartBounds = m_bounds;
		}

		if (m_dragging) {
			if (leftDown) {
				Vec2 delta = in.mousePos - m_dragStartMouse;
				if (!m_dragArmed) {
					float dist2 = delta.x * delta.x + delta.y * delta.y;
					if (dist2 > kDragThreshold * kDragThreshold) {
						m_dragArmed = true;
					}
				}
				if (m_dragArmed) {
					Rect moved = m_dragStartBounds;
					moved.x += delta.x;
					moved.y += delta.y;
					m_bounds = moved;
				}
			} else {
				m_dragging = false;
				m_dragArmed = false;
			}
		}
	}

	if (!m_collapsed) {
		for (auto& w : m_widgets) {
			if (w->visible()) {
				w->update(ctx);
			}
		}
	}
}

void Panel::layout(const GuiContext& ctx) {
	const Theme& th = ctx.theme();
	float titleOffset = hasTitleBar() ? th.titleBarHeight : 0.0f;
	float y = m_bounds.y + titleOffset + th.windowPadding;
	// Scrollbar gutter reservation arrives in phase 9; contentW always spans the full
	// interior for now.
	float contentW = m_bounds.w - 2 * th.windowPadding;
	float x = m_bounds.x + th.windowPadding;

	if (m_collapsed) {
		m_contentHeight = 0.0f;
		return;
	}

	for (auto& w : m_widgets) {
		if (!w->visible()) {
			continue;
		}
		if (w->hasBoundsOverride()) {
			Rect ov = w->boundsOverride();
			Rect abs{ m_bounds.x + ov.x, m_bounds.y + ov.y, ov.w, ov.h };
			w->setBounds(snapToPixels(abs));
			continue;
		}
		Vec2 pref = w->preferredSize(ctx);
		Rect r{ x, y - m_scrollY, contentW, pref.y };
		w->setBounds(snapToPixels(r));
		y += pref.y + th.itemSpacing;
	}
	m_contentHeight = y - (m_bounds.y + titleOffset) + th.windowPadding;
}

void Panel::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();

	if (!hasFlag(m_flags, PanelFlags::NoBackground)) {
		dl.addRectFilled(m_bounds, th.windowBg, th.rounding);
		dl.addRect(m_bounds, th.border, th.borderWidth, th.rounding);
	}

	if (hasTitleBar()) {
		Rect titleRect = titleBarRect(ctx);
		bool frontmost = ctx.panelCount() > 0 && ctx.panelAt(0) == this;
		dl.addRectFilled(titleRect, frontmost ? th.titleBgActive : th.titleBg, th.rounding);

		Rect titleTextRect = titleRect.insetXY(th.windowPadding * 0.5f, 0.0f);
		dl.addTextClipped(ctx.font(), th.fontSize, titleTextRect, th.text, m_title,
		                   Align::Start, Align::Center);
	}

	if (!m_collapsed) {
		for (auto& w : m_widgets) {
			if (w->visible()) {
				w->draw(dl, ctx);
			}
		}
	}
}

} // namespace lightGraphics::ui
