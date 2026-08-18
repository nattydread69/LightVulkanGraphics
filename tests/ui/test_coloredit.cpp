// docs/gui/05-widgets.md, "ColorEdit" -- headless, same pattern as test_dropdown.cpp: no
// Vulkan device, no window. Popup geometry (SV square / hue strip / alpha strip rects) is
// recomputed here from the same theme metrics ColorEdit itself uses
// (colorSquareSize/colorStripWidth/framePadding/itemSpacing), the same way test_dropdown.cpp
// derives its popup rect from theme.rowHeight rather than exposing DropDown's private
// computePopupRect() for tests to call directly.

#include <lightVulkanGraphics/ui/Ui.h>

#include <cassert>
#include <cmath>
#include <iostream>

namespace lvgui = lightGraphics::ui;

namespace {

	lvgui::GuiCreateInfo testCreateInfo() {
		lvgui::GuiCreateInfo info;
		info.fontPath = LVG_UI_TEST_FONT_PATH;
		return info;
	}

	void step(lvgui::GuiContext& ctx) {
		ctx.beginFrame({ 800.0f, 600.0f }, 1.0f, 0.016f);
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

	// Opens `ce`'s popup via a control click. Empty label -> splitRow()'s control rect
	// is the widget's full bounds() (Widget.h: "an empty label gives the control the
	// full row width"), so bounds() alone is enough to click it without reimplementing
	// the label/control split here.
	void openPopup(lvgui::GuiContext& ctx, lvgui::ColorEdit* ce) {
		lvgui::Rect b = ce->bounds();
		clickAt(ctx, { b.x + b.w * 0.5f, b.y + b.h * 0.5f });
	}

	// Mirrors ColorEdit::svSquareRect/hueStripRect/alphaStripRect/computePopupRect
	// (ColorEdit.cpp) from public theme metrics + the control's bounds(), same
	// derivation those private methods use internally.
	struct PopupGeometry {
		lvgui::Rect popup, square, hue, alpha;
	};

	PopupGeometry computeGeometry(const lvgui::GuiContext& ctx, lvgui::ColorEdit* ce, bool hasAlpha) {
		const lvgui::Theme& th = ctx.theme();
		lvgui::Rect control = ce->bounds();

		float width = 2.0f * th.framePadding + th.colorSquareSize + th.itemSpacing + th.colorStripWidth;
		float height = 2.0f * th.framePadding + th.colorSquareSize +
		               (hasAlpha ? th.itemSpacing + th.colorStripWidth : 0.0f);
		lvgui::Rect popup{ control.x, control.bottom(), width, height };

		lvgui::Rect square{ popup.x + th.framePadding, popup.y + th.framePadding,
		                     th.colorSquareSize, th.colorSquareSize };
		lvgui::Rect hue{ square.right() + th.itemSpacing, square.y, th.colorStripWidth, th.colorSquareSize };
		lvgui::Rect alpha{ square.x, square.bottom() + th.itemSpacing,
		                    th.colorSquareSize + th.itemSpacing + th.colorStripWidth, th.colorStripWidth };

		return { popup, square, hue, hasAlpha ? alpha : lvgui::Rect{} };
	}

	bool approxEq(std::uint8_t a, std::uint8_t b, int tolerance = 3) {
		return std::abs(static_cast<int>(a) - static_cast<int>(b)) <= tolerance;
	}

	// ---- tests --------------------------------------------------------------------

	void testPopupOpensOnControlClickAndClosesOnSecondClick() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* ce = panel->add<lvgui::ColorEdit4>("", lvgui::Color{ 255, 0, 0, 255 });
		step(ctx);
		assert(!ctx.isPopupOpen());

		openPopup(ctx, ce);
		assert(ctx.isPopupOpen());
		assert(ctx.popupOwner() == ce);

		lvgui::Rect b = ce->bounds();
		clickAt(ctx, { b.x + b.w * 0.5f, b.y + b.h * 0.5f });
		assert(!ctx.isPopupOpen());

		std::cout << "✓ testPopupOpensOnControlClickAndClosesOnSecondClick\n";
	}

	void testClickInSVSquareCornersSetSaturationAndValueExtremes() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		// Hue 0 (red) throughout -- isolates the SV square's own effect on the result.
		auto* ce = panel->add<lvgui::ColorEdit3>("", lvgui::Color{ 0, 0, 0, 255 });
		step(ctx);

		openPopup(ctx, ce);
		PopupGeometry g = computeGeometry(ctx, ce, false);

		// Exact boundary coordinates, not "a pixel inside the edge": Rect::contains()
		// is inclusive at both ends, and dragSVSquare()'s clamp() means a point exactly
		// ON the boundary maps to a mathematically exact 0.0f/1.0f, not an approximation
		// a few percent off it -- clicking even 1px inside a 140px square is enough
		// saturation/value error to miss a tight byte tolerance at the white corner.

