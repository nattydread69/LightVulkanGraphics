#include <lightVulkanGraphics/ui/Widget.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <atomic>

namespace lightGraphics::ui {

namespace {
	// docs/gui/01-architecture.md ("Widget identity") assigns WidgetIds from "a monotonic
	// counter in GuiContext". A single process-wide counter is used here instead: Widget's
	// base constructor runs inside Panel::add<W>()'s make_unique<W>(...), before the new
	// widget is attached to a Panel or has any way to reach the owning GuiContext, so
	// there is no GuiContext reference available at the point an id would need to be
	// allocated. A global counter still satisfies both of the doc's actual requirements
	// -- ids are never reused within a session, 0 stays reserved for kInvalidWidgetId --
	// and it avoids a circular Panel<->GuiContext header dependency for what is otherwise
	// a one-line detail.
	std::atomic<WidgetId> g_nextWidgetId{ 1 };
}

Widget::Widget() : m_id(g_nextWidgetId.fetch_add(1, std::memory_order_relaxed)) {}

void Widget::setBoundsOverride(const Rect& panelRelative) {
	m_boundsOverride = panelRelative;
}

void Widget::clearBoundsOverride() {
	m_boundsOverride.reset();
}

Widget::RowSplit Widget::splitRow(const GuiContext& ctx) const {
	if (m_label.empty()) {
		return { Rect{}, m_bounds };
	}

	const Theme& theme = ctx.theme();
	float labelW = (m_labelWidthOverride >= 0.0f) ? m_labelWidthOverride
	                                               : m_bounds.w * theme.labelWidthRatio;

	Rect labelRect{ m_bounds.x, m_bounds.y, labelW - theme.itemSpacing, m_bounds.h };
	Rect controlRect{ m_bounds.x + labelW, m_bounds.y, m_bounds.w - labelW, m_bounds.h };
	return { labelRect, controlRect };
}

} // namespace lightGraphics::ui
