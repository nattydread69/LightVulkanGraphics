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

#pragma once

// docs/gui/05-widgets.md, "PlotLine": A sparkline. Draw with a single addPolyline over
// the ring buffer mapped to the plot rect. Auto-scale should track a running min/max with
// mild hysteresis, or the plot jumps distractingly on every new extreme.

#include "../Widget.h"

#include <deque>
#include <span>
#include <string>

namespace lightGraphics::ui {

class PlotLine : public Widget {
public:
	PlotLine(std::string label, size_t historySize = 256);

	void push(float sample);                   // ring buffer
	void setValues(std::span<const float> values);  // or supply externally
	void setRange(float lo, float hi);         // NaN, NaN = auto-scale
	void setHeight(float px);
	void setShowLatestValue(bool show);
	// Only the first `fraction` (0..1, clamped) of the current samples are
	// drawn -- the X mapping still spans the FULL sample count, so a partial
	// reveal stops partway across the plot rather than being rescaled to
	// fill it (a curve "growing in" from the left as a caller advances this
	// in step with some external clock). Reset to 1.0 (show everything, the
	// original behaviour) by setValues().
	void setRevealFraction(float fraction);
	// Draws a vertical line at this fraction (0..1) across the plot's full
	// height, in a colour distinct from the polyline itself -- a moving
	// "where is `now`" marker over an otherwise fully-visible curve.
	// Negative or NaN (the default) hides it.
	void setPlayheadFraction(float fraction);

	Vec2 preferredSize(const GuiContext& ctx) const override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsCapture() const override { return false; }

private:
	void updateAutoScale();

	std::deque<float> m_samples;
	size_t m_historySize;
	float m_rangeMin = 0.0f, m_rangeMax = 1.0f;
	float m_autoScaleMin = 0.0f, m_autoScaleMax = 1.0f;
	bool m_autoScale = true;
	float m_height = 40.0f;
	bool m_showLatestValue = false;
	float m_revealFraction = 1.0f;
	float m_playheadFraction = -1.0f;
};

} // namespace lightGraphics::ui
