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

#include <lightVulkanGraphics/ui/widgets/PlotView.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace lightGraphics::ui {

namespace {
	constexpr float kDoubleClickSeconds = 0.4f;
	constexpr float kDoubleClickSlop = 6.0f;      // px the second click may land from the first
	constexpr float kHoverSnapRadius = 10.0f;     // px within which the readout snaps to a point
	constexpr float kTickSizeRatio = 0.85f;       // tick labels, axis labels: slightly smaller text
	constexpr float kMinPixelsPerXTick = 40.0f;   // densest X spacing tried; labels then decide
	constexpr float kXLabelGap = 14.0f;           // px kept clear between neighbouring X labels
	constexpr float kMinPixelsPerYTick = 40.0f;
	constexpr double kFitPadX = 0.03;             // auto-fit margin, as a fraction of the span
	constexpr double kFitPadY = 0.08;
	constexpr double kWheelZoomBase = 0.85;       // one wheel notch shows 85% of the span
	constexpr double kMinSpan = 1e-12;
	constexpr double kMaxSpan = 1e15;

	// Series colours after the theme's own plotLine and accent: a qualitative palette
	// whose entries stay distinguishable on dark, light and high-contrast backgrounds.
	constexpr Color kPalette[] = {
		{ 0xF2, 0x8E, 0x2B, 0xFF }, { 0xE1, 0x57, 0x59, 0xFF }, { 0xB0, 0x7A, 0xA1, 0xFF },
		{ 0xED, 0xC9, 0x48, 0xFF }, { 0x76, 0xB7, 0xB2, 0xFF }, { 0xFF, 0x9D, 0xA7, 0xFF },
		{ 0x9C, 0x75, 0x5F, 0xFF },
	};

	bool finite(Vec2 p) {
		return std::isfinite(p.x) && std::isfinite(p.y);
	}

	// The 1-2-5 sequence: the next / previous "nice" step either side of `step`.
	double nextNiceStep(double step) {
		double const decade = std::pow(10.0, std::floor(std::log10(step) + 1e-9));
		double const m = step / decade;
		return (m < 1.5 ? 2.0 : (m < 3.5 ? 5.0 : 10.0)) * decade;
	}
	double prevNiceStep(double step) {
		double const decade = std::pow(10.0, std::floor(std::log10(step) + 1e-9));
		double const m = step / decade;
		return (m < 1.5 ? 0.5 : (m < 3.5 ? 1.0 : 2.0)) * decade;
	}
	double niceStepNear(double rough) {
		double const decade = std::pow(10.0, std::floor(std::log10(rough)));
		double const m = rough / decade;
		return (m < 1.5 ? 1.0 : (m < 3.5 ? 2.0 : (m < 7.5 ? 5.0 : 10.0))) * decade;
	}
	long long tickCount(double lo, double hi, double step) {
		return static_cast<long long>(std::floor(hi / step + 1e-9)) -
		       static_cast<long long>(std::ceil(lo / step - 1e-9)) + 1;
	}

	// The step niceTicks() uses for [lo, hi]; 0 when there is no usable range.
	double tickStepFor(double lo, double hi, int maxTicks) {
		double const span = hi - lo;
		if (!(span > 0.0) || !std::isfinite(span)) {
			return 0.0;
		}
		maxTicks = std::max(2, maxTicks);
		double step = niceStepNear(span / static_cast<double>(maxTicks - 1));
		while (tickCount(lo, hi, step) > maxTicks) {
			step = nextNiceStep(step);
		}
		// A narrow range can leave a single tick (or none) at that step; go finer while
		// that still fits under maxTicks, so the axis always has two labels to read.
		while (tickCount(lo, hi, step) < 2) {
			double const finer = prevNiceStep(step);
			if (tickCount(lo, hi, finer) > maxTicks) {
				break;
			}
			step = finer;
		}
		return step;
	}

	std::vector<double> ticksForStep(double lo, double hi, double step) {
		std::vector<double> ticks;
		if (step <= 0.0) {
			return ticks;
		}
		long long const first = static_cast<long long>(std::ceil(lo / step - 1e-9));
		long long const last = static_cast<long long>(std::floor(hi / step + 1e-9));
		for (long long k = first; k <= last; ++k) {
			double v = static_cast<double>(k) * step;
			if (std::abs(v) < step * 1e-9) {
				v = 0.0;   // no "-0" from a product that only just rounds below zero
			}
			ticks.push_back(v);
		}
		return ticks;
	}

