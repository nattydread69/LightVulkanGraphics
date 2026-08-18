#include <lightVulkanGraphics/ui/Ui.h>

#include <cassert>
#include <cmath>
#include <iostream>

namespace lvgui = lightGraphics::ui;

namespace {

	// A minimal concrete CompositeWidget (docs/gui/05, "CompositeWidget") for exercising
	// the base class independently of Row/Vec3Field/CollapsingSection: one Label child,
	// stretched to the composite's full bounds every layout() call.
	class OneLabelComposite : public lvgui::CompositeWidget {
	public:
		lvgui::Label* label = add<lvgui::Label>("Hi");

		lvgui::Vec2 preferredSize(const lvgui::GuiContext& ctx) const override {
			return { 0.0f, ctx.theme().rowHeight };
		}
		void layout(const lvgui::GuiContext&) override {
			label->setBounds(bounds());
		}
	};

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

	void testThreeLabelsAndAButtonLayOutInOrder() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 100.0f, 100.0f, 300.0f, 400.0f });

		auto* l1 = panel->add<lvgui::Label>("One");
		auto* l2 = panel->add<lvgui::Label>("Two");
		auto* l3 = panel->add<lvgui::Label>("Three");
		auto* btn = panel->add<lvgui::Button>("Go");

		step(ctx);

		const lvgui::Theme& th = ctx.theme();
		float expectedX = panel->bounds().x + th.windowPadding;
		float expectedY = panel->bounds().y + th.titleBarHeight + th.windowPadding;

		assert(std::round(l1->bounds().x) == std::round(expectedX));
		assert(std::round(l1->bounds().y) == std::round(expectedY));
		assert(l1->bounds().w > 0.0f);

		// Declaration order = layout order: each row sits strictly below the previous one.
		assert(l2->bounds().y > l1->bounds().y);
		assert(l3->bounds().y > l2->bounds().y);
		assert(btn->bounds().y > l3->bounds().y);

		// All rows share the same x and width -- a single vertical stack.
		assert(l2->bounds().x == l1->bounds().x);
		assert(btn->bounds().w == l1->bounds().w);

		std::cout << "✓ testThreeLabelsAndAButtonLayOutInOrder\n";
	}

	void testTitleBarDragRespectsThresholdThenMoves() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 200.0f, 150.0f });

		// Press on the title bar.
		ctx.injectMousePos({ 100.0f, 55.0f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);

		lvgui::Rect afterPress = panel->bounds();
		assert(afterPress.x == 50.0f);

		// A 2px jitter while still held must NOT move the panel (4px drag threshold).
		ctx.injectMousePos({ 102.0f, 55.0f });
		step(ctx);
		assert(panel->bounds().x == afterPress.x);
		assert(panel->bounds().y == afterPress.y);

		// Moving well past the threshold must move the panel by the same delta.
		ctx.injectMousePos({ 130.0f, 55.0f });
		step(ctx);
		assert(panel->bounds().x == afterPress.x + 30.0f);
		assert(panel->bounds().y == afterPress.y);

		// Releasing stops the drag; further movement without a new press must not move it.
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		lvgui::Rect afterRelease = panel->bounds();

		ctx.injectMousePos({ 400.0f, 400.0f });
		step(ctx);
		assert(panel->bounds().x == afterRelease.x);
		assert(panel->bounds().y == afterRelease.y);

		std::cout << "✓ testTitleBarDragRespectsThresholdThenMoves\n";
	}

	void testZOrderClickBringsBackPanelToFront() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* back = ctx.createPanel("Back", { 50.0f, 50.0f, 200.0f, 150.0f });
		auto* front = ctx.createPanel("Front", { 300.0f, 50.0f, 200.0f, 150.0f });

		// The most recently created panel starts frontmost.
		assert(ctx.panelAt(0) == front);
		assert(ctx.panelAt(1) == back);

		// Click inside 'back', which does not overlap 'front'.
		ctx.injectMousePos({ 60.0f, 100.0f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);

		assert(ctx.panelAt(0) == back);
		assert(ctx.panelAt(1) == front);

		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		std::cout << "✓ testZOrderClickBringsBackPanelToFront\n";
	}

	void testWrappingLabelConvergesInOneFrame() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 160.0f, 400.0f });

		auto* label = panel->add<lvgui::Label>(
			"This is a fairly long sentence that should wrap across several lines "
			"once word wrap is enabled on a narrow panel.");
		label->setWordWrap(true);

		auto* after = panel->add<lvgui::Label>("After");

		step(ctx);

		float lh = ctx.font().lineHeight(ctx.theme().fontSize);
		// The wrapping label must occupy more than one line's height...
		assert(label->bounds().h > lh + 0.5f);
		// ...and the next row must already sit below it in THIS frame (not one frame
		// late), which is exactly why endFrame() lays out every panel twice.
		assert(after->bounds().y >= label->bounds().y + label->bounds().h);

		std::cout << "✓ testWrappingLabelConvergesInOneFrame\n";
	}

	void testSpacerAndSeparatorContributeFixedHeightRows() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 200.0f, 400.0f });

		auto* before = panel->add<lvgui::Label>("Before");
		auto* spacer = panel->add<lvgui::Spacer>(40.0f);
		auto* sep = panel->add<lvgui::Separator>();
		auto* after = panel->add<lvgui::Label>("After");

		step(ctx);

		assert(spacer->bounds().h == 40.0f);
		assert(spacer->bounds().y == before->bounds().y + before->bounds().h + ctx.theme().itemSpacing);
		assert(sep->bounds().y == spacer->bounds().y + spacer->bounds().h + ctx.theme().itemSpacing);
		assert(after->bounds().y == sep->bounds().y + sep->bounds().h + ctx.theme().itemSpacing);

		// Neither is a click target: a Spacer is never hit-testable, and a Separator
		// passes clicks through to whatever is behind it (docs/gui/05, base class).
		assert(!spacer->hitTest(spacer->bounds().centre()));
		assert(!sep->acceptsCapture());

		std::cout << "✓ testSpacerAndSeparatorContributeFixedHeightRows\n";
	}

	void testDisabledCompositeChildDrawsWithDisabledColorWithoutMutatingItsOwnFlag() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 200.0f });
		auto* composite = panel->add<OneLabelComposite>();

		step(ctx);

		const lvgui::Theme& th = ctx.theme();

		// Enabled composite: the child Label draws with the normal text colour.
		{
			lvgui::DrawList list;
			composite->draw(list, ctx);
			assert(!list.vertices().empty());
			for (const auto& v : list.vertices()) {
				assert(v.color == th.text.packed());
			}
		}

		// Disabling the COMPOSITE (not the label) must still grey the label out --
		// effectivelyEnabled() (docs/gui/05, "CompositeWidget") walks m_parent, so
		// Label::draw() sees the inherited disablement without CompositeWidget ever
		// touching the label's own m_enabled.
		composite->setEnabled(false);
		assert(composite->label->enabled());   // child's own flag left untouched

		{
			lvgui::DrawList list;
			composite->draw(list, ctx);
			assert(!list.vertices().empty());
			for (const auto& v : list.vertices()) {
				assert(v.color == th.textDisabled.packed());
			}
		}

		assert(composite->label->enabled());   // still untouched after drawing too

		std::cout << "✓ testDisabledCompositeChildDrawsWithDisabledColorWithoutMutatingItsOwnFlag\n";
	}

	void testCompositeChildBoundsReflectSameFrameResizeNoLag() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 200.0f });
		auto* composite = panel->add<OneLabelComposite>();

		step(ctx);
		lvgui::Rect firstLabelBounds = composite->label->bounds();
		assert(firstLabelBounds.w > 0.0f);

		// Move the panel (title bar drag) and confirm the child's bounds already
		// reflect the composite's NEW bounds by the end of the SAME frame the panel
		// moved in -- not one frame behind (docs/gui/05, "CompositeWidget": "children
		// must reflect the composite's bounds in the same frame those bounds change").
		// Widget::layout(ctx) is called synchronously by Panel::layout() right after
		// setBounds(), so there is no cached-from-an-earlier-call state involved.
		ctx.injectMousePos({ 50.0f, 5.0f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMousePos({ 150.0f, 55.0f });   // well past the 4px drag threshold
		step(ctx);

		lvgui::Rect movedCompositeBounds = composite->bounds();
		lvgui::Rect movedLabelBounds = composite->label->bounds();
		assert(movedCompositeBounds.x != firstLabelBounds.x);   // panel actually moved
		assert(movedLabelBounds.x == movedCompositeBounds.x);
		assert(movedLabelBounds.y == movedCompositeBounds.y);
		assert(movedLabelBounds.w == movedCompositeBounds.w);

		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		std::cout << "✓ testCompositeChildBoundsReflectSameFrameResizeNoLag\n";
	}

	void testCompositeViaBoundsOverrideStillLaysOutChildren() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 200.0f });
		auto* composite = panel->add<OneLabelComposite>();

		// docs/gui/06-layout-and-theme.md's absolute-placement escape hatch: setBounds()
		// is called from a DIFFERENT branch in Panel::layout() than the normal stack
		// path. A composite must still get its children positioned there too.
		composite->setBoundsOverride({ 20.0f, 30.0f, 100.0f, 22.0f });

		step(ctx);

		lvgui::Rect expected{ panel->bounds().x + 20.0f, panel->bounds().y + 30.0f, 100.0f, 22.0f };
		assert(composite->bounds().x == expected.x);
		assert(composite->label->bounds().x == composite->bounds().x);
		assert(composite->label->bounds().w == composite->bounds().w);

		std::cout << "✓ testCompositeViaBoundsOverrideStillLaysOutChildren\n";
	}

}

int main() {
	testThreeLabelsAndAButtonLayOutInOrder();
	testTitleBarDragRespectsThresholdThenMoves();
	testZOrderClickBringsBackPanelToFront();
	testWrappingLabelConvergesInOneFrame();
	testSpacerAndSeparatorContributeFixedHeightRows();
	testDisabledCompositeChildDrawsWithDisabledColorWithoutMutatingItsOwnFlag();
	testCompositeChildBoundsReflectSameFrameResizeNoLag();
	testCompositeViaBoundsOverrideStillLaysOutChildren();

	std::cout << "\n✅ All layout tests passed!\n";
	return 0;
}
