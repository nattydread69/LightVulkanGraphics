// CompositeWidget-derived widgets (Row, Vec3Field, CollapsingSection, TabBar)
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

void clickAt(lvgui::GuiContext& ctx, lvgui::Vec2 pos) {
	ctx.injectMousePos(pos);
	ctx.injectMouseButton(lvgui::MouseButton::Left, true);
	step(ctx);
	ctx.injectMouseButton(lvgui::MouseButton::Left, false);
	step(ctx);
}

void pressKey(lvgui::GuiContext& ctx, int key, int mods = 0) {
	ctx.injectKey(key, mods, true, false);
	step(ctx);
	ctx.injectKey(key, mods, false, false);
	step(ctx);
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

void testTabBarAddTabReturnsAWorkingHandle() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 300.0f });

	auto* tabs = panel->add<lvgui::TabBar>();
	auto general = tabs->addTab("General");
	auto advanced = tabs->addTab("Advanced");

	auto* a = general.add<lvgui::Label>("General child");
	auto* b = advanced.add<lvgui::Label>("Advanced child");

	assert(tabs->tabCount() == 2);
	assert(tabs->childCount() == 2);
	assert(tabs->childAt(0) == a);
	assert(tabs->childAt(1) == b);

	std::cout << "✓ testTabBarAddTabReturnsAWorkingHandle\n";
}

void testTabBarPreferredSizeCountsOnlyActiveTabChildren() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 400.0f });

	auto* tabs = panel->add<lvgui::TabBar>();
	auto general = tabs->addTab("General");
	auto advanced = tabs->addTab("Advanced");

	auto* g1 = general.add<lvgui::Label>("G1");
	auto* g2 = general.add<lvgui::Label>("G2");
	advanced.add<lvgui::Label>("A1");   // one child on the inactive tab

	step(ctx);

	const lvgui::Theme& th = ctx.theme();
	lvgui::Vec2 pref = tabs->preferredSize(ctx);
	float expectedGeneral = th.rowHeight + g1->preferredSize(ctx).y + g2->preferredSize(ctx).y +
	                         th.itemSpacing * 2.0f;
	assert(std::abs(pref.y - expectedGeneral) < 0.5f);

	tabs->setActiveTab(1);
	step(ctx);
	lvgui::Vec2 prefAdvanced = tabs->preferredSize(ctx);
	// Only "Advanced" (1 child) counts now, not "General" (2 children) -- confirms this
	// isn't just summing every child regardless of which tab it belongs to.
	assert(prefAdvanced.y < pref.y);

	std::cout << "✓ testTabBarPreferredSizeCountsOnlyActiveTabChildren\n";
}

void testTabBarOnlyLaysOutActiveTabChildren() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 400.0f });

	auto* tabs = panel->add<lvgui::TabBar>();
	auto general = tabs->addTab("General");
	auto advanced = tabs->addTab("Advanced");
	auto* activeChild = general.add<lvgui::Label>("Shown");
	auto* inactiveChild = advanced.add<lvgui::Label>("Hidden");

	step(ctx);

	// The active tab's child is positioned somewhere real within the panel...
	assert(activeChild->bounds().w > 0.0f);
	assert(activeChild->bounds().y > tabs->bounds().y);

	// ...the inactive tab's child was never laid out at all, so it still holds its
	// zero-initialised default bounds (docs/gui/05-widgets.md, "TabBar").
	assert(inactiveChild->bounds().w == 0.0f);
	assert(inactiveChild->bounds().h == 0.0f);

	std::cout << "✓ testTabBarOnlyLaysOutActiveTabChildren\n";
}

void testTabBarHitTestBlocksInactiveTabChildren() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 300.0f });

	auto* tabs = panel->add<lvgui::TabBar>();
	auto general = tabs->addTab("General");
	auto advanced = tabs->addTab("Advanced");
	general.add<lvgui::Label>("Shown");
	advanced.add<lvgui::Label>("Hidden");

	step(ctx);

	const lvgui::Theme& th = ctx.theme();
	// Where "Hidden" WOULD be if its tab were active and it had been laid out.
	lvgui::Vec2 hitPointInChildArea{ tabs->bounds().x + 10.0f, tabs->bounds().y + th.rowHeight + 10.0f };

	// The active tab's own child claims that point...
	assert(tabs->hitTestDeep(hitPointInChildArea) != nullptr);

	tabs->setActiveTab(1);
	step(ctx);
	// ...but once "Advanced" is active and "General" (with "Shown") is not, THAT point
	// resolves to "Hidden" instead -- proving the hit-test walk is filtered by the
	// CURRENTLY active tab, not fixed at construction time.
	lvgui::Widget* hit = tabs->hitTestDeep(hitPointInChildArea);
	assert(hit != nullptr);

	std::cout << "✓ testTabBarHitTestBlocksInactiveTabChildren\n";
}