	// Liang-Barsky: trims the segment (x0,y0)-(x1,y1) to the rectangle, in doubles.
	// Returns false when none of it is inside.
	bool clipSegment(double& x0, double& y0, double& x1, double& y1,
	                 double left, double top, double right, double bottom) {
		double const dx = x1 - x0;
		double const dy = y1 - y0;
		double t0 = 0.0, t1 = 1.0;
		const double p[4] = { -dx, dx, -dy, dy };
		const double q[4] = { x0 - left, right - x0, y0 - top, bottom - y0 };
		for (int i = 0; i < 4; ++i) {
			if (p[i] == 0.0) {
				if (q[i] < 0.0) {
					return false;   // parallel to this edge and outside it
				}
				continue;
			}
			double const t = q[i] / p[i];
			if (p[i] < 0.0) {
				t0 = std::max(t0, t);
			} else {
				t1 = std::min(t1, t);
			}
			if (t0 > t1) {
				return false;
			}
		}
		double const sx = x0, sy = y0;
		x0 = sx + t0 * dx;
		y0 = sy + t0 * dy;
		x1 = sx + t1 * dx;
		y1 = sy + t1 * dy;
		return true;
	}

	PlotRange sanitized(PlotRange r) {
		auto fixAxis = [](double& lo, double& hi) {
			if (!std::isfinite(lo) || !std::isfinite(hi)) {
				lo = 0.0;
				hi = 1.0;
			}
			if (hi < lo) {
				std::swap(lo, hi);
			}
			double const centre = 0.5 * (lo + hi);
			double const minSpan = kMinSpan * std::max(1.0, std::abs(centre));
			if (hi - lo < minSpan) {
				lo = centre - 0.5 * minSpan;
				hi = centre + 0.5 * minSpan;
			} else if (hi - lo > kMaxSpan) {
				lo = centre - 0.5 * kMaxSpan;
				hi = centre + 0.5 * kMaxSpan;
			}
		};
		fixAxis(r.xMin, r.xMax);
		fixAxis(r.yMin, r.yMax);
		return r;
	}

	// [lo, hi] grown by `pad` of its span; a zero-width span (one point, a flat line)
	// gets a span of its own so it isn't drawn as an infinitely thin sliver.
	void padAxis(double& lo, double& hi, double pad) {
		double const span = hi - lo;
		if (span <= 0.0) {
			double const half = std::max(std::abs(lo) * 0.1, 1.0);
			lo -= half;
			hi += half;
		} else {
			lo -= span * pad;
			hi += span * pad;
		}
	}

	bool sameView(const PlotRange& a, const PlotRange& b) {
		return a.xMin == b.xMin && a.xMax == b.xMax && a.yMin == b.yMin && a.yMax == b.yMax;
	}
}

PlotView::PlotView(std::string label) {
	setLabel(std::move(label));
}

// ---- series --------------------------------------------------------------------------

PlotView::Series* PlotView::seriesAt(int index) {
	return (index >= 0 && index < seriesCount()) ? &m_series[static_cast<std::size_t>(index)] : nullptr;
}

const PlotView::Series* PlotView::seriesAt(int index) const {
	return (index >= 0 && index < seriesCount()) ? &m_series[static_cast<std::size_t>(index)] : nullptr;
}

int PlotView::addSeries(std::string name, PlotStyle style) {
	Series s;
	s.name = std::move(name);
	s.style = style;
	m_series.push_back(std::move(s));
	markDataChanged();
	return seriesCount() - 1;
}

void PlotView::removeAllSeries() {
	m_series.clear();
	markDataChanged();
}

void PlotView::setSeriesData(int series, std::vector<Vec2> points) {
	if (Series* s = seriesAt(series)) {
		s->points = std::move(points);
		if (s->maxPoints > 0 && s->points.size() > s->maxPoints) {
			s->points.erase(s->points.begin(), s->points.end() - static_cast<std::ptrdiff_t>(s->maxPoints));
		}
		markDataChanged();
	}
}

void PlotView::appendPoint(int series, Vec2 point) {
	Series* s = seriesAt(series);
	if (!s) {
		return;
	}
	s->points.push_back(point);
	bool dropped = false;
	if (s->maxPoints > 0 && s->points.size() > s->maxPoints) {
		s->points.erase(s->points.begin(), s->points.end() - static_cast<std::ptrdiff_t>(s->maxPoints));
		dropped = true;
	}
	// Streaming is the common case for appendPoint(), so keep the cached bounds valid
	// by growing them rather than rescanning -- unless a point fell out of the window,
	// which may have been the one holding a bound.
	if (dropped) {
		markDataChanged();
	} else if (m_dataBoundsValid && s->visible && finite(point)) {
		if (!m_dataBoundsAny) {
			m_dataBounds = { point.x, point.x, point.y, point.y };
			m_dataBoundsAny = true;
		} else {
			m_dataBounds.xMin = std::min(m_dataBounds.xMin, static_cast<double>(point.x));
			m_dataBounds.xMax = std::max(m_dataBounds.xMax, static_cast<double>(point.x));
			m_dataBounds.yMin = std::min(m_dataBounds.yMin, static_cast<double>(point.y));
			m_dataBounds.yMax = std::max(m_dataBounds.yMax, static_cast<double>(point.y));
		}
	}
}

