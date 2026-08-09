#pragma once

// docs/gui/05-widgets.md note under "CompositeWidget": Row divides its rect horizontally
// by weight, minus theme.itemSpacing between children. Height is the max of children's
// preferred heights. Default weights are all equal.

#include "CompositeWidget.h"

#include <vector>

namespace lightGraphics::ui {

class Row : public CompositeWidget {
public:
	Row() = default;

	// Set custom weights for children (must match childCount() when layout is called).
	// Default: all equal.
	void setWeights(std::vector<float> weights) { m_weights = std::move(weights); }

	Vec2 preferredSize(const GuiContext& ctx) const override;
	void layout(const GuiContext& ctx) override;

private:
	std::vector<float> m_weights;
};

} // namespace lightGraphics::ui