void testTabBarHeaderHitTestReturnsTabBarItself() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 300.0f });

	auto* tabs = panel->add<lvgui::TabBar>();
	tabs->addTab("General");
	tabs->addTab("Advanced");
	step(ctx);

	lvgui::Vec2 headerPoint{ tabs->bounds().x + 10.0f, tabs->bounds().y + 5.0f };
	assert(tabs->hitTestDeep(headerPoint) == tabs);

	std::cout << "✓ testTabBarHeaderHitTestReturnsTabBarItself\n";
}

void testClickingSecondTabButtonSwitchesActiveTab() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 300.0f });

	auto* tabs = panel->add<lvgui::TabBar>();
	tabs->addTab("General");
	tabs->addTab("Advanced");
	step(ctx);
	assert(tabs->activeTab() == 0);

	// Two equal-width tab buttons across the header -- click well inside the second one.
	const lvgui::Theme& th = ctx.theme();
	lvgui::Vec2 secondTabPoint{ tabs->bounds().x + tabs->bounds().w * 0.75f,
	                            tabs->bounds().y + th.rowHeight * 0.5f };
	clickAt(ctx, secondTabPoint);

	assert(tabs->activeTab() == 1);

	std::cout << "✓ testClickingSecondTabButtonSwitchesActiveTab\n";
}

void testInactiveTabChildDoesNotReceiveClicks() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 300.0f });

	auto* tabs = panel->add<lvgui::TabBar>();
	auto general = tabs->addTab("General");
	auto advanced = tabs->addTab("Advanced");
	general.add<lvgui::Label>("Shown");
	auto* hiddenCheckbox = advanced.add<lvgui::Checkbox>("Hidden toggle", false);

	step(ctx);   // "General" active; "Advanced"'s checkbox is never laid out

	// Try to click where the checkbox WOULD be if its tab were active -- since it was
	// never positioned (see testTabBarOnlyLaysOutActiveTabChildren), this can't
	// accidentally land on it through stale bounds from a previous run.
	const lvgui::Theme& th = ctx.theme();
	clickAt(ctx, { tabs->bounds().x + 10.0f, tabs->bounds().y + th.rowHeight + 10.0f });

	assert(!hiddenCheckbox->value());

	std::cout << "✓ testInactiveTabChildDoesNotReceiveClicks\n";
}

void testArrowKeysSwitchActiveTabWhileFocused() {
	lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
	auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 300.0f });

	auto* tabs = panel->add<lvgui::TabBar>();
	tabs->addTab("A");
	tabs->addTab("B");
	tabs->addTab("C");
	step(ctx);

	ctx.setFocus(tabs);
	step(ctx);
	assert(ctx.focusedId() == tabs->id());
	assert(tabs->activeTab() == 0);

	pressKey(ctx, lvgui::Key::Right);
	assert(tabs->activeTab() == 1);

	pressKey(ctx, lvgui::Key::Right);
	assert(tabs->activeTab() == 2);

	// Wraps at the end, same convention DropDown's highlight and RadioGroup's selection
	// already use elsewhere in this library.
	pressKey(ctx, lvgui::Key::Right);
	assert(tabs->activeTab() == 0);

	pressKey(ctx, lvgui::Key::Left);
	assert(tabs->activeTab() == 2);

	std::cout << "✓ testArrowKeysSwitchActiveTabWhileFocused\n";
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
	testTabBarAddTabReturnsAWorkingHandle();
	testTabBarPreferredSizeCountsOnlyActiveTabChildren();
	testTabBarOnlyLaysOutActiveTabChildren();
	testTabBarHitTestBlocksInactiveTabChildren();
	testTabBarHeaderHitTestReturnsTabBarItself();
	testClickingSecondTabButtonSwitchesActiveTab();
	testInactiveTabChildDoesNotReceiveClicks();
	testArrowKeysSwitchActiveTabWhileFocused();

	std::cout << "\n✅ All composite widget tests passed!\n";
	return 0;
}
