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

// docs/gui/05-widgets.md, "PlotView" -- headless, same pattern as test_listbox.cpp: no
// Vulkan device, no window. Tick maths, auto-fit, the data<->screen mapping, and every
// interaction (wheel zoom with and without modifiers, drag pan, double-click reset,
// legend toggles) driven through injected input.

#include <lightVulkanGraphics/ui/Ui.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace lvgui = lightGraphics::ui;

namespace {

	lvgui::GuiCreateInfo testCreateInfo() {
		lvgui::GuiCreateInfo info;
		info.fontPath = LVG_UI_TEST_FONT_PATH;
		return info;
	}

	void step(lvgui::GuiContext& ctx, float dt = 0.016f) {
		ctx.beginFrame({ 800.0f, 600.0f }, 1.0f, dt);
		ctx.update();
		ctx.endFrame();
	}

	void clickAt(lvgui::GuiContext& ctx, lvgui::Vec2 pos) {
		ctx.injectMousePos(pos);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
	}

	void wheelAt(lvgui::GuiContext& ctx, lvgui::Vec2 pos, float delta, int mods = 0) {
		// Modifier state rides on key events (it is the full bitmask, GLFW-style).
		ctx.injectKey(lvgui::Key::Unknown, mods, true, false);
		ctx.injectMousePos(pos);
		ctx.injectScroll(delta);
		step(ctx);
		ctx.injectKey(lvgui::Key::Unknown, 0, false, false);
		step(ctx);
	}

	bool near(double a, double b, double tol = 1e-6) {
		return std::abs(a - b) <= tol * std::max(1.0, std::max(std::abs(a), std::abs(b)));
	}

	lvgui::Vec2 centre(const lvgui::Rect& r) {
		return { r.x + r.w * 0.5f, r.y + r.h * 0.5f };
	}

	struct Fixture {
		lvgui::GuiContext ctx{ testCreateInfo(), lvgui::PlatformHooks{} };
		lvgui::Panel* panel = nullptr;
		lvgui::PlotView* plot = nullptr;
		int viewChanges = 0;

		Fixture() {
			panel = ctx.createPanel("Plot", { 20.0f, 20.0f, 600.0f, 330.0f }, lvgui::PanelFlags::None);
			plot = panel->add<lvgui::PlotView>();
			plot->setOnViewChange([this](const lvgui::PlotRange&) { ++viewChanges; });
			step(ctx);
		}
	};

	void testNiceTicks() {
		using P = lvgui::PlotView;
		std::vector<double> t = P::niceTicks(0.0, 1.0, 6);
		std::vector<double> const tenths = { 0.0, 0.2, 0.4, 0.6, 0.8, 1.0 };
		assert(t.size() == tenths.size());
		for (std::size_t i = 0; i < t.size(); ++i) {
			assert(near(t[i], tenths[i]));
		}
		assert((P::niceTicks(-10.0, 10.0, 5) == std::vector<double>{ -10.0, -5.0, 0.0, 5.0, 10.0 }));

		// Never more than asked for, always multiples of 1/2/5 x 10^k, all inside the range.
		for (int maxTicks : { 2, 3, 5, 8 }) {
			for (auto [lo, hi] : { std::pair{ 0.31, 0.39 }, { -3.7, 1234.5 }, { 1e-9, 3e-9 }, { -1e7, 1e7 } }) {
				std::vector<double> ts = P::niceTicks(lo, hi, maxTicks);
				assert(static_cast<int>(ts.size()) <= maxTicks);
				assert(ts.size() >= 1);
				for (double v : ts) {
					assert(v >= lo - 1e-12 * std::abs(lo) && v <= hi + 1e-12 * std::abs(hi));
				}
			}
		}
		// A narrow range goes to a finer step rather than stopping at one label, when a
		// finer 1/2/5 step fits: 0.31..0.39 has only 0.35 at step 0.05, but four at 0.02.
		assert(P::niceTicks(0.31, 0.39, 4).size() >= 2);
		assert(P::niceTicks(0.31, 0.39, 3).size() == 1);   // 0.02 would give four: too many
		assert(P::niceTicks(5.0, 5.0, 4).empty());   // no range, no ticks

		std::cout << "✓ testNiceTicks\n";
	}

