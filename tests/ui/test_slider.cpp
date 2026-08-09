// Phase 6 acceptance tests (docs/gui/08-implementation-plan.md, "Phase 6 -- Value
// widgets"): Checkbox, RadioGroup/RadioButton, SliderT<float|int>, DragValueT. Headless --
// no Vulkan device, no window, same pattern as test_layout.cpp/test_hittest.cpp.

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

	// Sets/clears the level-tracked modifier state (docs/gui/04's modsDown addition) the
	// same way a real Shift/Ctrl key press would: the specific key code doesn't matter,
	// only the mods bitmask carried on the event, so Key::Unknown stands in for "whichever
	// physical modifier key this is."
	void setModsHeld(lvgui::GuiContext& ctx, int mods) {
		ctx.injectKey(lvgui::Key::Unknown, mods, mods != 0, false);
	}

	// ---- Slider ------------------------------------------------------------------

	void testSliderDragBeyondPanelBoundsKeepsTracking() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 100.0f });
		// Empty label -> splitRow() gives the control the full row width (Widget.cpp),
		// which keeps this test's track-position math exactly equal to bounds().
		auto* slider = panel->add<lvgui::Slider>("", 0.0f, 100.0f, 50.0f);

		step(ctx);
		lvgui::Rect b = slider->bounds();
		float midY = b.y + b.h * 0.5f;

		ctx.injectMousePos({ b.x, midY });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(ctx.activeId() == slider->id());
		assert(slider->value() < 1.0f);   // pressed at the very start of the track

		// Drag the cursor 5000px past the panel's right edge -- capture (docs/gui/04,
		// "Capture") must keep the value tracking the cursor the whole way.
		ctx.injectMousePos({ b.x + b.w + 5000.0f, midY });
		step(ctx);
		assert(ctx.activeId() == slider->id());
		assert(slider->value() > 99.0f);   // clamped to the max, not stuck or lost

		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		assert(ctx.activeId() == lvgui::kInvalidWidgetId);

		std::cout << "✓ testSliderDragBeyondPanelBoundsKeepsTracking\n";
	}

	void testSliderShiftDragIsFineAdjustment() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 200.0f });
		// Two separate sliders, not one pressed twice: SliderT's own double-click
		// detection (docs/gui/05, "Double click") would otherwise treat a second press
		// this soon after the first as a double-click and reset instead of drag.
		auto* normalSlider = panel->add<lvgui::Slider>("", 0.0f, 100.0f, 0.0f);
		auto* shiftSlider = panel->add<lvgui::Slider>("", 0.0f, 100.0f, 0.0f);

		step(ctx);
		lvgui::Rect bn = normalSlider->bounds();
		lvgui::Rect bs = shiftSlider->bounds();

		// Baseline: press at the track start, then drag the full track width at normal
		// (1x) sensitivity -- should land near the top of the range.
		ctx.injectMousePos({ bn.x, bn.y + bn.h * 0.5f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMousePos({ bn.x + bn.w, bn.y + bn.h * 0.5f });
		step(ctx);
		float normalValue = normalSlider->value();
		assert(normalValue > 90.0f);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		// Same full-width drag on the OTHER slider, but with Shift held throughout --
		// 0.1x sensitivity (docs/gui/05, "Shift + drag") must land well short of the
		// unshifted result.
		ctx.injectMousePos({ bs.x, bs.y + bs.h * 0.5f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		setModsHeld(ctx, lvgui::Mod::Shift);
		ctx.injectMousePos({ bs.x + bs.w, bs.y + bs.h * 0.5f });
		step(ctx);
		float shiftValue = shiftSlider->value();
		setModsHeld(ctx, 0);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		assert(shiftValue < normalValue * 0.3f);
		assert(shiftValue > 1.0f);   // still moved, just far less

		std::cout << "✓ testSliderShiftDragIsFineAdjustment\n";
	}

	void testSliderDoubleClickResetsToInitialValue() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 100.0f });
		auto* slider = panel->add<lvgui::Slider>("", 0.0f, 100.0f, 42.0f);

		step(ctx);
		lvgui::Rect b = slider->bounds();
		float midY = b.y + b.h * 0.5f;

		// First click, at the far end of the track -- moves the value away from initial.
		ctx.injectMousePos({ b.x + b.w, midY });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		assert(slider->value() > 90.0f);

		// Second click at the same spot, soon after -- a double-click, which must reset
		// to the construction-time initial value (42) rather than jumping to the cursor
		// again (which would just re-land near 100).
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(std::abs(slider->value() - 42.0f) < 1.0f);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		std::cout << "✓ testSliderDoubleClickResetsToInitialValue\n";
	}

	void testSliderIntStepClampsAfterRoundingNeverExceedsRange() {
		// docs/gui/10-testing.md, "Range/stepping edge cases": round-then-clamp, not
		// clamp-then-round, or 0-10 step 3 can produce 12 at the extremes.
		lvgui::SliderInt s("n", 0, 10, 0);
		s.setStep(3);
		for (float t = -0.5f; t <= 1.5f; t += 0.01f) {
			s.setValueFromNormalised(t);
			assert(s.value() >= 0 && s.value() <= 10);
		}
		std::cout << "✓ testSliderIntStepClampsAfterRoundingNeverExceedsRange\n";
	}

	void testSliderLogarithmicPlacesMidRangeValueNearMiddle() {
		lvgui::Slider s("timestep", 1.0e-4f, 1.0e-1f, 1.0e-4f);
		s.setScale(lvgui::SliderScale::Logarithmic);
		s.setValue(1.0e-2f);

		// Linear scale would place 1e-2 at t = (1e-2 - 1e-4) / (1e-1 - 1e-4) ~= 0.099 --
		// squashed against the low end. Logarithmic scale places the same value at
		// t = 2/3 (two of the three represented decades from the low end): well clear of
		// both extremes, unlike the linear case.
		float t = s.normalisedValue();
		assert(t > 0.5f && t < 0.85f);

		std::cout << "✓ testSliderLogarithmicPlacesMidRangeValueNearMiddle\n";
	}

	void testSliderBindReflectsExternalMutationNextFrame() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 100.0f });
		auto* slider = panel->add<lvgui::Slider>("", 0.0f, 10.0f, 0.0f);

		float bound = 5.0f;
		slider->bind(&bound);
		step(ctx);
		assert(slider->value() == 5.0f);

		bound = 8.0f;   // mutated externally, e.g. by the simulation loading a preset
		step(ctx);
		assert(slider->value() == 8.0f);

		std::cout << "✓ testSliderBindReflectsExternalMutationNextFrame\n";
	}

	void testSliderOnChangeDuringDragOnCommitOnceOnRelease() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 100.0f });
		auto* slider = panel->add<lvgui::Slider>("", 0.0f, 100.0f, 0.0f);

		int changeCount = 0, commitCount = 0;
		slider->setOnChange([&](float) { ++changeCount; });
		slider->setOnCommit([&](float) { ++commitCount; });

		step(ctx);
		lvgui::Rect b = slider->bounds();
		float midY = b.y + b.h * 0.5f;

		ctx.injectMousePos({ b.x, midY });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(changeCount == 1);
		assert(commitCount == 0);

		ctx.injectMousePos({ b.x + b.w * 0.3f, midY });
		step(ctx);
		ctx.injectMousePos({ b.x + b.w * 0.6f, midY });
		step(ctx);
		assert(changeCount == 3);   // fired on press, and on each of the two drag frames
		assert(commitCount == 0);

		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		assert(commitCount == 1);   // fires exactly once, on release
		assert(changeCount == 3);   // release itself is not a change

		std::cout << "✓ testSliderOnChangeDuringDragOnCommitOnceOnRelease\n";
	}

	// ---- Checkbox ------------------------------------------------------------------

	void testCheckboxTogglesOnWholeRowOnPressNotRelease() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 100.0f });
		auto* cb = panel->add<lvgui::Checkbox>("Show grid", false);

		step(ctx);
		lvgui::Rect b = cb->bounds();
		// The box itself lives in the control column (docs/gui/05, "Checkbox", the
		// convention decision); clicking near the LEFT edge -- the label column -- must
		// still toggle it, which is the point of this test.
		lvgui::Vec2 onLabelArea{ b.x + 2.0f, b.y + b.h * 0.5f };

		ctx.injectMousePos(onLabelArea);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(cb->value() == true);   // toggled on PRESS

		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		assert(cb->value() == true);   // release does not toggle again

		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(cb->value() == false);   // second press toggles back off

		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		std::cout << "✓ testCheckboxTogglesOnWholeRowOnPressNotRelease\n";
	}

	void testCheckboxTriStateCyclesThroughIndeterminate() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 100.0f });
		auto* cb = panel->add<lvgui::Checkbox>("Tri", false);
		cb->setTriState(true);

		step(ctx);
		lvgui::Rect b = cb->bounds();
		lvgui::Vec2 onRow{ b.x + 2.0f, b.y + b.h * 0.5f };
		ctx.injectMousePos(onRow);

		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(cb->state() == lvgui::Checkbox::State::On);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(cb->state() == lvgui::Checkbox::State::Indeterminate);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(cb->state() == lvgui::Checkbox::State::Off);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		std::cout << "✓ testCheckboxTriStateCyclesThroughIndeterminate\n";
	}

	// ---- RadioButton -----------------------------------------------------------

	void testRadioButtonExclusiveSelectionCannotToggleOff() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 200.0f });
		lvgui::RadioGroup group;
		auto* r0 = panel->add<lvgui::RadioButton>("A", &group, 0);
		auto* r1 = panel->add<lvgui::RadioButton>("B", &group, 1);

		step(ctx);
		lvgui::Rect b0 = r0->bounds();
		lvgui::Vec2 onR0{ b0.x + 2.0f, b0.y + b0.h * 0.5f };

		ctx.injectMousePos(onR0);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(group.value() == 0);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		// Clicking the already-selected button again must NOT toggle it off.
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(group.value() == 0);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		lvgui::Rect b1 = r1->bounds();
		ctx.injectMousePos({ b1.x + 2.0f, b1.y + b1.h * 0.5f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(group.value() == 1);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		std::cout << "✓ testRadioButtonExclusiveSelectionCannotToggleOff\n";
	}

	// ---- DragValue ---------------------------------------------------------------

	void testDragValueDragBeyondPanelBoundsKeepsTrackingAndShiftScales() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 100.0f });
		auto* drag = panel->add<lvgui::DragValueT<float>>("", 0.0f, 1.0f);   // speed = 1 unit/px

		step(ctx);
		lvgui::Rect b = drag->bounds();
		float midY = b.y + b.h * 0.5f;

		ctx.injectMousePos({ b.x, midY });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(ctx.activeId() == drag->id());

		// Drag far outside the panel -- capture must keep the value tracking (same
		// contract as Slider; DragValue has no track to clamp against, so it should just
		// keep accumulating).
		ctx.injectMousePos({ b.x + 5000.0f, midY });
		step(ctx);
		assert(drag->value() > 4000.0f);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		// Shift = 0.1x sensitivity (docs/gui/05, "DragValue").
		drag->setValue(0.0f);
		ctx.injectMousePos({ b.x, midY });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		setModsHeld(ctx, lvgui::Mod::Shift);
		ctx.injectMousePos({ b.x + 100.0f, midY });
		step(ctx);
		float shiftValue = drag->value();
		setModsHeld(ctx, 0);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		assert(shiftValue > 5.0f && shiftValue < 15.0f);   // ~10, not ~100

		std::cout << "✓ testDragValueDragBeyondPanelBoundsKeepsTrackingAndShiftScales\n";
	}

}

int main() {
	testSliderDragBeyondPanelBoundsKeepsTracking();
	testSliderShiftDragIsFineAdjustment();
	testSliderDoubleClickResetsToInitialValue();
	testSliderIntStepClampsAfterRoundingNeverExceedsRange();
	testSliderLogarithmicPlacesMidRangeValueNearMiddle();
	testSliderBindReflectsExternalMutationNextFrame();
	testSliderOnChangeDuringDragOnCommitOnceOnRelease();

	testCheckboxTogglesOnWholeRowOnPressNotRelease();
	testCheckboxTriStateCyclesThroughIndeterminate();

	testRadioButtonExclusiveSelectionCannotToggleOff();

	testDragValueDragBeyondPanelBoundsKeepsTrackingAndShiftScales();

	std::cout << "\n✅ All phase 6 value-widget tests passed!\n";
	return 0;
}
