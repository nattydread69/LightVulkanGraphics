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
};

} // namespace lightGraphics::ui