	void testFormatTick() {
		using P = lvgui::PlotView;
		assert(P::formatTick(0.4, 0.2) == "0.4");
		assert(P::formatTick(0.0, 0.5) == "0.0");     // zero matches its neighbours
		assert(P::formatTick(0.0, 5.0) == "0");
		assert(P::formatTick(1500.0, 500.0) == "1500");
		assert(P::formatTick(-2.0, 1.0) == "-2");
		assert(P::formatTick(0.05, 0.05) == "0.05");
		assert(P::formatTick(1e-6, 1e-7) == "1e-06");
		assert(P::formatTick(2e6, 1e6) == "2e+06");

		std::cout << "✓ testFormatTick\n";
	}

	void testEmptyPlotHasAUnitViewAndDraws() {
		Fixture f;
		lvgui::PlotRange v = f.plot->view();
		assert(v.xMin == 0.0 && v.xMax == 1.0 && v.yMin == 0.0 && v.yMax == 1.0);
		f.plot->addSeries("nothing yet");
		step(f.ctx);
		assert(f.plot->legendEntryRect(f.ctx, 0).empty());   // no data: not in the legend

		std::cout << "✓ testEmptyPlotHasAUnitViewAndDraws\n";
	}

	void testAutoFitCoversTheDataWithAMargin() {
		Fixture f;
		int s = f.plot->addSeries("line");
		f.plot->setSeriesData(s, { { 0.0f, 0.0f }, { 10.0f, 5.0f } });
		lvgui::PlotRange v = f.plot->view();
		assert(v.xMin < 0.0 && v.xMin > -1.0);
		assert(v.xMax > 10.0 && v.xMax < 11.0);
		assert(v.yMin < 0.0 && v.yMin > -1.0);
		assert(v.yMax > 5.0 && v.yMax < 6.0);

		// A single point (or a flat line) still gets a usable span.
		f.plot->setSeriesData(s, { { 3.0f, 3.0f } });
		v = f.plot->view();
		assert(v.xMin < 3.0 && v.xMax > 3.0 && v.yMin < 3.0 && v.yMax > 3.0);

		std::cout << "✓ testAutoFitCoversTheDataWithAMargin\n";
	}

	void testNonFinitePointsAreIgnoredByAutoFit() {
		Fixture f;
		int s = f.plot->addSeries("gappy");
		float const nan = std::numeric_limits<float>::quiet_NaN();
		float const inf = std::numeric_limits<float>::infinity();
		f.plot->setSeriesData(s, { { 0.0f, 1.0f }, { 1.0f, nan }, { 2.0f, inf }, { 3.0f, 2.0f } });
		lvgui::PlotRange v = f.plot->view();
		assert(std::isfinite(v.yMin) && std::isfinite(v.yMax));
		assert(v.yMax < 3.0);
		step(f.ctx);   // and drawing a broken line is fine

		std::cout << "✓ testNonFinitePointsAreIgnoredByAutoFit\n";
	}

	void testFixedXRangeFitsYToJustTheVisiblePoints() {
		Fixture f;
		int s = f.plot->addSeries("y = x");
		std::vector<lvgui::Vec2> pts;
		for (int i = 0; i <= 100; ++i) {
			pts.push_back({ static_cast<float>(i), static_cast<float>(i) });
		}
		f.plot->setSeriesData(s, pts);
		f.plot->setXRange(0.0, 10.0);
		lvgui::PlotRange v = f.plot->view();
		assert(v.xMin == 0.0 && v.xMax == 10.0);
		assert(v.yMax < 12.0);   // fitted to y <= 10, not to the off-screen y = 100
		assert(!f.plot->autoFitX() && f.plot->autoFitY());

		f.plot->setYRange(-1.0, 1.0);
		v = f.plot->view();
		assert(v.yMin == -1.0 && v.yMax == 1.0);
		assert(f.viewChanges == 0);   // the application's own changes are not reported

		std::cout << "✓ testFixedXRangeFitsYToJustTheVisiblePoints\n";
	}