void PlotView::setSeriesMaxPoints(int series, std::size_t maxPoints) {
	if (Series* s = seriesAt(series)) {
		s->maxPoints = maxPoints;
		if (maxPoints > 0 && s->points.size() > maxPoints) {
			s->points.erase(s->points.begin(), s->points.end() - static_cast<std::ptrdiff_t>(maxPoints));
			markDataChanged();
		}
	}
}

void PlotView::clearSeries(int series) {
	if (Series* s = seriesAt(series)) {
		s->points.clear();
		markDataChanged();
	}
}

void PlotView::setSeriesName(int series, std::string name) {
	if (Series* s = seriesAt(series)) {
		s->name = std::move(name);
	}
}

void PlotView::setSeriesStyle(int series, PlotStyle style) {
	if (Series* s = seriesAt(series)) {
		s->style = style;
	}
}

void PlotView::setSeriesColor(int series, Color color) {
	if (Series* s = seriesAt(series)) {
		s->color = color;
	}
}

void PlotView::setSeriesLineWidth(int series, float px) {
	if (Series* s = seriesAt(series)) {
		s->lineWidth = std::max(0.5f, px);
	}
}

void PlotView::setSeriesPointRadius(int series, float px) {
	if (Series* s = seriesAt(series)) {
		s->pointRadius = std::max(0.5f, px);
	}
}

void PlotView::setSeriesVisible(int series, bool visible) {
	if (Series* s = seriesAt(series)) {
		if (s->visible != visible) {
			s->visible = visible;
			markDataChanged();
		}
	}
}

bool PlotView::seriesVisible(int series) const {
	const Series* s = seriesAt(series);
	return s && s->visible;
}

const std::vector<Vec2>& PlotView::seriesData(int series) const {
	static const std::vector<Vec2> kNone;
	const Series* s = seriesAt(series);
	return s ? s->points : kNone;
}

const std::string& PlotView::seriesName(int series) const {
	static const std::string kNone;
	const Series* s = seriesAt(series);
	return s ? s->name : kNone;
}

Color PlotView::seriesColor(const GuiContext& ctx, int index) const {
	const Series* s = seriesAt(index);
	if (s && s->color) {
		return *s->color;
	}
	const Theme& th = ctx.theme();
	if (index == 0) {
		return th.plotLine;
	}
	if (index == 1) {
		return th.accent;
	}
	constexpr int kPaletteSize = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));
	return kPalette[(index - 2) % kPaletteSize];
}

// ---- view ----------------------------------------------------------------------------

void PlotView::setXRange(double lo, double hi) {
	m_xLo = lo;
	m_xHi = hi;
	m_autoFitX = false;
	m_userView.reset();
}

void PlotView::setYRange(double lo, double hi) {
	m_yLo = lo;
	m_yHi = hi;
	m_autoFitY = false;
	m_userView.reset();
}

void PlotView::setAutoFit(bool fitX, bool fitY) {
	m_autoFitX = fitX;
	m_autoFitY = fitY;
	m_userView.reset();
}

bool PlotView::dataBounds(PlotRange& out) const {
	if (!m_dataBoundsValid) {
		m_dataBoundsAny = false;
		for (const Series& s : m_series) {
			if (!s.visible) {
				continue;
			}
			for (Vec2 p : s.points) {
				if (!finite(p)) {
					continue;
				}
				if (!m_dataBoundsAny) {
					m_dataBounds = { p.x, p.x, p.y, p.y };
					m_dataBoundsAny = true;
				} else {
					m_dataBounds.xMin = std::min(m_dataBounds.xMin, static_cast<double>(p.x));
					m_dataBounds.xMax = std::max(m_dataBounds.xMax, static_cast<double>(p.x));
					m_dataBounds.yMin = std::min(m_dataBounds.yMin, static_cast<double>(p.y));
					m_dataBounds.yMax = std::max(m_dataBounds.yMax, static_cast<double>(p.y));
				}
			}
		}
		m_dataBoundsValid = true;
	}
	out = m_dataBounds;
	return m_dataBoundsAny;
}

PlotRange PlotView::fittedView() const {
	PlotRange r{ m_xLo, m_xHi, m_yLo, m_yHi };
	if (!m_autoFitX && !m_autoFitY) {
		return sanitized(r);
	}

	PlotRange b;
	bool const any = dataBounds(b);
	if (m_autoFitX) {
		r.xMin = any ? b.xMin : 0.0;
		r.xMax = any ? b.xMax : 1.0;
		if (any) {
			padAxis(r.xMin, r.xMax, kFitPadX);
		}
	}
	if (m_autoFitY) {
		if (!m_autoFitX && any) {
			// X is pinned: fit Y to just the points inside that X range, which is what
			// is actually on screen -- not to data scrolled off either side.
			double lo = std::numeric_limits<double>::infinity();
			double hi = -lo;
			for (const Series& s : m_series) {
				if (!s.visible) {
					continue;
				}
				for (Vec2 p : s.points) {
					if (finite(p) && p.x >= r.xMin && p.x <= r.xMax) {
						lo = std::min(lo, static_cast<double>(p.y));
						hi = std::max(hi, static_cast<double>(p.y));
					}
				}
			}
			if (lo <= hi) {
				r.yMin = lo;
				r.yMax = hi;
			} else {
				r.yMin = b.yMin;
				r.yMax = b.yMax;
			}
		} else {
			r.yMin = any ? b.yMin : 0.0;
			r.yMax = any ? b.yMax : 1.0;
		}
		if (any) {
			padAxis(r.yMin, r.yMax, kFitPadY);
		}
	}
	return sanitized(r);
}

