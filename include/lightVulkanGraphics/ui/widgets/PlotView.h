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

// docs/gui/05-widgets.md, "PlotView": a 2D x/y plot -- PlotLine's big brother. Where
// PlotLine is a label-sized sparkline of one value over time, PlotView is a real chart:
// several named series drawn as lines and/or points, axes with "nice" tick values, a
// grid, a legend, and the interaction you expect from a plotting window:
//
//   wheel           zoom about the cursor (Shift: X only, Ctrl: Y only)
//   left-drag       pan
//   double-click    reset the view (back to auto-fit, or whatever the application set)
//   legend click    show/hide that series
//   hover           readout of the nearest data point (or the cursor's coordinates)
//
// Each axis either auto-fits to the visible data (the default) or holds a range the
// application set. Zooming or panning puts the plot in a "user view" that overrides
// both until it is reset, and reports the new view through setOnViewChange() -- so an
// application plotting a function can resample it over exactly what is on screen.
//
// Data is never interpreted beyond its coordinates: series need not be sorted, and a
// non-finite y (or x) breaks a line rather than joining across the gap.

#include "../Widget.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lightGraphics::ui {

enum class PlotStyle { Line, Points, LineAndPoints };

// A visible window in data coordinates. Doubles, so deep zooms stay exact.
struct PlotRange {
	double xMin = 0.0, xMax = 1.0;
	double yMin = 0.0, yMax = 1.0;
};

class PlotView : public Widget {
public:
	// A non-empty label is drawn as a title above the plot.
	explicit PlotView(std::string label = {});

	// ---- series ----
	// Returns the new series' index (0, 1, 2, ...). Its colour comes from a palette that
	// starts with the theme's plotLine and accent colours unless setSeriesColor() is used.
	int addSeries(std::string name, PlotStyle style = PlotStyle::Line);
	int seriesCount() const { return static_cast<int>(m_series.size()); }
	void removeAllSeries();

	void setSeriesData(int series, std::vector<Vec2> points);
	// Appends one point, for data that arrives a sample at a time. With a rolling window
	// set, the oldest point is dropped once the series holds more than that many.
	void appendPoint(int series, Vec2 point);
	void setSeriesMaxPoints(int series, std::size_t maxPoints);   // 0 = unlimited (default)
	void clearSeries(int series);

	void setSeriesName(int series, std::string name);
	void setSeriesStyle(int series, PlotStyle style);
	void setSeriesColor(int series, Color color);
	void setSeriesLineWidth(int series, float px);     // default 2
	void setSeriesPointRadius(int series, float px);   // default 3
	// Hidden series are neither drawn nor counted by auto-fit; the legend still lists
	// them (dimmed) so they can be clicked back on.
	void setSeriesVisible(int series, bool visible);
	bool seriesVisible(int series) const;

	const std::vector<Vec2>& seriesData(int series) const;
	const std::string& seriesName(int series) const;

	// ---- axes and view ----
	void setXLabel(std::string label) { m_xLabel = std::move(label); }
	void setYLabel(std::string label) { m_yLabel = std::move(label); }
	// Fix an axis to a range (turns that axis's auto-fit off) or let it follow the data.
	// Any of these also discards a user zoom/pan: what the application asks for wins.
	void setXRange(double lo, double hi);
	void setYRange(double lo, double hi);
	void setAutoFit(bool fitX, bool fitY);
	bool autoFitX() const { return m_autoFitX; }
	bool autoFitY() const { return m_autoFitY; }

	// The range currently shown: the user's zoom/pan if there is one, otherwise the
	// fixed or auto-fitted range of each axis.
	PlotRange view() const;
	bool hasUserView() const { return m_userView.has_value(); }
	// Same as a double-click: drop the user's zoom/pan. Fires onViewChange if it had one.
	void resetView();
	// Fired after the user zooms, pans or resets -- not when the application changes
	// the view itself.
	void setOnViewChange(std::function<void(const PlotRange&)> fn) { m_onViewChange = std::move(fn); }