	void testDataAndScreenCoordinatesRoundTrip() {
		Fixture f;
		f.plot->setXRange(-5.0, 5.0);
		f.plot->setYRange(0.0, 100.0);
		step(f.ctx);
		lvgui::Rect const p = f.plot->plotRect(f.ctx);
		lvgui::Vec2 const bl = f.plot->dataToScreen(f.ctx, { -5.0f, 0.0f });
		assert(near(bl.x, p.x, 1e-4) && near(bl.y, p.bottom(), 1e-4));
		lvgui::Vec2 const tr = f.plot->dataToScreen(f.ctx, { 5.0f, 100.0f });
		assert(near(tr.x, p.right(), 1e-4) && near(tr.y, p.y, 1e-4));

		lvgui::Vec2 const d = f.plot->screenToData(f.ctx, f.plot->dataToScreen(f.ctx, { 1.25f, 37.5f }));
		assert(near(d.x, 1.25, 1e-4) && near(d.y, 37.5, 1e-4));

		std::cout << "✓ testDataAndScreenCoordinatesRoundTrip\n";
	}

	void testWheelZoomsAboutTheCursor() {
		Fixture f;
		f.plot->setXRange(0.0, 100.0);
		f.plot->setYRange(0.0, 100.0);
		step(f.ctx);
		lvgui::Vec2 const cursor = f.plot->dataToScreen(f.ctx, { 25.0f, 75.0f });

		wheelAt(f.ctx, cursor, 1.0f);   // away from the user: zoom in
		lvgui::PlotRange v = f.plot->view();
		assert(f.plot->hasUserView());
		assert(f.viewChanges == 1);
		assert(v.xMax - v.xMin < 100.0 && v.yMax - v.yMin < 100.0);
		// The point under the cursor stays under the cursor.
		lvgui::Vec2 const under = f.plot->screenToData(f.ctx, cursor);
		assert(near(under.x, 25.0, 1e-3) && near(under.y, 75.0, 1e-3));

		wheelAt(f.ctx, cursor, -2.0f);   // towards the user: zoom out past where we started
		v = f.plot->view();
		assert(v.xMax - v.xMin > 100.0);

		std::cout << "✓ testWheelZoomsAboutTheCursor\n";
	}

	void testShiftZoomsOnlyXAndCtrlOnlyY() {
		Fixture f;
		f.plot->setXRange(0.0, 100.0);
		f.plot->setYRange(0.0, 100.0);
		step(f.ctx);
		lvgui::Vec2 const mid = centre(f.plot->plotRect(f.ctx));

		wheelAt(f.ctx, mid, 1.0f, lvgui::Mod::Shift);
		lvgui::PlotRange v = f.plot->view();
		assert(v.xMax - v.xMin < 99.0);
		assert(near(v.yMax - v.yMin, 100.0));

		f.plot->resetView();
		wheelAt(f.ctx, mid, 1.0f, lvgui::Mod::Ctrl);
		v = f.plot->view();
		assert(near(v.xMax - v.xMin, 100.0));
		assert(v.yMax - v.yMin < 99.0);

		std::cout << "✓ testShiftZoomsOnlyXAndCtrlOnlyY\n";
	}

	void testDragPansAndAPlainClickDoesNot() {
		Fixture f;
		int s = f.plot->addSeries("data");
		f.plot->setSeriesData(s, { { 0.0f, 0.0f }, { 10.0f, 10.0f } });
		step(f.ctx);

		// A click that doesn't move leaves auto-fit in charge...
		clickAt(f.ctx, centre(f.plot->plotRect(f.ctx)));
		assert(!f.plot->hasUserView());
		f.plot->appendPoint(s, { 20.0f, 20.0f });
		assert(f.plot->view().xMax > 20.0);   // ...so the view still follows new data

		// A drag pans: moving the mouse right moves the view's window left.
		step(f.ctx, 1.0f);   // well clear of the double-click window
		lvgui::Rect const p = f.plot->plotRect(f.ctx);
		lvgui::PlotRange const before = f.plot->view();
		lvgui::Vec2 const start = centre(p);
		f.ctx.injectMousePos(start);
		f.ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(f.ctx);
		f.ctx.injectMousePos({ start.x + p.w * 0.25f, start.y });
		step(f.ctx);
		f.ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(f.ctx);
		lvgui::PlotRange const after = f.plot->view();
		double const span = before.xMax - before.xMin;
		assert(f.plot->hasUserView());
		assert(near(after.xMin, before.xMin - 0.25 * span, 1e-3));
		assert(near(after.yMin, before.yMin, 1e-6));

		std::cout << "✓ testDragPansAndAPlainClickDoesNot\n";
	}