PlotRange PlotView::view() const {
	return m_userView ? *m_userView : fittedView();
}

void PlotView::setUserView(const PlotRange& view) {
	PlotRange const v = sanitized(view);
	if (m_userView && sameView(*m_userView, v)) {
		return;
	}
	m_userView = v;
	if (m_onViewChange) {
		m_onViewChange(v);
	}
}

void PlotView::resetView() {
	if (!m_userView) {
		return;
	}
	m_userView.reset();
	if (m_onViewChange) {
		m_onViewChange(view());
	}
}

// ---- ticks ---------------------------------------------------------------------------

std::vector<double> PlotView::niceTicks(double lo, double hi, int maxTicks) {
	return ticksForStep(lo, hi, tickStepFor(lo, hi, maxTicks));
}

std::string PlotView::formatTick(double value, double step) {
	char buf[48];
	double const magnitude = std::abs(value);
	if (magnitude >= 1e6 || (magnitude < 1e-4 && value != 0.0) || !(step > 0.0) || step < 1e-6) {
		std::snprintf(buf, sizeof(buf), "%g", value);
		return buf;
	}
	// Steps are 1, 2 or 5 x 10^k, so 10^k decides the decimals: 0.2 -> 1, 0.05 -> 2.
	// Zero gets the same decimals as its neighbours ("0.0" between "-0.5" and "0.5").
	int const decimals = std::max(0, static_cast<int>(-std::floor(std::log10(step) + 1e-9)));
	std::snprintf(buf, sizeof(buf), "%.*f", decimals, value == 0.0 ? 0.0 : value);
	return buf;
}

// ---- geometry ------------------------------------------------------------------------

PlotView::Layout PlotView::computeLayout(const GuiContext& ctx, const PlotRange& v) const {
	const Theme& th = ctx.theme();
	const Font& font = ctx.font();
	float const pad = th.framePadding * 2.0f;
	float const tickSize = th.fontSize * kTickSizeRatio;
	float const tickLineH = font.lineHeight(tickSize);

	Layout l;
	float top = m_bounds.y + pad;
	if (!m_label.empty()) {
		top += font.lineHeight(th.fontSize) + 2.0f;
	}
	if (!m_yLabel.empty()) {
		top += tickLineH + 2.0f;   // the Y axis label sits above the axis, read horizontally
	}
	float bottom = m_bounds.bottom() - pad - tickLineH - 4.0f;
	if (!m_xLabel.empty()) {
		bottom -= tickLineH + 2.0f;
	}
	float const plotH = std::max(1.0f, bottom - top);

	int const maxYTicks = std::clamp(static_cast<int>(plotH / kMinPixelsPerYTick), 2, 12);
	l.yStep = tickStepFor(v.yMin, v.yMax, maxYTicks);
	l.yTicks = niceTicks(v.yMin, v.yMax, maxYTicks);
	float widest = 0.0f;
	for (double t : l.yTicks) {
		widest = std::max(widest, font.measureText(formatTick(t, l.yStep), tickSize).x);
	}

	float const left = m_bounds.x + pad + widest + 6.0f;
	// Room on the right for the last X tick label, which is centred on its gridline.
	float const right = m_bounds.right() - pad - 10.0f;
	float const plotW = std::max(1.0f, right - left);
	l.plot = { left, top, plotW, plotH };

	// X labels sit side by side, so their width -- not a fixed pixel spacing -- decides
	// how many fit: start from the densest nice step and coarsen until they don't crowd.
	int const maxXTicks = std::clamp(static_cast<int>(plotW / kMinPixelsPerXTick), 2, 16);
	l.xStep = tickStepFor(v.xMin, v.xMax, maxXTicks);
	for (int guard = 0; guard < 16 && l.xStep > 0.0; ++guard) {
		l.xTicks = ticksForStep(v.xMin, v.xMax, l.xStep);
		float widestX = 0.0f;
		for (double t : l.xTicks) {
			widestX = std::max(widestX, font.measureText(formatTick(t, l.xStep), tickSize).x);
		}
		float const pxPerStep = static_cast<float>(l.xStep / (v.xMax - v.xMin)) * plotW;
		if (widestX + kXLabelGap <= pxPerStep) {
			break;
		}
		l.xStep = nextNiceStep(l.xStep);
	}
	l.xTicks = ticksForStep(v.xMin, v.xMax, l.xStep);

	// The legend lists every named series that has data -- hidden ones too (dimmed), so
	// they can be clicked back on -- but not an empty placeholder series.
	if (m_showLegend) {
		std::vector<int> listed;
		float nameW = 0.0f;
		for (int i = 0; i < seriesCount(); ++i) {
			const Series& s = m_series[static_cast<std::size_t>(i)];
			if (!s.name.empty() && !s.points.empty()) {
				listed.push_back(i);
				nameW = std::max(nameW, font.measureText(s.name, th.fontSize).x);
			}
		}
		if (!listed.empty()) {
			float const rowH = th.fontSize + 6.0f;
			float const swatchW = 20.0f;
			float const w = swatchW + nameW + 3.0f * th.framePadding + 4.0f;
			float const h = rowH * static_cast<float>(listed.size()) + 2.0f * th.framePadding;
			l.legend = { l.plot.right() - w - 6.0f, l.plot.y + 6.0f, w, h };
			for (std::size_t row = 0; row < listed.size(); ++row) {
				l.legendRows.push_back({ listed[row],
					Rect{ l.legend.x + th.framePadding * 0.5f,
					      l.legend.y + th.framePadding + rowH * static_cast<float>(row),
					      w - th.framePadding, rowH } });
			}
		}
	}
	return l;
}

