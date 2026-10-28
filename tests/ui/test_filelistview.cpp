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

// docs/gui/05-widgets.md, "FileListView" -- headless, same pattern as test_listbox.cpp:
// no Vulkan device, no window, no filesystem. Entries are made up in memory, which is
// the point of the widget never scanning a directory itself.

#include <lightVulkanGraphics/ui/Ui.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
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

	void pressKey(lvgui::GuiContext& ctx, int key, int mods = 0) {
		ctx.injectKey(key, mods, true, false);
		step(ctx);
		ctx.injectKey(key, mods, false, false);
		step(ctx);
	}

	lvgui::Vec2 centre(const lvgui::Rect& r) {
		return { r.x + r.w * 0.5f, r.y + r.h * 0.5f };
	}

	lvgui::FileEntry file(std::string name, std::uint64_t size, std::int64_t modified) {
		lvgui::FileEntry e;
		e.name = std::move(name);
		e.size = size;
		e.modifiedTime = modified;
		return e;
	}

	lvgui::FileEntry folder(std::string name, std::int64_t modified = 1'700'000'000) {
		lvgui::FileEntry e;
		e.name = std::move(name);
		e.isDirectory = true;
		e.modifiedTime = modified;
		return e;
	}

	std::vector<std::string> names(const lvgui::FileListView& list) {
		std::vector<std::string> out;
		for (const lvgui::FileEntry& e : list.entries()) {
			out.push_back(e.name);
		}
		return out;
	}

	std::string selectedName(const lvgui::FileListView& list) {
		const lvgui::FileEntry* e = list.selectedEntry();
		return e ? e->name : std::string();
	}

	// A panel just big enough for the list's default 8 rows, holding only the list.
	struct Fixture {
		lvgui::GuiContext ctx{ testCreateInfo(), lvgui::PlatformHooks{} };
		lvgui::Panel* panel = nullptr;
		lvgui::FileListView* list = nullptr;

		Fixture() {
			panel = ctx.createPanel("Files", { 20.0f, 20.0f, 600.0f, 300.0f }, lvgui::PanelFlags::None);
			list = panel->add<lvgui::FileListView>();
			list->setEntries({
				file("frame_10.csv", 300, 1'700'000'300),
				folder("zeta"),
				file("Frame_2.csv", 100, 1'700'000'100),
				file("apple.txt", 5'000, 1'700'000'900),
				folder("Alpha"),
				file("banana.txt", 100, 1'700'000'500),
			});
			step(ctx);
		}
	};

	void testNaturalCompare() {
		using V = lvgui::FileListView;
		assert(V::compareNatural("frame_2", "frame_10") < 0);
		assert(V::compareNatural("frame_10", "frame_2") > 0);
		assert(V::compareNatural("apple", "Banana") < 0);      // case-insensitive
		assert(V::compareNatural("Apple", "banana") < 0);
		assert(V::compareNatural("f01", "f1") != 0);           // still a strict order...
		assert(V::compareNatural("f01", "f2") < 0);            // ...by numeric value
		assert(V::compareNatural("a.txt", "A.txt") != 0);
		assert(V::compareNatural("a.txt", "A.txt") == -V::compareNatural("A.txt", "a.txt"));
		assert(V::compareNatural("same", "same") == 0);
		assert(V::compareNatural("abc", "abcd") < 0);
		assert(V::compareNatural("v1.9", "v1.10") < 0);

		std::cout << "✓ testNaturalCompare\n";
	}

	void testFormatSize() {
		using V = lvgui::FileListView;
		assert(V::formatSize(0) == "0 B");
		assert(V::formatSize(512) == "512 B");
		assert(V::formatSize(1023) == "1023 B");
		assert(V::formatSize(1536) == "1.5 KB");
		assert(V::formatSize(20 * 1024) == "20 KB");
		assert(V::formatSize(3'355'443) == "3.2 MB");
		assert(V::formatSize(5ull * 1024 * 1024 * 1024) == "5.0 GB");

		std::cout << "✓ testFormatSize\n";
	}

	void testFormatModifiedTime() {
		using V = lvgui::FileListView;
		assert(V::formatModifiedTime(0).empty());
		// Local time, so only the shape can be pinned down here: "YYYY-MM-DD HH:MM".
		std::string const s = V::formatModifiedTime(1'700'000'000);
		assert(s.size() == 16);
		assert(s[4] == '-' && s[7] == '-' && s[10] == ' ' && s[13] == ':');
		assert(s.compare(0, 3, "202") == 0);

		std::cout << "✓ testFormatModifiedTime\n";
	}

	void testDefaultSortIsFoldersFirstThenNaturalNames() {
		Fixture f;
		std::vector<std::string> const expected = {
			"Alpha", "zeta", "apple.txt", "banana.txt", "Frame_2.csv", "frame_10.csv"
		};
		assert(names(*f.list) == expected);
		assert(f.list->sortKey() == lvgui::FileSortKey::Name);
		assert(f.list->sortAscending());

		std::cout << "✓ testDefaultSortIsFoldersFirstThenNaturalNames\n";
	}

	void testSortBySizeAndModifiedKeepsFoldersFirstAndBreaksTiesByName() {
		Fixture f;

		f.list->setSort(lvgui::FileSortKey::Size, false);
		std::vector<std::string> const bySizeDesc = {
			"Alpha", "zeta", "apple.txt", "frame_10.csv", "banana.txt", "Frame_2.csv"   // 100 B tie: A to Z
		};
		assert(names(*f.list) == bySizeDesc);

		f.list->setSort(lvgui::FileSortKey::Size, true);
		std::vector<std::string> const bySizeAsc = {
			"Alpha", "zeta", "banana.txt", "Frame_2.csv", "frame_10.csv", "apple.txt"   // tie still A to Z
		};
		assert(names(*f.list) == bySizeAsc);

		f.list->setSort(lvgui::FileSortKey::Modified, false);
		std::vector<std::string> const newestFirst = {
			"Alpha", "zeta", "apple.txt", "banana.txt", "frame_10.csv", "Frame_2.csv"
		};
		assert(names(*f.list) == newestFirst);

		std::cout << "✓ testSortBySizeAndModifiedKeepsFoldersFirstAndBreaksTiesByName\n";
	}

	void testHeaderClicksSortAndASecondClickReverses() {
		Fixture f;
		int sortChanges = 0;
		lvgui::FileSortKey lastKey = lvgui::FileSortKey::Name;
		bool lastAscending = true;
		f.list->setOnSortChange([&](lvgui::FileSortKey key, bool ascending) {
			++sortChanges;
			lastKey = key;
			lastAscending = ascending;
		});

		// Modified starts newest first...
		clickAt(f.ctx, centre(f.list->headerCellRect(f.ctx, lvgui::FileSortKey::Modified)));
		assert(f.list->sortKey() == lvgui::FileSortKey::Modified);
		assert(!f.list->sortAscending());
		assert(sortChanges == 1 && lastKey == lvgui::FileSortKey::Modified && !lastAscending);
		assert(f.list->entries()[2].name == "apple.txt");

		// ...and a second click on the same header flips it.
		clickAt(f.ctx, centre(f.list->headerCellRect(f.ctx, lvgui::FileSortKey::Modified)));
		assert(f.list->sortAscending());
		assert(sortChanges == 2 && lastAscending);
		assert(f.list->entries()[2].name == "Frame_2.csv");

		// Name starts A to Z.
		clickAt(f.ctx, centre(f.list->headerCellRect(f.ctx, lvgui::FileSortKey::Name)));
		assert(f.list->sortKey() == lvgui::FileSortKey::Name && f.list->sortAscending());

		std::cout << "✓ testHeaderClicksSortAndASecondClickReverses\n";
	}

	void testPressOnAHeaderThenDragOffDoesNotSort() {
		Fixture f;
		f.ctx.injectMousePos(centre(f.list->headerCellRect(f.ctx, lvgui::FileSortKey::Size)));
		f.ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(f.ctx);
		f.ctx.injectMousePos(centre(f.list->rowRect(f.ctx, 3)));
		f.ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(f.ctx);

		assert(f.list->sortKey() == lvgui::FileSortKey::Name);

		std::cout << "✓ testPressOnAHeaderThenDragOffDoesNotSort\n";
	}

	void testResortingKeepsTheSameEntrySelected() {
		Fixture f;
		assert(f.list->selectName("banana.txt"));
		f.list->setSort(lvgui::FileSortKey::Size, false);
		assert(selectedName(*f.list) == "banana.txt");
		assert(f.list->selectedIndex() == 4);

		std::cout << "✓ testResortingKeepsTheSameEntrySelected\n";
	}

	void testClickSelectsAndDoubleClickActivates() {
		Fixture f;
		int selectionChanges = 0;
		std::string activated;
		f.list->setOnSelectionChange([&](const lvgui::FileEntry*) { ++selectionChanges; });
		f.list->setOnActivate([&](const lvgui::FileEntry& e) { activated = e.name; });

		clickAt(f.ctx, centre(f.list->rowRect(f.ctx, 2)));
		assert(selectedName(*f.list) == "apple.txt");
		assert(selectionChanges == 1);
		assert(activated.empty());

		clickAt(f.ctx, centre(f.list->rowRect(f.ctx, 2)));
		assert(activated == "apple.txt");
		assert(selectionChanges == 1);   // same row: no second change

		std::cout << "✓ testClickSelectsAndDoubleClickActivates\n";
	}

	void testSlowSecondClickOrClickOnAnotherRowDoesNotActivate() {
		Fixture f;
		int activations = 0;
		f.list->setOnActivate([&](const lvgui::FileEntry&) { ++activations; });

		clickAt(f.ctx, centre(f.list->rowRect(f.ctx, 2)));
		step(f.ctx, 0.5f);   // longer than the double-click window
		clickAt(f.ctx, centre(f.list->rowRect(f.ctx, 2)));
		assert(activations == 0);

		clickAt(f.ctx, centre(f.list->rowRect(f.ctx, 3)));   // quick, but a different row
		assert(activations == 0);
		assert(selectedName(*f.list) == "banana.txt");

		std::cout << "✓ testSlowSecondClickOrClickOnAnotherRowDoesNotActivate\n";
	}

	void testClickBelowTheLastRowClearsTheSelection() {
		Fixture f;
		clickAt(f.ctx, centre(f.list->rowRect(f.ctx, 0)));
		assert(f.list->selectedIndex() == 0);

		lvgui::Rect const last = f.list->rowRect(f.ctx, 5);
		clickAt(f.ctx, { last.x + last.w * 0.5f, last.bottom() + 30.0f });
		assert(f.list->selectedIndex() < 0);

		std::cout << "✓ testClickBelowTheLastRowClearsTheSelection\n";
	}

	void testKeyboardMovesClampsActivatesAndNavigatesUp() {
		Fixture f;
		std::string activated;
		int upRequests = 0;
		f.list->setOnActivate([&](const lvgui::FileEntry& e) { activated = e.name; });
		f.list->setOnNavigateUp([&] { ++upRequests; });

		clickAt(f.ctx, centre(f.list->rowRect(f.ctx, 0)));   // focus + select first
		pressKey(f.ctx, lvgui::Key::Up);                       // no wrap at the top
		assert(f.list->selectedIndex() == 0);
		pressKey(f.ctx, lvgui::Key::Down);
		pressKey(f.ctx, lvgui::Key::Down);
		assert(selectedName(*f.list) == "apple.txt");
		pressKey(f.ctx, lvgui::Key::End);
		assert(f.list->selectedIndex() == 5);
		pressKey(f.ctx, lvgui::Key::Down);                     // no wrap at the bottom
		assert(f.list->selectedIndex() == 5);
		pressKey(f.ctx, lvgui::Key::Home);
		assert(f.list->selectedIndex() == 0);
		pressKey(f.ctx, lvgui::Key::PageDown);
		assert(f.list->selectedIndex() == 5);

		pressKey(f.ctx, lvgui::Key::Enter);
		assert(activated == "frame_10.csv");
		pressKey(f.ctx, lvgui::Key::Backspace);
		assert(upRequests == 1);

		std::cout << "✓ testKeyboardMovesClampsActivatesAndNavigatesUp\n";
	}

	void testTypeAheadJumpsToTheFirstNameWithThatPrefix() {
		Fixture f;
		clickAt(f.ctx, centre(f.list->rowRect(f.ctx, 0)));   // focus

		f.ctx.injectChar('b');
		step(f.ctx);
		assert(selectedName(*f.list) == "banana.txt");

		f.ctx.injectChar('F');   // case-insensitive, and "bf" matches nothing...
		step(f.ctx);
		assert(selectedName(*f.list) == "banana.txt");   // ...so the selection stays put

		step(f.ctx, 1.5f);       // pause: the buffer starts over
		f.ctx.injectChar('F');
		f.ctx.injectChar('R');
		step(f.ctx);
		assert(selectedName(*f.list) == "Frame_2.csv");

		std::cout << "✓ testTypeAheadJumpsToTheFirstNameWithThatPrefix\n";
	}

	void testWheelAndScrollbarDragScrollALongList() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Files", { 20.0f, 20.0f, 600.0f, 300.0f }, lvgui::PanelFlags::None);
		auto* list = panel->add<lvgui::FileListView>();
		std::vector<lvgui::FileEntry> many;
		for (int i = 0; i < 100; ++i) {
			many.push_back(file("file_" + std::to_string(i) + ".dat", 10, 1'700'000'000));
		}
		list->setEntries(std::move(many));
		step(ctx);
		assert(list->firstVisibleRow() == 0);

		ctx.injectMousePos(centre(list->rowRect(ctx, 2)));
		ctx.injectScroll(-1.0f);   // towards the user: down
		step(ctx);
		assert(list->firstVisibleRow() == 3);

		// Selecting a row out of view scrolls it into view.
		list->setSelectedIndex(80);
		step(ctx);
		assert(list->firstVisibleRow() <= 80 && 80 < list->firstVisibleRow() + 8);

		// Drag the thumb all the way up: back to the top. The thumb sits in a track
		// below the header row, at the current scroll fraction of its travel.
		lvgui::Rect const b = list->bounds();
		const lvgui::Theme& th = ctx.theme();
		float const trackX = b.right() - th.scrollbarWidth * 0.5f - 1.0f;
		float const trackTop = b.y + th.rowHeight + th.framePadding;
		float const trackH = b.h - th.rowHeight - 2.0f * th.framePadding;
		float const fraction = static_cast<float>(list->firstVisibleRow()) / static_cast<float>(100 - 8);
		float const thumbH = std::max(trackH * (8.0f / 100.0f), 16.0f);
		float const thumbY = trackTop + fraction * (trackH - thumbH) + thumbH * 0.5f;

		ctx.injectMousePos({ trackX, thumbY });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMousePos({ trackX, b.y - 200.0f });   // far past the top: clamps
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		assert(list->firstVisibleRow() == 0);

		std::cout << "✓ testWheelAndScrollbarDragScrollALongList\n";
	}

	void testFillPanelGrowsWithThePanel() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Files", { 20.0f, 20.0f, 500.0f, 300.0f }, lvgui::PanelFlags::Default);
		panel->add<lvgui::Label>("above");
		auto* list = panel->add<lvgui::FileListView>();
		panel->add<lvgui::Button>("below");
		list->setVisibleRows(3);
		step(ctx);
		float const fixedH = list->bounds().h;

		list->setFillPanel(true);
		step(ctx);
		float const filledH = list->bounds().h;
		assert(filledH > fixedH);
		assert(!panel->needsScrollbar());   // fills the panel, never overflows it

		panel->setBounds({ 20.0f, 20.0f, 500.0f, 500.0f });
		step(ctx);
		assert(list->bounds().h > filledH + 150.0f);
		assert(!panel->needsScrollbar());

		// Never below its minimum rows, however small the panel gets.
		panel->setBounds({ 20.0f, 20.0f, 500.0f, 60.0f });
		step(ctx);
		assert(list->bounds().h >= fixedH - 0.5f);

		std::cout << "✓ testFillPanelGrowsWithThePanel\n";
	}

	void testSetEntriesClearsSelectionAndEmptyListIsSafe() {
		Fixture f;
		f.list->setSelectedIndex(3);
		f.list->setEntries({});
		assert(f.list->selectedIndex() < 0);
		assert(f.list->selectedEntry() == nullptr);
		step(f.ctx);
		pressKey(f.ctx, lvgui::Key::Down);
		clickAt(f.ctx, centre(f.list->bounds()));
		assert(f.list->selectedIndex() < 0);
		assert(!f.list->selectName("anything"));

		std::cout << "✓ testSetEntriesClearsSelectionAndEmptyListIsSafe\n";
	}


	// Each fill list used to ask the other for its preferredSize(), which asked the first
	// for ITS preferredSize() in turn: a stack overflow as soon as the panel laid out.
	void testTwoFillListsInOnePanelShareTheSpareHeight() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Files", { 20.0f, 20.0f, 500.0f, 500.0f }, lvgui::PanelFlags::Default);
		auto* top = panel->add<lvgui::FileListView>();
		auto* bottom = panel->add<lvgui::FileListView>();
		top->setVisibleRows(2);
		bottom->setVisibleRows(2);
		top->setFillPanel(true);
		bottom->setFillPanel(true);
		step(ctx);

		assert(std::abs(top->bounds().h - bottom->bounds().h) <= 1.0f);
		assert(top->bounds().h > 150.0f);
		assert(!panel->needsScrollbar());

		std::cout << "✓ testTwoFillListsInOnePanelShareTheSpareHeight\n";
	}

	// The double-click window remembers a row index; re-sorting in between makes that
	// index a different file, which must not be "double-clicked" by the second click.
	void testReSortingBetweenTwoClicksDoesNotActivate() {
		Fixture f;
		int activations = 0;
		f.list->setOnActivate([&](const lvgui::FileEntry&) { ++activations; });

		clickAt(f.ctx, centre(f.list->rowRect(f.ctx, 2)));   // apple.txt
		f.list->setSort(lvgui::FileSortKey::Size, true);       // row 2 is now banana.txt
		clickAt(f.ctx, centre(f.list->rowRect(f.ctx, 2)));
		assert(activations == 0);
		assert(selectedName(*f.list) == "banana.txt");

		std::cout << "✓ testReSortingBetweenTwoClicksDoesNotActivate\n";
	}

}

int main() {
	testNaturalCompare();
	testFormatSize();
	testFormatModifiedTime();
	testDefaultSortIsFoldersFirstThenNaturalNames();
	testSortBySizeAndModifiedKeepsFoldersFirstAndBreaksTiesByName();
	testHeaderClicksSortAndASecondClickReverses();
	testPressOnAHeaderThenDragOffDoesNotSort();
	testResortingKeepsTheSameEntrySelected();
	testClickSelectsAndDoubleClickActivates();
	testSlowSecondClickOrClickOnAnotherRowDoesNotActivate();
	testClickBelowTheLastRowClearsTheSelection();
	testKeyboardMovesClampsActivatesAndNavigatesUp();
	testTypeAheadJumpsToTheFirstNameWithThatPrefix();
	testWheelAndScrollbarDragScrollALongList();
	testFillPanelGrowsWithThePanel();
	testSetEntriesClearsSelectionAndEmptyListIsSafe();
	testTwoFillListsInOnePanelShareTheSpareHeight();
	testReSortingBetweenTwoClicksDoesNotActivate();

	std::cout << "\n✅ All FileListView tests passed!\n";
	return 0;
}