	void testDoubleClickResetsTheView() {
		Fixture f;
		int s = f.plot->addSeries("data");
		f.plot->setSeriesData(s, { { 0.0f, 0.0f }, { 10.0f, 10.0f } });
		step(f.ctx);
		lvgui::PlotRange const fitted = f.plot->view();
		lvgui::Vec2 const mid = centre(f.plot->plotRect(f.ctx));

		wheelAt(f.ctx, mid, 3.0f);
		assert(f.plot->hasUserView());
		step(f.ctx, 1.0f);

		clickAt(f.ctx, mid);
		clickAt(f.ctx, mid);
		assert(!f.plot->hasUserView());
		lvgui::PlotRange const v = f.plot->view();
		assert(near(v.xMin, fitted.xMin) && near(v.xMax, fitted.xMax));
		assert(f.viewChanges == 2);   // the zoom, then the reset

		std::cout << "✓ testDoubleClickResetsTheView\n";
	}

	void testLegendClicksToggleSeriesAndAutoFitSkipsHiddenOnes() {
		Fixture f;
		int small = f.plot->addSeries("small");
		int big = f.plot->addSeries("big");
		f.plot->setSeriesData(small, { { 0.0f, 0.0f }, { 1.0f, 1.0f } });
		f.plot->setSeriesData(big, { { 0.0f, 0.0f }, { 1.0f, 100.0f } });
		step(f.ctx);
		assert(f.plot->view().yMax > 100.0);

		lvgui::Rect const bigEntry = f.plot->legendEntryRect(f.ctx, big);
		assert(!bigEntry.empty());
		clickAt(f.ctx, centre(bigEntry));
		assert(!f.plot->seriesVisible(big));
		assert(f.plot->view().yMax < 2.0);    // auto-fit ignores the hidden series
		assert(!f.plot->hasUserView());       // a legend click isn't a zoom/pan
		assert(!f.plot->legendEntryRect(f.ctx, big).empty());   // still listed, to turn back on

		step(f.ctx, 1.0f);
		clickAt(f.ctx, centre(f.plot->legendEntryRect(f.ctx, big)));
		assert(f.plot->seriesVisible(big));

		std::cout << "✓ testLegendClicksToggleSeriesAndAutoFitSkipsHiddenOnes\n";
	}

	void testRollingWindowKeepsTheNewestPoints() {
		Fixture f;
		int s = f.plot->addSeries("stream");
		f.plot->setSeriesMaxPoints(s, 3);
		for (int i = 0; i < 5; ++i) {
			f.plot->appendPoint(s, { static_cast<float>(i), static_cast<float>(i * i) });
			(void)f.plot->view();   // keep the cached bounds warm between appends
		}
		const std::vector<lvgui::Vec2>& d = f.plot->seriesData(s);
		assert(d.size() == 3 && d.front().x == 2.0f && d.back().x == 4.0f);
		// The dropped points no longer count towards auto-fit.
		lvgui::PlotRange const v = f.plot->view();
		assert(v.xMin > 1.0 && v.yMin > 1.0);

		std::cout << "✓ testRollingWindowKeepsTheNewestPoints\n";
	}

	void testApplicationRangesDiscardTheUserView() {
		Fixture f;
		f.plot->setXRange(0.0, 10.0);
		step(f.ctx);
		wheelAt(f.ctx, centre(f.plot->plotRect(f.ctx)), 2.0f);
		assert(f.plot->hasUserView());
		int const changes = f.viewChanges;

		f.plot->setXRange(0.0, 50.0);
		assert(!f.plot->hasUserView());
		assert(f.plot->view().xMax == 50.0);
		assert(f.viewChanges == changes);

		std::cout << "✓ testApplicationRangesDiscardTheUserView\n";
	}