Rect PlotView::plotRect(const GuiContext& ctx) const {
	return computeLayout(ctx, view()).plot;
}

Vec2 PlotView::dataToScreen(const GuiContext& ctx, Vec2 data) const {
	PlotRange const v = view();
	Rect const p = computeLayout(ctx, v).plot;
	return { static_cast<float>(p.x + (data.x - v.xMin) / (v.xMax - v.xMin) * p.w),
	         static_cast<float>(p.bottom() - (data.y - v.yMin) / (v.yMax - v.yMin) * p.h) };
}

Vec2 PlotView::screenToData(const GuiContext& ctx, Vec2 screen) const {
	PlotRange const v = view();
	Rect const p = computeLayout(ctx, v).plot;
	return { static_cast<float>(v.xMin + (screen.x - p.x) / p.w * (v.xMax - v.xMin)),
	         static_cast<float>(v.yMin + (p.bottom() - screen.y) / p.h * (v.yMax - v.yMin)) };
}

Rect PlotView::legendEntryRect(const GuiContext& ctx, int series) const {
	Layout const l = computeLayout(ctx, view());
	for (const LegendRow& row : l.legendRows) {
		if (row.series == series) {
			return row.rect;
		}
	}
	return {};
}

int PlotView::legendRowAt(const Layout& l, Vec2 p) const {
	for (const LegendRow& row : l.legendRows) {
		if (row.rect.contains(p)) {
			return row.series;
		}
	}
	return -1;
}

// ---- interaction ---------------------------------------------------------------------

PlotRange PlotView::anchored(const GuiContext& ctx, PlotRange v, double anchorX, double anchorY, Vec2 screen) const {
	// The Y tick labels' width sets the plot's left edge, and depends on the view being
	// laid out -- so a view computed from the OLD layout puts the anchor a few pixels off
	// once its own labels are measured. Shift by the error and re-measure; a couple of
	// rounds settle it, since a shift that small rarely changes the labels again.
	for (int i = 0; i < 4; ++i) {
		Layout const l = computeLayout(ctx, v);
		double const x = v.xMin + (screen.x - l.plot.x) / l.plot.w * (v.xMax - v.xMin);
		double const y = v.yMin + (l.plot.bottom() - screen.y) / l.plot.h * (v.yMax - v.yMin);
		double const dx = anchorX - x;
		double const dy = anchorY - y;
		if (dx == 0.0 && dy == 0.0) {
			break;
		}
		v.xMin += dx;
		v.xMax += dx;
		v.yMin += dy;
		v.yMax += dy;
	}
	return v;
}

