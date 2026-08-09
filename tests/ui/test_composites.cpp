// Phase 8, session B: CompositeWidget-derived widgets (Row, Vec3Field, CollapsingSection)
// Headless testing, same pattern as test_layout.cpp/test_slider.cpp.

#include <lightVulkanGraphics/ui/Ui.h>

#include <cassert>
#include <iostream>
#include <vector>

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

void testRowDividesHorizontallyByWeight() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 100.0f });

	// Create a Row with 3 children and custom weights (1:2:1)
	auto* row = panel->add<lvgui::Row>();
	row->add<lvgui::Label>("A");
	row->add<lvgui::Label>("B");
	row->add<lvgui::Label>("C");
	row->setWeights({ 1.0f, 2.0f, 1.0f });

	step(ctx);

	const lvgui::Theme& th = ctx.theme();
	float totalWidth = row->bounds().w;  // Use Row's actual width after layout
	float spacing = th.itemSpacing * 2.0f;
	float availableWidth = totalWidth - spacing;

	// Expected widths: A = availableWidth / 4, B = availableWidth / 2, C = availableWidth / 4
	float expectedA = availableWidth * 0.25f;
	float expectedB = availableWidth * 0.5f;
	float expectedC = availableWidth * 0.25f;

	lvgui::Rect boundsA = row->childAt(0)->bounds();
	lvgui::Rect boundsB = row->childAt(1)->bounds();
	lvgui::Rect boundsC = row->childAt(2)->bounds();

	assert(std::abs(boundsA.w - expectedA) < 1.0f);
	assert(std::abs(boundsB.w - expectedB) < 1.0f);
	assert(std::abs(boundsC.w - expectedC) < 1.0f);

	std::cout << "✓ testRowDividesHorizontallyByWeight\n";
}

void testVec3FieldOwnsThreDragValues() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 100.0f });

	glm::vec3 initial{ 1.0f, 2.0f, 3.0f };
	auto* field = panel->add<lvgui::Vec3Field>("Pos", initial);

	step(ctx);

	// Check initial value
	assert(field->childCount() == 3);
	assert(std::abs(field->value().x - 1.0f) < 0.01f);
	assert(std::abs(field->value().y - 2.0f) < 0.01f);
	assert(std::abs(field->value().z - 3.0f) < 0.01f);

	// Set a new value
	field->setValue({ 4.0f, 5.0f, 6.0f });
	step(ctx);

	assert(std::abs(field->value().x - 4.0f) < 0.01f);
	assert(std::abs(field->value().y - 5.0f) < 0.01f);
	assert(std::abs(field->value().z - 6.0f) < 0.01f);

	std::cout << "✓ testVec3FieldOwnsThreDragValues\n";
}

void testCollapsingHeaderWhenClosed() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 300.0f });

	auto* section = panel->add<lvgui::CollapsingSection>("Settings", false);  // Start closed
	section->add<lvgui::Label>("Child 1");
	section->add<lvgui::Label>("Child 2");

	step(ctx);

	const lvgui::Theme& th = ctx.theme();

	// When closed, preferredSize should be just header height
	lvgui::Vec2 pref = section->preferredSize(ctx);
	assert(std::abs(pref.y - th.rowHeight) < 0.1f);

	// Children should not be laid out when closed
	// (their bounds should not be set to a valid position within the panel)

	std::cout << "✓ testCollapsingHeaderWhenClosed\n";
}

void testCollapsingOpenedHeight() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 500.0f });

	auto* section = panel->add<lvgui::CollapsingSection>("Settings", true);  // Start open
	auto* child1 = section->add<lvgui::Label>("Child 1");
	auto* child2 = section->add<lvgui::Label>("Child 2");

	step(ctx);

	const lvgui::Theme& th = ctx.theme();

	// When open, preferredSize should include header + children
	lvgui::Vec2 pref = section->preferredSize(ctx);
	float child1Height = child1->preferredSize(ctx).y;
	float child2Height = child2->preferredSize(ctx).y;
	float expectedHeight = th.rowHeight + child1Height + child2Height + (th.itemSpacing * 2);
	assert(std::abs(pref.y - expectedHeight) < 1.0f);

	std::cout << "✓ testCollapsingOpenedHeight\n";
}

void testCollapsingToggleViaSetOpen() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 300.0f });

	auto* section = panel->add<lvgui::CollapsingSection>("Settings", false);
	section->add<lvgui::Label>("Child");

	step(ctx);
	assert(!section->isOpen());

	// Programmatically toggle via setOpen
	section->setOpen(true);
	step(ctx);
	assert(section->isOpen());

	section->setOpen(false);
	step(ctx);
	assert(!section->isOpen());

	std::cout << "✓ testCollapsingToggleViaSetOpen\n";
}

void testVec3FieldSubFieldsReportCorrectPanel() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 100.0f });

	glm::vec3 initial{ 1.0f, 2.0f, 3.0f };
	auto* field = panel->add<lvgui::Vec3Field>("Pos", initial);

	step(ctx);

	// Check that sub-fields (X, Y, Z drag values) report the same panel as their parent
	assert(field->childCount() == 3);
	for (std::size_t i = 0; i < 3; ++i) {
		auto* child = field->childAt(i);
		assert(child->panel() == panel);
	}

	std::cout << "✓ testVec3FieldSubFieldsReportCorrectPanel\n";
}

void testCollapsingHitTestWhenClosed() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 300.0f });

	auto* section = panel->add<lvgui::CollapsingSection>("Settings", false);  // Start closed
	auto* child = section->add<lvgui::Label>("Child");

	step(ctx);

	// When closed, section's layout() doesn't position children, but we can manually
	// check what the expected position would be
	const lvgui::Theme& th = ctx.theme();
	float expectedChildY = section->bounds().y + th.rowHeight + th.itemSpacing;

	// Hit-test at a point that would be in the child's area if it were laid out
	lvgui::Vec2 hitPointInChildArea{ section->bounds().x + 10.0f, expectedChildY + 10.0f };

	// When closed, children should not be hit-testable
	assert(section->hitTestDeep(hitPointInChildArea) == nullptr);

	// Hit-test in the header area (top of section)
	lvgui::Vec2 hitPointInHeader{ section->bounds().x + 10.0f, section->bounds().y + 5.0f };

	// Header should be hit-testable (returns the section itself)
	assert(section->hitTestDeep(hitPointInHeader) == section);

	std::cout << "✓ testCollapsingHitTestWhenClosed\n";
}

}

int main() {
	testRowDividesHorizontallyByWeight();
	testVec3FieldOwnsThreDragValues();
	testCollapsingHeaderWhenClosed();
	testCollapsingOpenedHeight();
	testCollapsingToggleViaSetOpen();
	testVec3FieldSubFieldsReportCorrectPanel();
	testCollapsingHitTestWhenClosed();

	std::cout << "\n✅ All composite widget tests passed!\n";
	return 0;
}