	void testNonInteractivePlotIgnoresTheMouse() {
		Fixture f;
		f.plot->setXRange(0.0, 10.0);
		f.plot->setInteractive(false);
		step(f.ctx);
		lvgui::Vec2 const mid = centre(f.plot->plotRect(f.ctx));
		wheelAt(f.ctx, mid, 2.0f);
		clickAt(f.ctx, mid);
		clickAt(f.ctx, mid);
		assert(!f.plot->hasUserView());
		assert(f.viewChanges == 0);
		assert(!f.plot->wantsWheel());

		std::cout << "✓ testNonInteractivePlotIgnoresTheMouse\n";
	}

	// The wheel zooms the plot; it must not also scroll the panel the plot sits in.
	void testWheelOverThePlotDoesNotScrollItsPanel() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Tall", { 20.0f, 20.0f, 500.0f, 200.0f }, lvgui::PanelFlags::Default);
		auto* plot = panel->add<lvgui::PlotView>();
		plot->setHeight(150.0f);
		for (int i = 0; i < 12; ++i) {
			panel->add<lvgui::Label>("filler " + std::to_string(i));
		}
		step(ctx);
		step(ctx);
		assert(panel->needsScrollbar());

		wheelAt(ctx, centre(plot->plotRect(ctx)), -1.0f);
		assert(panel->scrollY() == 0.0f);
		assert(plot->hasUserView());

		std::cout << "✓ testWheelOverThePlotDoesNotScrollItsPanel\n";
	}


	// Zoomed far into a long line, its off-screen neighbours map to millions of pixels
	// away. Every segment is clipped to the plot first, so nothing near those
	// coordinates may reach the draw list -- and the visible stretch is still drawn.
	void testDeepZoomClipsLinesToThePlot() {
		Fixture f;
		int s = f.plot->addSeries("y = x");
		std::vector<lvgui::Vec2> pts;
		for (int i = 0; i <= 1000; ++i) {
			pts.push_back({ static_cast<float>(i), static_cast<float>(i) });
		}
		f.plot->setSeriesData(s, pts);
		f.plot->setXRange(500.0, 500.01);
		f.plot->setYRange(500.0, 500.01);
		step(f.ctx);

		lvgui::Rect const p = f.plot->plotRect(f.ctx);
		float maxAbs = 0.0f;
		bool anyInside = false;
		for (const lvgui::UiVertex& vtx : f.ctx.drawList().vertices()) {
			maxAbs = std::max({ maxAbs, std::abs(vtx.pos.x), std::abs(vtx.pos.y) });
			anyInside = anyInside || (vtx.pos.x > p.x + 2.0f && vtx.pos.x < p.right() - 2.0f &&
			                          vtx.pos.y > p.y + 2.0f && vtx.pos.y < p.bottom() - 2.0f);
		}
		assert(maxAbs < 5000.0f);
		assert(anyInside);

		std::cout << "✓ testDeepZoomClipsLinesToThePlot\n";
	}

}

int main() {
	testNiceTicks();
	testFormatTick();
	testEmptyPlotHasAUnitViewAndDraws();
	testAutoFitCoversTheDataWithAMargin();
	testNonFinitePointsAreIgnoredByAutoFit();
	testFixedXRangeFitsYToJustTheVisiblePoints();
	testDataAndScreenCoordinatesRoundTrip();
	testWheelZoomsAboutTheCursor();
	testShiftZoomsOnlyXAndCtrlOnlyY();
	testDragPansAndAPlainClickDoesNot();
	testDoubleClickResetsTheView();
	testLegendClicksToggleSeriesAndAutoFitSkipsHiddenOnes();
	testRollingWindowKeepsTheNewestPoints();
	testApplicationRangesDiscardTheUserView();
	testNonInteractivePlotIgnoresTheMouse();
	testWheelOverThePlotDoesNotScrollItsPanel();
	testDeepZoomClipsLinesToThePlot();

	std::cout << "\n✅ All PlotView tests passed!\n";
	return 0;
}