void PlotView::update(GuiContext& ctx) {
	const InputState& in = ctx.input();
	m_time += in.deltaTime;
	if (!m_enabled || !m_interactive) {
		m_drag = Drag::None;
		return;
	}

	PlotRange const v = view();
	Layout const l = computeLayout(ctx, v);
	Vec2 const mouse = in.mousePos;
	bool const inPlot = l.plot.contains(mouse);
	bool const captured = ctx.activeId() == id();
	const int leftIdx = static_cast<int>(MouseButton::Left);

	auto dataAt = [&](Vec2 s) {
		return std::pair<double, double>{ v.xMin + (s.x - l.plot.x) / l.plot.w * (v.xMax - v.xMin),
		                                  v.yMin + (l.plot.bottom() - s.y) / l.plot.h * (v.yMax - v.yMin) };
	};

	// Wheel: zoom about the point under the cursor, so it stays put on screen.
	if (ctx.hoveredId() == id() && inPlot && in.wheelDelta != 0.0f && m_drag != Drag::Pan) {
		bool const shift = (in.modsDown & Mod::Shift) != 0;
		bool const ctrl = (in.modsDown & Mod::Ctrl) != 0;
		bool const zoomX = shift || !ctrl;
		bool const zoomY = ctrl || !shift;
		double const factor = std::pow(kWheelZoomBase, static_cast<double>(in.wheelDelta));
		auto const [ax, ay] = dataAt(mouse);
		PlotRange nv = v;
		if (zoomX) {
			nv.xMin = ax + (v.xMin - ax) * factor;
			nv.xMax = ax + (v.xMax - ax) * factor;
		}
		if (zoomY) {
			nv.yMin = ay + (v.yMin - ay) * factor;
			nv.yMax = ay + (v.yMax - ay) * factor;
		}
		setUserView(anchored(ctx, nv, ax, ay, mouse));
	}

	if (captured && in.mousePressed[leftIdx]) {
		m_drag = Drag::None;
		int const row = legendRowAt(l, mouse);
		if (row >= 0) {
			m_drag = Drag::Legend;
			m_pressLegendRow = row;
		} else if (inPlot) {
			float const dx = mouse.x - m_lastClickPos.x;
			float const dy = mouse.y - m_lastClickPos.y;
			bool const isDoubleClick = (m_time - m_lastClickTime) < kDoubleClickSeconds &&
				dx * dx + dy * dy < kDoubleClickSlop * kDoubleClickSlop;
			if (isDoubleClick) {
				m_lastClickTime = -1000.0f;
				resetView();
				return;
			}
			m_lastClickTime = m_time;
			m_lastClickPos = mouse;
			m_drag = Drag::Pan;
			m_dragStartMouse = mouse;
			m_dragStartView = v;
			auto const [gx, gy] = dataAt(mouse);
			m_dragAnchorX = gx;
			m_dragAnchorY = gy;
		}
	}

	if (captured && m_drag == Drag::Pan && in.mouseDown[leftIdx]) {
		float const dx = mouse.x - m_dragStartMouse.x;
		float const dy = mouse.y - m_dragStartMouse.y;
		// A press that hasn't moved yet changes nothing -- in particular it doesn't turn a
		// plain click into a "user view" that would stop auto-fit following the data.
		if (dx != 0.0f || dy != 0.0f) {
			// The data point grabbed at the press follows the cursor exactly.
			const PlotRange& s = m_dragStartView;
			double const xPerPx = (s.xMax - s.xMin) / l.plot.w;
			double const yPerPx = (s.yMax - s.yMin) / l.plot.h;
			PlotRange const guess{ s.xMin - dx * xPerPx, s.xMax - dx * xPerPx,
			                       s.yMin + dy * yPerPx, s.yMax + dy * yPerPx };
			setUserView(anchored(ctx, guess, m_dragAnchorX, m_dragAnchorY, mouse));
		}
	}

	if (captured && in.mouseReleased[leftIdx]) {
		// Legend clicks are release-inside, like Button: dragging off the row cancels.
		if (m_drag == Drag::Legend && legendRowAt(l, mouse) == m_pressLegendRow) {
			setSeriesVisible(m_pressLegendRow, !seriesVisible(m_pressLegendRow));
		}
		m_drag = Drag::None;
		m_pressLegendRow = -1;
	}
}

// ---- drawing -------------------------------------------------------------------------

