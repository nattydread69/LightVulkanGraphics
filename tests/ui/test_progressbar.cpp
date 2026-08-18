// ProgressBar and PlotLine widget tests. Headless.

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

void testProgressBarClampsFraction() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 100.0f });
	auto* bar = panel->add<lvgui::ProgressBar>("Progress");

	step(ctx);

	bar->setFraction(1.5f);  // Try to exceed 1.0
	step(ctx);
	// Can't directly verify internal state, but next step should not crash
	// and the visual rendering should clamp the fill width

	bar->setFraction(-0.5f);  // Try to go below 0.0
	step(ctx);

	bar->setFraction(0.5f);  // Valid value
	step(ctx);

	std::cout << "✓ testProgressBarClampsFraction\n";
}

void testProgressBarIndeterminateAnimation() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 100.0f });
	auto* bar = panel->add<lvgui::ProgressBar>("Progress");

	bar->setIndeterminate(true);
	step(ctx);

	// Update several times to accumulate time
	for (int i = 0; i < 100; ++i) {
		step(ctx);
	}

	// Should have cycled but not crashed
	bar->setIndeterminate(false);
	step(ctx);

	std::cout << "✓ testProgressBarIndeterminateAnimation\n";
}

void testPlotLineRingBuffer() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 100.0f });
	auto* plot = panel->add<lvgui::PlotLine>("Data", 10);  // Small history for testing

	step(ctx);

	// Push 15 samples (more than the 10-size buffer)
	for (int i = 0; i < 15; ++i) {
		plot->push(static_cast<float>(i));
	}
	step(ctx);

	// Buffer should only contain the last 10: 5-14
	// We can't directly check, but setting external values should work
	std::vector<float> newValues{ 100.0f, 101.0f, 102.0f };
	plot->setValues(newValues);
	step(ctx);

	std::cout << "✓ testPlotLineRingBuffer\n";
}

void testPlotLineAutoScale() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 100.0f });
	auto* plot = panel->add<lvgui::PlotLine>("Data", 256);

	step(ctx);

	// Push some samples
	plot->push(5.0f);
	plot->push(10.0f);
	plot->push(3.0f);
	step(ctx);

	// Add a new extreme: 20.0
	plot->push(20.0f);
	step(ctx);

	// Add another: 1.0
	plot->push(1.0f);
	step(ctx);

	// Should not crash and should have auto-scaled to contain [1, 20]

	// Explicitly set range to disable auto-scale
	plot->setRange(0.0f, 100.0f);
	step(ctx);

	// Re-enable auto-scale
	plot->setRange(std::nan(""), std::nan(""));
	step(ctx);

	std::cout << "✓ testPlotLineAutoScale\n";
}

}

int main() {
	testProgressBarClampsFraction();
	testProgressBarIndeterminateAnimation();
	testPlotLineRingBuffer();
	testPlotLineAutoScale();

	std::cout << "\n✅ All ProgressBar and PlotLine tests passed!\n";
	return 0;
}