		// Top-right: saturation 1, value 1 -> full hue-0 red, since hue starts at 0 for
		// a black initial (toHSV's canonical grey/black hue).
		clickAt(ctx, { g.square.right(), g.square.top() });
		lvgui::Color topRight = ce->value();
		assert(approxEq(topRight.r, 255) && approxEq(topRight.g, 0) && approxEq(topRight.b, 0));

		// Bottom edge (value 0) is black regardless of saturation.
		clickAt(ctx, { g.square.right(), g.square.bottom() });
		lvgui::Color bottomRight = ce->value();
		assert(approxEq(bottomRight.r, 0) && approxEq(bottomRight.g, 0) && approxEq(bottomRight.b, 0));

		// Top-left: saturation 0, value 1 -> white.
		clickAt(ctx, { g.square.left(), g.square.top() });
		lvgui::Color topLeft = ce->value();
		assert(approxEq(topLeft.r, 255) && approxEq(topLeft.g, 255) && approxEq(topLeft.b, 255));

		std::cout << "✓ testClickInSVSquareCornersSetSaturationAndValueExtremes\n";
	}

	void testClickInHueStripSetsHue() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* ce = panel->add<lvgui::ColorEdit3>("", lvgui::Color{ 255, 0, 0, 255 });
		step(ctx);

		openPopup(ctx, ce);
		PopupGeometry g = computeGeometry(ctx, ce, false);

		// Pin saturation/value to 1 first, so the hue strip's effect on the result is
		// isolated (matches how a user would actually use the picker).
		clickAt(ctx, { g.square.right(), g.square.top() });

		// One third of the way down the strip -> hue 120 (green).
		float y = g.hue.y + g.hue.h * (120.0f / 360.0f);
		clickAt(ctx, { g.hue.centre().x, y });
		lvgui::Color green = ce->value();
		assert(approxEq(green.r, 0) && approxEq(green.g, 255) && approxEq(green.b, 0));

		// Two thirds -> hue 240 (blue).
		y = g.hue.y + g.hue.h * (240.0f / 360.0f);
		clickAt(ctx, { g.hue.centre().x, y });
		lvgui::Color blue = ce->value();
		assert(approxEq(blue.r, 0) && approxEq(blue.g, 0) && approxEq(blue.b, 255));

		std::cout << "✓ testClickInHueStripSetsHue\n";
	}

	void testAlphaStripSetsAlphaOnColorEdit4Only() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* ce = panel->add<lvgui::ColorEdit4>("", lvgui::Color{ 100, 150, 200, 255 });
		step(ctx);

		openPopup(ctx, ce);
		PopupGeometry g = computeGeometry(ctx, ce, true);
		assert(!g.alpha.empty());

		clickAt(ctx, { g.alpha.left(), g.alpha.centre().y });
		assert(ce->value().a <= 5);   // left edge -> ~transparent

		// Popup stays open past a drag/click release (docs/gui/05, "ColorEdit") -- it
		// takes a second control click or an outside click to close, unlike DropDown's
		// one-shot item selection.
		assert(ctx.isPopupOpen());

		clickAt(ctx, { g.alpha.right(), g.alpha.centre().y });
		assert(ce->value().a >= 250);   // right edge -> ~opaque

		// RGB is untouched by alpha-strip drags.
		assert(ce->value().r == 100 && ce->value().g == 150 && ce->value().b == 200);

		std::cout << "✓ testAlphaStripSetsAlphaOnColorEdit4Only\n";
	}

	void testColorEdit3HasNoAlphaStripAndClickingThatRegionClosesPopup() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* ce = panel->add<lvgui::ColorEdit3>("", lvgui::Color{ 10, 20, 30, 255 });
		step(ctx);

		openPopup(ctx, ce);
		// Geometry AS IF hasAlpha were true -- ColorEdit3's actual (shorter) popup ends
		// well above this point, so a click here lands outside its real popup rect.
		PopupGeometry hypothetical = computeGeometry(ctx, ce, true);
		assert(!hypothetical.alpha.empty());

		// GuiContext's own "closes on any press outside the popup rect" rule (see its
		// openPopup()/update() comments) fires here, since ColorEdit3's popup doesn't
		// extend down to where a ColorEdit4's alpha strip would be.
		clickAt(ctx, { hypothetical.alpha.centre().x, hypothetical.alpha.centre().y });
		assert(!ctx.isPopupOpen());
		// Alpha was never touched -- ColorEdit3's initial value had a=255 and stays there.
		assert(ce->value().a == 255);

		std::cout << "✓ testColorEdit3HasNoAlphaStripAndClickingThatRegionClosesPopup\n";
	}

	void testDraggingBeyondSquareBoundsClampsRatherThanStopping() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* ce = panel->add<lvgui::ColorEdit3>("", lvgui::Color{ 0, 0, 0, 255 });
		step(ctx);

		openPopup(ctx, ce);
		PopupGeometry g = computeGeometry(ctx, ce, false);

		// Press inside the square, then drag to a point far outside every popup rect --
		// same "capture spans the whole gesture" contract as Slider's track drag and
		// Panel's resize grip (see ColorEdit.h's m_dragTarget comment). The drag must
		// still land on the clamped edge of the square, not leave the value wherever it
		// was on the press frame.
		ctx.injectMousePos(g.square.centre());
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMousePos({ -5000.0f, -5000.0f });
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		// Top-left of the square in local space = (mouse.x - x)/w clamped to 0, and
		// value = 1 - clamped(0) = 1 -- i.e. saturation 0, value 1 -> white.
		lvgui::Color result = ce->value();
		assert(approxEq(result.r, 255) && approxEq(result.g, 255) && approxEq(result.b, 255));

		std::cout << "✓ testDraggingBeyondSquareBoundsClampsRatherThanStopping\n";
	}

	void testHuePreservedAcrossDesaturateAndResaturateWithBoundPointer() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* ce = panel->add<lvgui::ColorEdit3>("", lvgui::Color{ 255, 0, 0, 255 });
		lvgui::Color bound = ce->value();
		ce->bind(&bound);
		step(ctx);

		openPopup(ctx, ce);
		PopupGeometry g = computeGeometry(ctx, ce, false);

		// Pin sat/val to 1, then set hue to 240 (blue) via the hue strip.
		clickAt(ctx, { g.square.right(), g.square.top() });
		clickAt(ctx, { g.hue.centre().x, g.hue.y + g.hue.h * (240.0f / 360.0f) });
		lvgui::Color afterHue = ce->value();
		assert(approxEq(afterHue.r, 0) && approxEq(afterHue.g, 0) && approxEq(afterHue.b, 255));
		assert(bound.b >= 250);   // bind() writeback happened

		// Desaturate to grey via the SV square's left edge -- no hue-strip interaction.
		clickAt(ctx, { g.square.left(), g.square.centre().y });
		lvgui::Color grey = ce->value();
		assert(std::abs(static_cast<int>(grey.r) - static_cast<int>(grey.g)) <= 3);
		assert(std::abs(static_cast<int>(grey.g) - static_cast<int>(grey.b)) <= 3);

		// Resaturate via the SV square's right edge, top row (sat=1, val=1) -- STILL no
		// hue-strip interaction. This is the case pullBoundValue() exists for
		// (ColorEdit.h): every step above went through commit(), which keeps the bound
		// pointer exactly equal to what the HSV cache predicts, so next frame's
		// pullBoundValue() must NOT treat that bind()-driven write as an external change
		// and must NOT resync the cache's hue back to greyscale's canonical 0 (red).
		clickAt(ctx, { g.square.right(), g.square.top() });
		lvgui::Color resaturated = ce->value();
		assert(approxEq(resaturated.r, 0) && approxEq(resaturated.g, 0) && approxEq(resaturated.b, 255));

		std::cout << "✓ testHuePreservedAcrossDesaturateAndResaturateWithBoundPointer\n";
	}

	void testSetValueResyncsHueImmediately() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		// Starts red (hue 0).
		auto* ce = panel->add<lvgui::ColorEdit3>("", lvgui::Color{ 255, 0, 0, 255 });
		step(ctx);

		// Explicit external set to green (hue 120) -- setValue()'s doc contract is that
		// this ALWAYS resyncs the HSV cache, unlike the lazy bind()-pull path.
		ce->setValue(lvgui::Color{ 0, 255, 0, 255 });

		openPopup(ctx, ce);
		PopupGeometry g = computeGeometry(ctx, ce, false);

		// Desaturate then resaturate without touching the hue strip -- if setValue()
		// hadn't resynced the cache, this would come back red (the stale hue-0 cache
		// from construction), not green.
		clickAt(ctx, { g.square.left(), g.square.centre().y });
		clickAt(ctx, { g.square.right(), g.square.top() });

		lvgui::Color result = ce->value();
		assert(approxEq(result.r, 0) && approxEq(result.g, 255) && approxEq(result.b, 0));

		std::cout << "✓ testSetValueResyncsHueImmediately\n";
	}

	void testOnChangeFiresOnDrag() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* ce = panel->add<lvgui::ColorEdit3>("", lvgui::Color{ 0, 0, 0, 255 });
		int fireCount = 0;
		ce->setOnChange([&](lvgui::Color) { ++fireCount; });
		step(ctx);

		openPopup(ctx, ce);
		PopupGeometry g = computeGeometry(ctx, ce, false);
		clickAt(ctx, { g.square.right(), g.square.top() });

		assert(fireCount >= 1);

		std::cout << "✓ testOnChangeFiresOnDrag\n";
	}

}

int main() {
	testPopupOpensOnControlClickAndClosesOnSecondClick();
	testClickInSVSquareCornersSetSaturationAndValueExtremes();
	testClickInHueStripSetsHue();
	testAlphaStripSetsAlphaOnColorEdit4Only();
	testColorEdit3HasNoAlphaStripAndClickingThatRegionClosesPopup();
	testDraggingBeyondSquareBoundsClampsRatherThanStopping();
	testHuePreservedAcrossDesaturateAndResaturateWithBoundPointer();
	testSetValueResyncsHueImmediately();
	testOnChangeFiresOnDrag();

	std::cout << "\n✅ All ColorEdit tests passed!\n";
	return 0;
}
