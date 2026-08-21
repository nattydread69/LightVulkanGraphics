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

#include <lightVulkanGraphics/ui/widgets/PlotLine.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <vector>

namespace lightGraphics::ui {

PlotLine::PlotLine(std::string label, size_t historySize)
	: m_historySize(historySize) {
	setLabel(std::move(label));
}

void PlotLine::push(float sample) {
	m_samples.push_back(sample);
	if (m_samples.size() > m_historySize) {
		m_samples.pop_front();
	}
	updateAutoScale();
}

void PlotLine::setValues(std::span<const float> values) {
	m_samples.clear();
	for (float v : values) {
		m_samples.push_back(v);
		if (m_samples.size() > m_historySize) {
			m_samples.pop_front();
		}
	}
	updateAutoScale();
}

void PlotLine::setRange(float lo, float hi) {
	if (std::isnan(lo) || std::isnan(hi)) {
		m_autoScale = true;
		updateAutoScale();
	} else {
		m_autoScale = false;
		m_rangeMin = lo;
		m_rangeMax = hi;
	}
}

void PlotLine::setHeight(float px) {
	m_height = px;
}

void PlotLine::setShowLatestValue(bool show) {
	m_showLatestValue = show;
}

void PlotLine::updateAutoScale() {
	if (!m_autoScale || m_samples.empty()) {
		return;
	}

	float minVal = std::numeric_limits<float>::max();
	float maxVal = std::numeric_limits<float>::lowest();

	for (float sample : m_samples) {
		minVal = std::min(minVal, sample);
		maxVal = std::max(maxVal, sample);
	}

	// If all samples are the same, expand range slightly
	if (minVal == maxVal) {
		minVal -= 0.5f;
		maxVal += 0.5f;
	}

	// Apply hysteresis: expand but don't shrink (much)
	// This prevents the plot from jumping on every new extremum
	if (minVal < m_autoScaleMin) {
		m_autoScaleMin = minVal;
	} else if (minVal > m_autoScaleMin * 0.95f) {
		// Slowly contract if values no longer reach the old minimum
		m_autoScaleMin = std::max(minVal, m_autoScaleMin * 0.99f);
	}

	if (maxVal > m_autoScaleMax) {
		m_autoScaleMax = maxVal;
	} else if (maxVal < m_autoScaleMax * 1.05f) {
		// Slowly contract if values no longer reach the old maximum
		m_autoScaleMax = std::min(maxVal, m_autoScaleMax * 1.01f);
	}

	m_rangeMin = m_autoScaleMin;
	m_rangeMax = m_autoScaleMax;
}

Vec2 PlotLine::preferredSize(const GuiContext&) const {
	return { 0.0f, m_height };
}

void PlotLine::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}

	const Theme& th = ctx.theme();
	RowSplit split = splitRow(ctx);

	if (!m_label.empty()) {
		Color labelColor = effectivelyEnabled() ? th.text : th.textDisabled;
		dl.addTextClipped(ctx.font(), th.fontSize, split.label, labelColor, m_label, Align::Start, Align::Center);
	}

	const Rect& plotRect = split.control;

	// Draw plot background
	dl.addRectFilled(plotRect, th.frameBg, th.rounding);
	dl.addRect(plotRect, th.border, 1.0f, th.rounding);

	// If no samples, nothing more to draw
	if (m_samples.empty()) {
		return;
	}

	// Build polyline points from samples
	std::vector<Vec2> points;
	points.reserve(m_samples.size());

	float range = m_rangeMax - m_rangeMin;
	if (range < 1e-6f) {
		range = 1.0f;  // Avoid division by zero
	}

	for (size_t i = 0; i < m_samples.size(); ++i) {
		float sample = m_samples[i];

		// Map sample value to plot rect
		float normalizedY = (sample - m_rangeMin) / range;
		normalizedY = std::clamp(normalizedY, 0.0f, 1.0f);

		// X coordinate: spread samples across the plot width
		float x = plotRect.x + (i / static_cast<float>(std::max(size_t(1), m_samples.size() - 1))) * plotRect.w;
		// Y coordinate: flip so top is max value
		float y = plotRect.y + plotRect.h * (1.0f - normalizedY);

		points.emplace_back(x, y);
	}

	// Draw the polyline
	if (points.size() > 1) {
		dl.addPolyline(points.data(), static_cast<int>(points.size()), th.accent, 1.0f, false);
	}

	// Draw latest value text if requested
	if (m_showLatestValue && !m_samples.empty()) {
		float latestValue = m_samples.back();
		std::ostringstream oss;
		oss.precision(3);
		oss << latestValue;
		std::string valueText = oss.str();

		Color textColor = effectivelyEnabled() ? th.text : th.textDisabled;
		Rect textRect = plotRect.inset(th.framePadding);
		dl.addTextClipped(ctx.font(), th.fontSize, textRect, textColor, valueText, Align::End, Align::Start);
	}
}

} // namespace lightGraphics::ui
