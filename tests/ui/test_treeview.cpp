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

// docs/gui/05-widgets.md, "TreeView" -- headless, same pattern as test_listbox.cpp: no
// Vulkan device, no window. Building and removing nodes, which rows show, and every
// interaction (row and arrow clicks, Shift+click, double-click, the keyboard,
// type-ahead, scrolling) driven through injected input.

#include <lightVulkanGraphics/ui/Ui.h>

#include <cassert>
#include <iostream>
#include <string>
#include <utility>
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

	// Long enough that the next click can't pair up with the last into a double-click.
	void pause(lvgui::GuiContext& ctx) {
		step(ctx, 1.0f);
	}

	void clickAt(lvgui::GuiContext& ctx, lvgui::Vec2 pos, int mods = 0) {
		// Modifier state rides on key events (it is the full bitmask, GLFW-style).
		if (mods != 0) {
			ctx.injectKey(lvgui::Key::Unknown, mods, true, false);
		}
		ctx.injectMousePos(pos);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		if (mods != 0) {
			ctx.injectKey(lvgui::Key::Unknown, 0, false, false);
			step(ctx);
		}
	}

	void pressKey(lvgui::GuiContext& ctx, int key) {
		ctx.injectKey(key, 0, true, false);
		step(ctx);
		ctx.injectKey(key, 0, false, false);
		step(ctx);
	}

	lvgui::Vec2 centre(const lvgui::Rect& r) {
		return { r.x + r.w * 0.5f, r.y + r.h * 0.5f };
	}

	// A point on the row's text, clear of the disclosure arrow.
	lvgui::Vec2 textOf(const lvgui::Rect& row) {
		return { row.x + row.w * 0.75f, row.y + row.h * 0.5f };
	}

	// Scene
	//   Lights
	//     Key
	//     Fill
	//   Meshes
	//     Body
	//       Arm
	//         Hand
	//     Floor
	// Camera
	struct SceneIds {
		int scene, lights, key, fill, meshes, body, arm, hand, floor, camera;
	};

	SceneIds buildScene(lvgui::TreeView& tree) {
		SceneIds s{};
		s.scene = tree.addNode("Scene");
		s.lights = tree.addNode("Lights", s.scene);
		s.key = tree.addNode("Key", s.lights);
		s.fill = tree.addNode("Fill", s.lights);
		s.meshes = tree.addNode("Meshes", s.scene);
		s.body = tree.addNode("Body", s.meshes);
		s.arm = tree.addNode("Arm", s.body);
		s.hand = tree.addNode("Hand", s.arm);
		s.floor = tree.addNode("Floor", s.meshes);
		s.camera = tree.addNode("Camera");
		return s;
	}

	struct Fixture {
		lvgui::GuiContext ctx{ testCreateInfo(), lvgui::PlatformHooks{} };
		lvgui::Panel* panel = nullptr;
		lvgui::TreeView* tree = nullptr;
		SceneIds ids{};
		std::vector<int> selections;
		std::vector<int> activations;
		std::vector<std::pair<int, bool>> toggles;

		Fixture() {
			panel = ctx.createPanel("Tree", { 20.0f, 20.0f, 360.0f, 320.0f }, lvgui::PanelFlags::None);
			tree = panel->add<lvgui::TreeView>();
			tree->setVisibleRows(10);
			ids = buildScene(*tree);
			tree->setOnSelectionChange([this](int node) { selections.push_back(node); });
			tree->setOnActivate([this](int node) { activations.push_back(node); });
			tree->setOnToggle([this](int node, bool expanded) { toggles.emplace_back(node, expanded); });
			step(ctx);
		}

		void click(int node) {
			clickAt(ctx, textOf(tree->rowRect(ctx, node)));
		}
		void clickArrow(int node, int mods = 0) {
			clickAt(ctx, centre(tree->toggleRect(ctx, node)), mods);
		}
	};

	void testBuildingTheTree() {
		lvgui::TreeView tree;
		SceneIds s = buildScene(tree);
		assert(s.scene == 0 && s.lights == 1 && s.camera == 9);   // ids count up from 0
		assert(tree.nodeCount() == 10);
		assert((tree.childrenOf(-1) == std::vector<int>{ s.scene, s.camera }));
		assert((tree.childrenOf(s.meshes) == std::vector<int>{ s.body, s.floor }));
		assert(tree.parentOf(s.hand) == s.arm);
		assert(tree.parentOf(s.scene) == -1);
		assert(tree.depthOf(s.scene) == 0 && tree.depthOf(s.hand) == 4);

		tree.setNodeDetail(s.key, "spot");
		tree.setNodeUserData(s.key, 42);
		tree.setNodeText(s.key, "Key light");
		assert(tree.nodeText(s.key) == "Key light");
		assert(tree.nodeDetail(s.key) == "spot");
		assert(tree.nodeUserData(s.key) == 42);
		assert(tree.nodeUserData(s.fill) == 0);

		// A parent that doesn't exist adds nothing; queries on bad ids are harmless.
		assert(tree.addNode("orphan", 99) == -1);
		assert(tree.addNode("orphan", -2) == -1);
		assert(tree.nodeCount() == 10);
		assert(!tree.isValid(99) && !tree.isValid(-1));
		assert(tree.nodeText(99).empty() && tree.childrenOf(99).empty() && tree.parentOf(99) == -1);

		tree.clear();
		assert(tree.nodeCount() == 0 && tree.childrenOf(-1).empty());
		assert(tree.addNode("fresh") == 0);   // ids start over

		std::cout << "✓ testBuildingTheTree\n";
	}

	void testOnlyExpandedBranchesShow() {
		lvgui::TreeView tree;
		SceneIds s = buildScene(tree);
		assert((tree.visibleNodes() == std::vector<int>{ s.scene, s.camera }));   // new nodes start collapsed

		tree.setExpanded(s.scene, true);
		assert((tree.visibleNodes() == std::vector<int>{ s.scene, s.lights, s.meshes, s.camera }));

		tree.setExpanded(s.scene, true, true);   // the whole subtree, in pre-order
		assert((tree.visibleNodes() ==
			std::vector<int>{ s.scene, s.lights, s.key, s.fill, s.meshes, s.body, s.arm, s.hand, s.floor, s.camera }));

		// Collapsing a branch hides everything under it, but its descendants remember
		// their own state for when it opens again.
		tree.setExpanded(s.meshes, false);
		assert((tree.visibleNodes() == std::vector<int>{ s.scene, s.lights, s.key, s.fill, s.meshes, s.camera }));
		tree.setExpanded(s.meshes, true);
		assert(tree.visibleNodes().size() == 10);

		tree.collapseAll();
		assert(tree.visibleNodes().size() == 2);
		tree.expandAll();
		assert(tree.visibleNodes().size() == 10);

		// A node marked expanded before it has children shows them as they arrive.
		int extra = tree.addNode("Extras");
		tree.setExpanded(extra, true);
		int child = tree.addNode("Child", extra);
		assert(tree.visibleNodes().back() == child);

		std::cout << "✓ testOnlyExpandedBranchesShow\n";
	}

	void testClickingARowSelectsItAndTheArrowToggles() {
		Fixture f;
		f.click(f.ids.camera);
		assert(f.tree->selectedNode() == f.ids.camera);
		assert((f.selections == std::vector<int>{ f.ids.camera }));

		pause(f.ctx);
		f.clickArrow(f.ids.scene);
		assert(f.tree->isExpanded(f.ids.scene));
		assert(f.tree->selectedNode() == f.ids.camera);   // the arrow doesn't select
		assert(f.selections.size() == 1);
		assert((f.toggles == std::vector<std::pair<int, bool>>{ { f.ids.scene, true } }));
		assert(f.tree->visibleNodes().size() == 4);

		pause(f.ctx);
		f.clickArrow(f.ids.scene);
		assert(!f.tree->isExpanded(f.ids.scene));
		assert(f.toggles.back() == std::make_pair(f.ids.scene, false));

		// Leaves have no arrow, and clicking where it would be just selects the row.
		assert(f.tree->toggleRect(f.ctx, f.ids.camera).empty());

		std::cout << "✓ testClickingARowSelectsItAndTheArrowToggles\n";
	}

	void testCollapsingMovesAHiddenSelectionUp() {
		Fixture f;
		f.tree->setSelectedNode(f.ids.hand);   // reveals it: every ancestor opens
		step(f.ctx);
		assert(f.tree->isExpanded(f.ids.scene) && f.tree->isExpanded(f.ids.arm));
		assert(f.selections.empty());          // programmatic: no callback

		f.clickArrow(f.ids.meshes);
		assert(f.tree->selectedNode() == f.ids.meshes);
		assert((f.selections == std::vector<int>{ f.ids.meshes }));
		assert((f.toggles == std::vector<std::pair<int, bool>>{ { f.ids.meshes, false } }));

		// Collapsing a sibling branch leaves the selection alone.
		pause(f.ctx);
		f.clickArrow(f.ids.lights);   // opens it
		pause(f.ctx);
		f.clickArrow(f.ids.lights);   // and closes it again
		assert(f.tree->selectedNode() == f.ids.meshes);
		assert(f.selections.size() == 1);

		std::cout << "✓ testCollapsingMovesAHiddenSelectionUp\n";
	}

	void testShiftClickTogglesTheWholeSubtree() {
		Fixture f;
		f.clickArrow(f.ids.scene, lvgui::Mod::Shift);
		assert(f.tree->visibleNodes().size() == 10);   // everything under Scene, plus Camera
		// One callback per node that actually changed -- the ones with children.
		assert(f.toggles.size() == 5);   // Scene, Lights, Meshes, Body, Arm
		for (const auto& t : f.toggles) {
			assert(t.second);
		}

		pause(f.ctx);
		f.toggles.clear();
		f.clickArrow(f.ids.scene, lvgui::Mod::Shift);
		assert(f.tree->visibleNodes().size() == 2);
		assert(f.toggles.size() == 5);
		f.tree->setExpanded(f.ids.scene, true);
		assert(f.tree->visibleNodes().size() == 4);   // the children closed too

		std::cout << "✓ testShiftClickTogglesTheWholeSubtree\n";
	}

	void testDoubleClickActivatesAndOpensParents() {
		Fixture f;
		f.click(f.ids.camera);
		f.click(f.ids.camera);
		assert((f.activations == std::vector<int>{ f.ids.camera }));
		assert(f.toggles.empty());   // a leaf has nothing to open

		pause(f.ctx);
		f.click(f.ids.scene);
		f.click(f.ids.scene);
		assert(f.activations.back() == f.ids.scene);
		assert(f.tree->isExpanded(f.ids.scene));
		assert(f.toggles.size() == 1);

		// A third quick click starts a new pair rather than activating again.
		f.click(f.ids.scene);
		assert(f.activations.size() == 2);

		// Two clicks on different rows are not a double-click.
		pause(f.ctx);
		f.click(f.ids.lights);
		f.click(f.ids.meshes);
		assert(f.activations.size() == 2);

		std::cout << "✓ testDoubleClickActivatesAndOpensParents\n";
	}

	void testKeyboardNavigation() {
		Fixture f;
		f.click(f.ids.scene);   // focuses the tree
		assert(f.ctx.focusedId() == f.tree->id());

		pressKey(f.ctx, lvgui::Key::Down);
		assert(f.tree->selectedNode() == f.ids.camera);
		pressKey(f.ctx, lvgui::Key::Down);   // no wrap-around
		assert(f.tree->selectedNode() == f.ids.camera);
		pressKey(f.ctx, lvgui::Key::Up);
		assert(f.tree->selectedNode() == f.ids.scene);

		// Right opens a collapsed node, then steps into its first child.
		pressKey(f.ctx, lvgui::Key::Right);
		assert(f.tree->isExpanded(f.ids.scene) && f.tree->selectedNode() == f.ids.scene);
		pressKey(f.ctx, lvgui::Key::Right);
		assert(f.tree->selectedNode() == f.ids.lights);
		pressKey(f.ctx, lvgui::Key::Right);
		pressKey(f.ctx, lvgui::Key::Right);
		assert(f.tree->selectedNode() == f.ids.key);
		pressKey(f.ctx, lvgui::Key::Right);   // a leaf: nothing to do
		assert(f.tree->selectedNode() == f.ids.key);

		// Left steps out to the parent, then closes it.
		pressKey(f.ctx, lvgui::Key::Left);
		assert(f.tree->selectedNode() == f.ids.lights && f.tree->isExpanded(f.ids.lights));
		pressKey(f.ctx, lvgui::Key::Left);
		assert(!f.tree->isExpanded(f.ids.lights));
		pressKey(f.ctx, lvgui::Key::Left);
		assert(f.tree->selectedNode() == f.ids.scene);

		pressKey(f.ctx, lvgui::Key::End);
		assert(f.tree->selectedNode() == f.ids.camera);
		pressKey(f.ctx, lvgui::Key::Home);
		assert(f.tree->selectedNode() == f.ids.scene);
		pressKey(f.ctx, lvgui::Key::PageDown);
		assert(f.tree->selectedNode() == f.ids.camera);   // clamps at the last row

		pressKey(f.ctx, lvgui::Key::Enter);
		assert((f.activations == std::vector<int>{ f.ids.camera }));

		// Every keyboard toggle was reported.
		assert((f.toggles == std::vector<std::pair<int, bool>>{
			{ f.ids.scene, true }, { f.ids.lights, true }, { f.ids.lights, false } }));

		std::cout << "✓ testKeyboardNavigation\n";
	}

	void testKeyboardFromASelectionHiddenByTheApplication() {
		Fixture f;
		f.tree->setSelectedNode(f.ids.fill);
		f.click(f.ids.fill);   // focus it (already selected: no callback)
		assert(f.selections.empty());
		f.tree->setExpanded(f.ids.lights, false);   // the application hides the selection
		assert(f.tree->selectedNode() == f.ids.fill);

		// Down moves on from the nearest row that still shows: Lights.
		pressKey(f.ctx, lvgui::Key::Down);
		assert(f.tree->selectedNode() == f.ids.meshes);

		std::cout << "✓ testKeyboardFromASelectionHiddenByTheApplication\n";
	}

	void testTypeAheadSearchesTheShowingRows() {
		Fixture f;
		f.click(f.ids.scene);
		f.ctx.injectChar('c');
		step(f.ctx);
		assert(f.tree->selectedNode() == f.ids.camera);

		// "Floor" is hidden, so typing it finds nothing and the selection stays.
		step(f.ctx, 1.5f);
		f.ctx.injectChar('f');
		step(f.ctx);
		assert(f.tree->selectedNode() == f.ids.camera);

		f.tree->expandAll();
		step(f.ctx, 1.5f);
		f.ctx.injectChar('F');
		f.ctx.injectChar('l');
		f.ctx.injectChar('o');
		step(f.ctx);
		assert(f.tree->selectedNode() == f.ids.floor);

		std::cout << "✓ testTypeAheadSearchesTheShowingRows\n";
	}

	void testRemovingASubtreeKeepsTheOtherIds() {
		Fixture f;
		f.tree->expandAll();
		f.tree->setSelectedNode(f.ids.arm);
		f.tree->removeNode(f.ids.body);

		assert(f.tree->nodeCount() == 7);
		assert(!f.tree->isValid(f.ids.body) && !f.tree->isValid(f.ids.arm) && !f.tree->isValid(f.ids.hand));
		assert(f.tree->selectedNode() == -1);   // it was inside: cleared...
		assert(f.selections.empty());           // ...quietly
		assert((f.tree->childrenOf(f.ids.meshes) == std::vector<int>{ f.ids.floor }));
		assert(f.tree->nodeText(f.ids.floor) == "Floor");   // ids after it didn't move
		assert(f.tree->visibleNodes().size() == 7);

		// New ids carry on from the old ones rather than reusing removed slots.
		int extra = f.tree->addNode("Extra", f.ids.meshes);
		assert(extra == 10);

		f.tree->removeNode(f.ids.camera);   // a top-level node
		assert((f.tree->childrenOf(-1) == std::vector<int>{ f.ids.scene }));
		f.tree->removeNode(f.ids.camera);   // already gone: harmless
		assert(f.tree->nodeCount() == 7);
		step(f.ctx);

		std::cout << "✓ testRemovingASubtreeKeepsTheOtherIds\n";
	}

	void testScrollingALongTree() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Tree", { 20.0f, 20.0f, 360.0f, 300.0f }, lvgui::PanelFlags::None);
		auto* tree = panel->add<lvgui::TreeView>();
		tree->setVisibleRows(6);
		std::vector<int> groups;
		for (int g = 0; g < 30; ++g) {
			int group = tree->addNode("group " + std::to_string(g));
			groups.push_back(group);
			for (int c = 0; c < 3; ++c) {
				tree->addNode("item " + std::to_string(g) + "." + std::to_string(c), group);
			}
		}
		step(ctx);
		assert(tree->firstVisibleRow() == 0);
		assert(!tree->rowRect(ctx, groups[0]).empty());
		assert(tree->rowRect(ctx, groups[10]).empty());   // scrolled out of view

		ctx.injectMousePos(centre(tree->bounds()));
		ctx.injectScroll(-1.0f);
		step(ctx);
		assert(tree->firstVisibleRow() == 3);

		// reveal() opens the way to a node and scrolls it into view.
		int deep = tree->childrenOf(groups[20])[2];
		tree->reveal(deep);
		step(ctx);
		assert(tree->isExpanded(groups[20]));
		assert(!tree->rowRect(ctx, deep).empty());

		// Opening a node at the bottom of the view brings its children into view with it.
		tree->collapseAll();
		int const lastGroup = groups.back();
		tree->reveal(lastGroup);
		step(ctx);
		assert(tree->firstVisibleRow() == 24);   // 30 rows, 6 showing: the last six
		clickAt(ctx, centre(tree->toggleRect(ctx, lastGroup)));
		for (int child : tree->childrenOf(lastGroup)) {
			assert(!tree->rowRect(ctx, child).empty());
		}
		assert(!tree->rowRect(ctx, lastGroup).empty());

		std::cout << "✓ testScrollingALongTree\n";
	}

	// A widget's first update() runs before its first layout, with empty bounds. The
	// tree must not take "no room" to mean "one row fits": selecting a node straight
	// after creating the tree scrolled it to the top of the view as if it were.
	void testSelectingRightAfterCreationDoesNotScroll() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Tree", { 20.0f, 20.0f, 360.0f, 300.0f }, lvgui::PanelFlags::None);
		auto* tree = panel->add<lvgui::TreeView>();
		tree->setVisibleRows(10);
		std::vector<int> nodes;
		for (int i = 0; i < 20; ++i) {
			nodes.push_back(tree->addNode("node " + std::to_string(i)));
		}
		step(ctx);
		tree->setSelectedNode(nodes[5]);
		step(ctx);
		assert(tree->firstVisibleRow() == 0);   // row 5 of 10 was already in view

		std::cout << "✓ testSelectingRightAfterCreationDoesNotScroll\n";
	}

	void testScrollbarDragAndTrackPaging() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Tree", { 20.0f, 20.0f, 360.0f, 300.0f }, lvgui::PanelFlags::None);
		auto* tree = panel->add<lvgui::TreeView>();
		tree->setVisibleRows(5);
		for (int i = 0; i < 50; ++i) {
			tree->addNode("node " + std::to_string(i));
		}
		step(ctx);

		// The thumb starts at the top of the track, on the right-hand edge.
		lvgui::Rect const b = tree->bounds();
		lvgui::Vec2 const thumb{ b.right() - 6.0f, b.y + 10.0f };
		ctx.injectMousePos(thumb);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMousePos({ thumb.x, b.bottom() + 200.0f });   // dragging past the end
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		assert(tree->firstVisibleRow() == 45);

		// A click in the track above the thumb pages up.
		clickAt(ctx, { thumb.x, b.y + 10.0f });
		assert(tree->firstVisibleRow() == 40);
		assert(tree->selectedNode() == -1);   // scrollbar clicks never select

		std::cout << "✓ testScrollbarDragAndTrackPaging\n";
	}

	void testClickBelowTheLastRowClearsTheSelection() {
		Fixture f;
		f.click(f.ids.camera);
		pause(f.ctx);
		lvgui::Rect const b = f.tree->bounds();
		clickAt(f.ctx, { b.x + 40.0f, b.bottom() - 10.0f });
		assert(f.tree->selectedNode() == -1);
		assert((f.selections == std::vector<int>{ f.ids.camera, -1 }));

		std::cout << "✓ testClickBelowTheLastRowClearsTheSelection\n";
	}

	void testDisabledTreeIgnoresInput() {
		Fixture f;
		f.tree->setEnabled(false);
		step(f.ctx);
		clickAt(f.ctx, textOf(f.tree->rowRect(f.ctx, f.ids.camera)));
		clickAt(f.ctx, centre(f.tree->toggleRect(f.ctx, f.ids.scene)));
		assert(f.tree->selectedNode() == -1);
		assert(!f.tree->isExpanded(f.ids.scene));
		assert(f.selections.empty() && f.toggles.empty());

		std::cout << "✓ testDisabledTreeIgnoresInput\n";
	}

	void testWheelOverTheTreeDoesNotScrollItsPanel() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Tall", { 20.0f, 20.0f, 300.0f, 200.0f }, lvgui::PanelFlags::Default);
		auto* tree = panel->add<lvgui::TreeView>();
		tree->setVisibleRows(4);
		for (int i = 0; i < 20; ++i) {
			tree->addNode("node " + std::to_string(i));
		}
		for (int i = 0; i < 12; ++i) {
			panel->add<lvgui::Label>("filler " + std::to_string(i));
		}
		step(ctx);
		step(ctx);
		assert(panel->needsScrollbar());

		ctx.injectMousePos(centre(tree->bounds()));
		ctx.injectScroll(-1.0f);
		step(ctx);
		assert(panel->scrollY() == 0.0f);
		assert(tree->firstVisibleRow() == 3);

		std::cout << "✓ testWheelOverTheTreeDoesNotScrollItsPanel\n";
	}

	void testCallbacksMayRebuildTheTree() {
		// An application that rebuilds the whole tree from inside a callback must not
		// leave the widget reading freed nodes for the rest of that frame.
		Fixture f;
		lvgui::TreeView* tree = f.tree;
		tree->setOnActivate([tree](int) {
			tree->clear();
			buildScene(*tree);
		});
		tree->setOnSelectionChange([tree](int node) {
			if (node == 9) {
				tree->removeNode(9);
			}
		});
		f.click(f.ids.scene);
		f.click(f.ids.scene);   // double-click: toggles, then activates -> rebuild
		assert(tree->nodeCount() == 10);
		pause(f.ctx);
		f.click(f.ids.camera);   // selecting it removes it
		assert(!tree->isValid(f.ids.camera) && tree->selectedNode() == -1);
		pressKey(f.ctx, lvgui::Key::Down);
		pressKey(f.ctx, lvgui::Key::Enter);   // rebuilds again from the keyboard
		step(f.ctx);
		assert(tree->nodeCount() == 10);

		std::cout << "✓ testCallbacksMayRebuildTheTree\n";
	}

	void testVeryDeepTreesDoNotRecurse() {
		lvgui::TreeView tree;
		int parent = -1;
		int const depth = 20000;
		for (int i = 0; i < depth; ++i) {
			parent = tree.addNode("level " + std::to_string(i), parent);
		}
		tree.expandAll();
		assert(static_cast<int>(tree.visibleNodes().size()) == depth);
		tree.reveal(parent);
		assert(tree.depthOf(parent) == depth - 1);
		tree.setExpanded(0, false, true);
		assert(tree.visibleNodes().size() == 1);
		tree.removeNode(0);
		assert(tree.nodeCount() == 0 && tree.visibleNodes().empty());

		std::cout << "✓ testVeryDeepTreesDoNotRecurse\n";
	}

	void testDrawsEveryRowKind() {
		// Labels, details, deep indents, the selection, an empty tree's text: just
		// exercise every branch of draw() (sanitizer builds catch anything out of range).
		Fixture f;
		f.tree->setLabel("Scene");
		f.tree->setNodeDetail(f.ids.key, "spot light");
		f.tree->setNodeDetail(f.ids.hand, "a detail far too long to fit beside the name in this row");
		f.tree->expandAll();
		f.tree->setSelectedNode(f.ids.hand);
		step(f.ctx);

		lvgui::TreeView* empty = f.panel->add<lvgui::TreeView>();
		empty->setEmptyText("Nothing loaded");
		step(f.ctx);

		std::cout << "✓ testDrawsEveryRowKind\n";
	}

} // namespace

int main() {
	testBuildingTheTree();
	testOnlyExpandedBranchesShow();
	testClickingARowSelectsItAndTheArrowToggles();
	testCollapsingMovesAHiddenSelectionUp();
	testShiftClickTogglesTheWholeSubtree();
	testDoubleClickActivatesAndOpensParents();
	testKeyboardNavigation();
	testKeyboardFromASelectionHiddenByTheApplication();
	testTypeAheadSearchesTheShowingRows();
	testRemovingASubtreeKeepsTheOtherIds();
	testScrollingALongTree();
	testSelectingRightAfterCreationDoesNotScroll();
	testScrollbarDragAndTrackPaging();
	testClickBelowTheLastRowClearsTheSelection();
	testDisabledTreeIgnoresInput();
	testWheelOverTheTreeDoesNotScrollItsPanel();
	testCallbacksMayRebuildTheTree();
	testVeryDeepTreesDoNotRecurse();
	testDrawsEveryRowKind();

	std::cout << "\n✅ All TreeView tests passed!\n";
	return 0;
}