void PlotView::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	const Font& font = ctx.font();
	PlotRange const v = view();
	Layout const l = computeLayout(ctx, v);
	float const pad = th.framePadding * 2.0f;
	float const tickSize = th.fontSize * kTickSizeRatio;
	float const tickLineH = font.lineHeight(tickSize);
	bool const isEnabled = effectivelyEnabled();
	Color const textColor = isEnabled ? th.text : th.textDisabled;

	dl.addRectFilled(m_bounds, th.frameBg, th.rounding);
	dl.addRect(m_bounds, th.border, 1.0f, th.rounding);
	if (l.plot.w < 4.0f || l.plot.h < 4.0f) {
		return;
	}

	auto toX = [&](double x) { return static_cast<float>(l.plot.x + (x - v.xMin) / (v.xMax - v.xMin) * l.plot.w); };
	auto toY = [&](double y) { return static_cast<float>(l.plot.bottom() - (y - v.yMin) / (v.yMax - v.yMin) * l.plot.h); };

	// ---- titles ----
	float captionY = m_bounds.y + pad;
	if (!m_label.empty()) {
		float const h = font.lineHeight(th.fontSize);
		dl.addTextClipped(font, th.fontSize, { m_bounds.x + pad, captionY, m_bounds.w - 2.0f * pad, h },
		                  textColor, m_label, Align::Center, Align::Center);
		captionY += h + 2.0f;
	}
	if (!m_yLabel.empty()) {
		dl.addTextClipped(font, tickSize, { m_bounds.x + pad, captionY, m_bounds.w - 2.0f * pad, tickLineH },
		                  th.textDisabled, m_yLabel, Align::Start, Align::Center);
	}

	// ---- plot area, grid and ticks ----
	Color const plotBg = Color::lerp(th.frameBg, th.scrollbarBg, 0.5f);
	Color const gridColor = Color::lerp(plotBg, th.border, 0.7f);
	dl.addRectFilled(l.plot, plotBg);

	for (double t : l.xTicks) {
		float const x = toX(t);
		if (m_showGrid) {
			dl.addLine({ x, l.plot.y }, { x, l.plot.bottom() }, gridColor);
		}
		std::string const label = formatTick(t, l.xStep);
		float const w = font.measureText(label, tickSize).x;
		float const lx = std::clamp(x - w * 0.5f, m_bounds.x + 2.0f, m_bounds.right() - w - 2.0f);
		dl.addText(font, tickSize, { lx, l.plot.bottom() + 3.0f }, th.textDisabled, label, TextFlags::Tabular);
	}
	for (double t : l.yTicks) {
		float const y = toY(t);
		if (m_showGrid) {
			dl.addLine({ l.plot.x, y }, { l.plot.right(), y }, gridColor);
		}
		std::string const label = formatTick(t, l.yStep);
		float const w = font.measureText(label, tickSize).x;
		dl.addText(font, tickSize, { l.plot.x - w - 6.0f, y - tickLineH * 0.5f }, th.textDisabled, label,
		           TextFlags::Tabular);
	}
	// x = 0 / y = 0, a little brighter than the grid, when they are in view.
	if (v.xMin < 0.0 && v.xMax > 0.0) {
		float const x = toX(0.0);
		dl.addLine({ x, l.plot.y }, { x, l.plot.bottom() }, th.textDisabled);
	}
	if (v.yMin < 0.0 && v.yMax > 0.0) {
		float const y = toY(0.0);
		dl.addLine({ l.plot.x, y }, { l.plot.right(), y }, th.textDisabled);
	}
	dl.addRect(l.plot, th.border);

	if (!m_xLabel.empty()) {
		dl.addTextClipped(font, tickSize, { l.plot.x, l.plot.bottom() + 3.0f + tickLineH + 2.0f, l.plot.w, tickLineH },
		                  th.textDisabled, m_xLabel, Align::Center, Align::Center);
	}

	// ---- data, clipped to the plot area ----
	dl.pushClipRect(l.plot, true);
	std::vector<Vec2> run;
	for (int i = 0; i < seriesCount(); ++i) {
		const Series& s = m_series[static_cast<std::size_t>(i)];
		if (!s.visible || s.points.empty()) {
			continue;
		}
		Color const color = isEnabled ? seriesColor(ctx, i) : th.textDisabled;
		if (s.style != PlotStyle::Points) {
			// One polyline per unbroken, visible run. A non-finite sample (1/x at 0)
			// splits the line instead of joining two unrelated points across the plot,
			// and each segment is clipped to the plot area in doubles first: zoomed far
			// in, an off-screen neighbour can be millions of pixels away, and squeezing
			// that through a float would tilt the part that IS visible.
			double const margin = static_cast<double>(s.lineWidth) + 2.0;
			double const cl = l.plot.x - margin, ct = l.plot.y - margin;
			double const cr = l.plot.right() + margin, cb = l.plot.bottom() + margin;
			run.clear();
			auto flush = [&] {
				if (run.size() >= 2) {
					dl.addPolyline(run.data(), static_cast<int>(run.size()), color, s.lineWidth, false);
				}
				run.clear();
			};
			bool havePrev = false;
			double px = 0.0, py = 0.0;
			for (Vec2 p : s.points) {
				if (!finite(p)) {
					flush();
					havePrev = false;
					continue;
				}
				double const cx = l.plot.x + (p.x - v.xMin) / (v.xMax - v.xMin) * l.plot.w;
				double const cy = l.plot.bottom() - (p.y - v.yMin) / (v.yMax - v.yMin) * l.plot.h;
				if (havePrev) {
					double ax = px, ay = py, bx = cx, by = cy;
					if (clipSegment(ax, ay, bx, by, cl, ct, cr, cb)) {
						Vec2 const a{ static_cast<float>(ax), static_cast<float>(ay) };
						Vec2 const b{ static_cast<float>(bx), static_cast<float>(by) };
						// A segment that doesn't continue the current run (it was clipped
						// where it enters) starts a new one.
						if (!run.empty() && (run.back().x != a.x || run.back().y != a.y)) {
							flush();
						}
						if (run.empty()) {
							run.push_back(a);
						}
						run.push_back(b);
						if (bx != cx || by != cy) {
							flush();   // clipped where it leaves: the run ends here
						}
					} else {
						flush();
					}
				}
				px = cx;
				py = cy;
				havePrev = true;
			}
			flush();
		}
		if (s.style != PlotStyle::Line) {
			float const r = s.pointRadius;
			for (Vec2 p : s.points) {
				if (!finite(p)) {
					continue;
				}
				Vec2 const sp{ toX(p.x), toY(p.y) };
				if (sp.x < l.plot.x - r || sp.x > l.plot.right() + r || sp.y < l.plot.y - r || sp.y > l.plot.bottom() + r) {
					continue;
				}
				dl.addCircleFilled(sp, r, color);
			}
		}
	}
	dl.popClipRect();

	// ---- hover readout ----
	Vec2 const mouse = ctx.input().mousePos;
	bool const hovering = ctx.hoveredId() == id() && l.plot.contains(mouse) && m_drag != Drag::Pan &&
		legendRowAt(l, mouse) < 0;
	if (hovering) {
		int bestSeries = -1;
		Vec2 bestData{}, bestScreen{};
		float bestDist2 = kHoverSnapRadius * kHoverSnapRadius;
		for (int i = 0; i < seriesCount(); ++i) {
			const Series& s = m_series[static_cast<std::size_t>(i)];
			if (!s.visible) {
				continue;
			}
			for (Vec2 p : s.points) {
				if (!finite(p)) {
					continue;
				}
				Vec2 const sp{ toX(p.x), toY(p.y) };
				float const dx = sp.x - mouse.x;
				float const dy = sp.y - mouse.y;
				float const d2 = dx * dx + dy * dy;
				if (d2 <= bestDist2) {
					bestDist2 = d2;
					bestSeries = i;
					bestData = p;
					bestScreen = sp;
				}
			}
		}

		std::string text;
		Vec2 anchor = mouse;
		if (bestSeries >= 0) {
			Color const c = seriesColor(ctx, bestSeries);
			dl.addCircleFilled(bestScreen, 5.5f, th.frameBg);
			dl.addCircleFilled(bestScreen, 4.0f, c);
			const std::string& name = m_series[static_cast<std::size_t>(bestSeries)].name;
			text = (name.empty() ? std::string() : name + ": ") + "(" +
				formatTick(bestData.x, l.xStep / 100.0) + ", " + formatTick(bestData.y, l.yStep / 100.0) + ")";
			anchor = bestScreen;
		} else {
			auto const x = v.xMin + (mouse.x - l.plot.x) / l.plot.w * (v.xMax - v.xMin);
			auto const y = v.yMin + (l.plot.bottom() - mouse.y) / l.plot.h * (v.yMax - v.yMin);
			text = "(" + formatTick(x, l.xStep / 100.0) + ", " + formatTick(y, l.yStep / 100.0) + ")";
			Color const cross = th.textDisabled.withAlpha(0.5f);
			dl.addLine({ mouse.x, l.plot.y }, { mouse.x, l.plot.bottom() }, cross);
			dl.addLine({ l.plot.x, mouse.y }, { l.plot.right(), mouse.y }, cross);
		}
		// A small label box beside the point, kept inside the plot area.
		Vec2 const ts = font.measureText(text, tickSize, TextFlags::Tabular);
		Rect box{ anchor.x + 10.0f, anchor.y - ts.y - 10.0f, ts.x + 8.0f, ts.y + 4.0f };
		if (box.right() > l.plot.right()) {
			box.x = anchor.x - 10.0f - box.w;
		}
		if (box.y < l.plot.y) {
			box.y = anchor.y + 10.0f;
		}
		dl.addRectFilled(box, th.windowBg, th.rounding);
		dl.addRect(box, th.border, 1.0f, th.rounding);
		dl.addText(font, tickSize, { box.x + 4.0f, box.y + 2.0f }, th.text, text, TextFlags::Tabular);
	}

	// ---- legend ----
	if (!l.legendRows.empty()) {
		dl.addRectFilled(l.legend, th.windowBg, th.rounding);
		dl.addRect(l.legend, th.border, 1.0f, th.rounding);
		int const hoveredSeries = (ctx.hoveredId() == id() && m_interactive) ? legendRowAt(l, mouse) : -1;
		for (const LegendRow& legendRow : l.legendRows) {
			int const i = legendRow.series;
			const Series& s = m_series[static_cast<std::size_t>(i)];
			Rect const row = legendRow.rect;
			if (i == hoveredSeries) {
				dl.addRectFilled(row, th.frameBgHovered, th.rounding);
			}
			Color c = isEnabled ? seriesColor(ctx, i) : th.textDisabled;
			if (!s.visible) {
				c = c.withAlpha(0.3f);
			}
			float const cy = row.y + row.h * 0.5f;
			float const sx = row.x + th.framePadding;
			if (s.style != PlotStyle::Points) {
				dl.addLine({ sx, cy }, { sx + 16.0f, cy }, c, 2.0f);
			}
			if (s.style != PlotStyle::Line) {
				dl.addCircleFilled({ sx + 8.0f, cy }, 3.0f, c);
			}
			Rect const nameRect{ sx + 22.0f, row.y, row.right() - sx - 22.0f, row.h };
			dl.addTextClipped(font, th.fontSize, nameRect, s.visible ? textColor : th.textDisabled, s.name,
			                  Align::Start, Align::Center);
		}
	}
}

} // namespace lightGraphics::ui
