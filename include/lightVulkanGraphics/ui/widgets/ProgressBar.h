#pragma once

// docs/gui/05-widgets.md, "ProgressBar": Non-interactive. setFraction clamps to [0,1].
// Indeterminate mode sweeps a bar of 30% track width on a 1.5 second loop using
// accumulated deltaTime.

#include "../Widget.h"

#include <string>

namespace lightGraphics::ui {

class ProgressBar : public Widget {
public:
	explicit ProgressBar(std::string label);

	void setFraction(float frac);           // clamped to [0,1]
	void setIndeterminate(bool indeterminate);
	void setOverlayText(std::string text);  // e.g. "1420 / 5000 steps"

	Vec2 preferredSize(const GuiContext& ctx) const override;
	void update(GuiContext& ctx) override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsCapture() const override { return false; }

private:
	float m_fraction = 0.0f;
	bool m_indeterminate = false;
	std::string m_overlayText;
	double m_accumulatedTime = 0.0;
};

} // namespace lightGraphics::ui