	// ---- appearance and behaviour ----
	void setHeight(float px) { m_height = px; }   // default 260
	void setShowLegend(bool show) { m_showLegend = show; }
	void setShowGrid(bool show) { m_showGrid = show; }
	// false: wheel/drag/double-click/legend clicks are ignored (hover readout stays).
	void setInteractive(bool interactive) { m_interactive = interactive; }

	// ---- geometry, for overlays and tests (valid after a layout pass) ----
	Rect plotRect(const GuiContext& ctx) const;          // the data area, inside the axes
	Vec2 dataToScreen(const GuiContext& ctx, Vec2 data) const;
	Vec2 screenToData(const GuiContext& ctx, Vec2 screen) const;
	// Empty when that series isn't in the legend (unnamed, no data, or no legend).
	Rect legendEntryRect(const GuiContext& ctx, int series) const;

	// "Nice" tick positions covering [lo, hi]: multiples of 1, 2 or 5 x 10^k, at most
	// maxTicks of them (at least 2 when the range allows).
	static std::vector<double> niceTicks(double lo, double hi, int maxTicks);
	// A tick label with just enough decimals for `step` ("0.5", "1500", "1e-06").
	static std::string formatTick(double value, double step);

	Vec2 preferredSize(const GuiContext&) const override { return { 0.0f, m_height }; }
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsFocus() const override { return false; }
	// Consumes the wheel while hovered (it zooms), so the owning panel doesn't scroll.
	bool wantsWheel() const override { return m_interactive; }

private:
	struct Series {
		std::string name;
		std::vector<Vec2> points;
		PlotStyle style = PlotStyle::Line;
		std::optional<Color> color;
		float lineWidth = 2.0f;
		float pointRadius = 3.0f;
		bool visible = true;
		std::size_t maxPoints = 0;
	};
	struct LegendRow {
		int series;
		Rect rect;
	};
	struct Layout {
		Rect plot;
		Rect legend;
		std::vector<LegendRow> legendRows;   // empty when there is no legend
		std::vector<double> xTicks, yTicks;
		double xStep = 1.0, yStep = 1.0;
	};

	Series* seriesAt(int index);
	const Series* seriesAt(int index) const;
	Color seriesColor(const GuiContext&, int index) const;
	// Bounds of every finite point in every visible series; false if there are none.
	bool dataBounds(PlotRange& out) const;
	PlotRange fittedView() const;
	Layout computeLayout(const GuiContext&, const PlotRange& view) const;
	int legendRowAt(const Layout&, Vec2 p) const;   // the series under p, or -1
	void setUserView(const PlotRange& view);
	// `v` shifted so the data point (anchorX, anchorY) lies under `screen` in v's own
	// layout -- see the .cpp for why the layout can move under a zoom or pan.
	PlotRange anchored(const GuiContext&, PlotRange v, double anchorX, double anchorY, Vec2 screen) const;
	void markDataChanged() { m_dataBoundsValid = false; }

	std::vector<Series> m_series;
	std::string m_xLabel, m_yLabel;

	bool m_autoFitX = true, m_autoFitY = true;
	double m_xLo = 0.0, m_xHi = 1.0, m_yLo = 0.0, m_yHi = 1.0;   // used when not auto-fitting
	std::optional<PlotRange> m_userView;

	float m_height = 260.0f;
	bool m_showLegend = true;
	bool m_showGrid = true;
	bool m_interactive = true;

	// Data bounds are cached -- auto-fit asks for them every frame, and walking every
	// point of every series each time adds up for long series.
	mutable bool m_dataBoundsValid = false;
	mutable bool m_dataBoundsAny = false;
	mutable PlotRange m_dataBounds;

	// Interaction state.
	enum class Drag { None, Pan, Legend };
	Drag m_drag = Drag::None;
	Vec2 m_dragStartMouse;
	PlotRange m_dragStartView;
	double m_dragAnchorX = 0.0, m_dragAnchorY = 0.0;   // the data point grabbed by a pan
	int m_pressLegendRow = -1;
	float m_time = 0.0f;
	float m_lastClickTime = -1000.0f;
	Vec2 m_lastClickPos;

	std::function<void(const PlotRange&)> m_onViewChange;
};

} // namespace lightGraphics::ui
