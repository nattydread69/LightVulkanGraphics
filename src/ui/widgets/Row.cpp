#include <lightVulkanGraphics/ui/widgets/Row.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

namespace lightGraphics::ui {

Vec2 Row::preferredSize(const GuiContext& ctx) const {
	if (childCount() == 0) {
		return { 0.0f, ctx.theme().rowHeight };
	}

	// Height is the max preferred height of children
	float maxHeight = ctx.theme().rowHeight;
	for (std::size_t i = 0; i < childCount(); ++i) {
		Vec2 childPref = childAt(i)->preferredSize(ctx);
		maxHeight = std::max(maxHeight, childPref.y);
	}

	return { 0.0f, maxHeight };
}

void Row::layout(const GuiContext& ctx) {
	const std::size_t count = childCount();
	if (count == 0) {
		return;
	}

	const Theme& th = ctx.theme();

	// Calculate total weight
	float totalWeight = 0.0f;
	if (m_weights.empty() || m_weights.size() != count) {
		// Default: all weights equal
		totalWeight = static_cast<float>(count);
	} else {
		for (float w : m_weights) {
			totalWeight += w;
		}
	}

	// Available width: subtract spacing between children
	float spacing = th.itemSpacing * static_cast<float>(count - 1);
	float availableWidth = m_bounds.w - spacing;

	// Position children left to right
	float x = m_bounds.x;
	for (std::size_t i = 0; i < count; ++i) {
		float weight = (m_weights.empty() || m_weights.size() != count)
		               ? 1.0f
		               : m_weights[i];
		float childWidth = availableWidth * (weight / totalWeight);

		Widget* child = childAt(i);
		child->setBounds({ x, m_bounds.y, childWidth, m_bounds.h });
		child->layout(ctx);

		x += childWidth + th.itemSpacing;
	}
}

} // namespace lightGraphics::ui
