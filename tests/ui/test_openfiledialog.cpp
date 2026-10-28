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

// OpenFileDialog -- headless, same pattern as test_savefiledialog.cpp: no Vulkan
// device, no window. Reaches the dialog's child widgets through its accessors
// (fileList(), confirmButton(), ...), and exercises real directory scanning against a
// temp directory it creates and removes itself.

#include <lightVulkanGraphics/ui/Ui.h>

#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>
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

	lvgui::Vec2 centre(const lvgui::Rect& b) {
		return { b.x + b.w * 0.5f, b.y + b.h * 0.5f };
	}

	lvgui::Vec2 centre(lvgui::Widget* w) {
		return centre(w->bounds());
	}

	// Centre of row `row` of the dialog's file list. Computed from the list's own
	// geometry (header row, then rows from the top) rather than rowRect(), so it also
	// names "where row 0 would be" in an empty list.
	lvgui::Vec2 rowCentre(const lvgui::GuiContext& ctx, lvgui::FileListView* list, int row) {
		const lvgui::Theme& th = ctx.theme();
		lvgui::Rect b = list->bounds();
		float y = b.y + th.rowHeight + th.framePadding + (static_cast<float>(row) + 0.5f) * th.rowHeight;
		return { b.x + b.w * 0.3f, y };
	}

	std::string selectedName(lvgui::FileListView* list) {
		const lvgui::FileEntry* entry = list->selectedEntry();
		return entry ? entry->name : std::string();
	}

	std::vector<std::string> listedNames(lvgui::FileListView* list) {
		std::vector<std::string> out;
		for (const lvgui::FileEntry& e : list->entries()) {
			out.push_back(e.name);
		}
		return out;
	}

	lvgui::FileListView* fileList(lvgui::OpenFileDialog& dlg) {
		return dlg.fileList();
	}
	lvgui::Button* openButton(lvgui::OpenFileDialog& dlg) {
		return dlg.confirmButton();
	}
	lvgui::Button* cancelButton(lvgui::OpenFileDialog& dlg) {
		return dlg.cancelButton();
	}

	void doubleClickAt(lvgui::GuiContext& ctx, lvgui::Vec2 pos) {
		clickAt(ctx, pos);
		clickAt(ctx, pos);
	}

	void typeAscii(lvgui::GuiContext& ctx, std::string_view ascii) {
		for (char c : ascii) {
			ctx.injectChar(static_cast<unsigned char>(c));
		}
		step(ctx);
	}

	// RAII temp directory, unique per test run via the object's own address.
	struct TempDir {
		std::filesystem::path path;
		TempDir() {
			path = std::filesystem::temp_directory_path() /
				("lvgui_openfiledialog_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
			std::filesystem::create_directories(path);
		}
		~TempDir() {
			std::error_code ec;
			std::filesystem::remove_all(path, ec);
		}
		void touch(const std::string& name) {
			std::ofstream(path / name) << "{}";
		}
		// A file `bytes` long, last written `hoursAgo` hours ago.
		void write(const std::string& name, std::size_t bytes, int hoursAgo) {
			std::ofstream(path / name, std::ios::binary) << std::string(bytes, 'x');
			std::error_code ec;
			std::filesystem::last_write_time(path / name,
				std::filesystem::file_time_type::clock::now() - std::chrono::hours(hoursAgo), ec);
		}
		void mkdir(const std::string& name) {
			std::filesystem::create_directories(path / name);
		}
	};

	void testDialogStartsHidden() {
		TempDir dir;
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".pose.json");
		step(ctx);

		assert(!dlg.isOpen());

		std::cout << "✓ testDialogStartsHidden\n";
	}

	void testOpenShowsTheDialog() {
		TempDir dir;
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".pose.json");

		dlg.open();
		step(ctx);

		assert(dlg.isOpen());

		std::cout << "✓ testOpenShowsTheDialog\n";
	}

	void testExistingFilesAreListedAlphabeticallyAndFilteredByExtension() {
		TempDir dir;
		dir.touch("b.pose.json");
		dir.touch("a.pose.json");
		dir.touch("note.txt");   // must NOT appear -- wrong extension

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".pose.json");
		dlg.open();
		step(ctx);

		auto* list = fileList(dlg);
		assert(list != nullptr);

		clickAt(ctx, rowCentre(ctx, list, 0));
		assert(selectedName(list) == "a.pose.json");

		clickAt(ctx, rowCentre(ctx, list, 1));
		assert(selectedName(list) == "b.pose.json");

		std::cout << "✓ testExistingFilesAreListedAlphabeticallyAndFilteredByExtension\n";
	}

	void testOpenFiresOnConfirmWithFullPathOfSelectedFile() {
		TempDir dir;
		dir.touch("chosen.pose.json");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".pose.json");

		std::string confirmedPath;
		bool confirmed = false;
		dlg.setOnConfirm([&](const std::string& path) { confirmed = true; confirmedPath = path; });

		dlg.open();
		step(ctx);

		clickAt(ctx, rowCentre(ctx, fileList(dlg), 0));
		clickAt(ctx, centre(openButton(dlg)));

		assert(confirmed);
		std::filesystem::path const expected = dir.path / "chosen.pose.json";
		assert(confirmedPath == expected.string());
		assert(!dlg.isOpen());

		std::cout << "✓ testOpenFiresOnConfirmWithFullPathOfSelectedFile\n";
	}

	void testOpenWithNothingSelectedDoesNotConfirmOrClose() {
		TempDir dir;
		dir.touch("only.pose.json");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".pose.json");

		bool confirmed = false;
		dlg.setOnConfirm([&](const std::string&) { confirmed = true; });

		dlg.open();
		step(ctx);
		clickAt(ctx, centre(openButton(dlg)));

		assert(!confirmed);
		assert(dlg.isOpen());

		std::cout << "✓ testOpenWithNothingSelectedDoesNotConfirmOrClose\n";
	}

	void testCancelClosesAndFiresOnCancelNotOnConfirm() {
		TempDir dir;
		dir.touch("a.pose.json");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".pose.json");

		bool confirmed = false, cancelled = false;
		dlg.setOnConfirm([&](const std::string&) { confirmed = true; });
		dlg.setOnCancel([&] { cancelled = true; });

		dlg.open();
		step(ctx);
		clickAt(ctx, rowCentre(ctx, fileList(dlg), 0));
		clickAt(ctx, centre(cancelButton(dlg)));

		assert(!confirmed);
		assert(cancelled);
		assert(!dlg.isOpen());

		std::cout << "✓ testCancelClosesAndFiresOnCancelNotOnConfirm\n";
	}

	void testEscapeAlsoFiresOnCancel() {
		TempDir dir;
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".pose.json");

		bool cancelled = false;
		dlg.setOnCancel([&] { cancelled = true; });

		dlg.open();
		step(ctx);
		pressKey(ctx, lvgui::Key::Escape);

		assert(cancelled);
		assert(!dlg.isOpen());

		std::cout << "✓ testEscapeAlsoFiresOnCancel\n";
	}

	void testReopenRefreshesFileListingAndClearsSelection() {
		TempDir dir;
		dir.touch("first.pose.json");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".pose.json");

		dlg.open();
		step(ctx);
		clickAt(ctx, rowCentre(ctx, fileList(dlg), 0));
		assert(selectedName(fileList(dlg)) == "first.pose.json");

		dir.touch("second.pose.json");
		dlg.open();   // re-scan; selection must reset, not carry over as a stale index
		step(ctx);
		assert(fileList(dlg)->selectedIndex() < 0);
		clickAt(ctx, rowCentre(ctx, fileList(dlg), 0));
		assert(selectedName(fileList(dlg)) == "first.pose.json");   // still alphabetically first
		clickAt(ctx, rowCentre(ctx, fileList(dlg), 1));
		assert(selectedName(fileList(dlg)) == "second.pose.json");

		std::cout << "✓ testReopenRefreshesFileListingAndClearsSelection\n";
	}


	void testFoldersAreListedFirstAndDoubleClickEntersThem() {
		TempDir dir;
		dir.touch("a.csv");
		dir.mkdir("zz_data");
		dir.touch("zz_data/inside.csv");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".csv");
		dlg.open();
		step(ctx);

		assert((listedNames(fileList(dlg)) == std::vector<std::string>{ "zz_data", "a.csv" }));

		doubleClickAt(ctx, rowCentre(ctx, fileList(dlg), 0));
		assert(dlg.isOpen());
		assert(dlg.directory() == (dir.path / "zz_data").string());
		assert((listedNames(fileList(dlg)) == std::vector<std::string>{ "inside.csv" }));
		assert(std::string(dlg.pathField()->text()) == dlg.directory());

		std::cout << "✓ testFoldersAreListedFirstAndDoubleClickEntersThem\n";
	}

	void testUpButtonAndBackspaceReturnToTheParentSelectingTheFolderLeft() {
		TempDir dir;
		dir.mkdir("one");
		dir.mkdir("two");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, (dir.path / "two").string(), ".csv");
		dlg.open();
		step(ctx);

		clickAt(ctx, centre(dlg.upButton()));
		assert(dlg.directory() == dir.path.string());
		assert(selectedName(fileList(dlg)) == "two");

		doubleClickAt(ctx, rowCentre(ctx, fileList(dlg), 0));   // into "one"
		assert(dlg.directory() == (dir.path / "one").string());
		clickAt(ctx, rowCentre(ctx, fileList(dlg), 2));         // focus the (empty) list
		pressKey(ctx, lvgui::Key::Backspace);
		assert(dlg.directory() == dir.path.string());
		assert(selectedName(fileList(dlg)) == "one");

		std::cout << "✓ testUpButtonAndBackspaceReturnToTheParentSelectingTheFolderLeft\n";
	}

	void testOpenButtonOnAFolderEntersItInsteadOfConfirming() {
		TempDir dir;
		dir.mkdir("sub");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".csv");
		bool confirmed = false;
		dlg.setOnConfirm([&](const std::string&) { confirmed = true; });
		dlg.open();
		step(ctx);

		clickAt(ctx, rowCentre(ctx, fileList(dlg), 0));
		clickAt(ctx, centre(openButton(dlg)));
		assert(!confirmed);
		assert(dlg.isOpen());
		assert(dlg.directory() == (dir.path / "sub").string());

		std::cout << "✓ testOpenButtonOnAFolderEntersItInsteadOfConfirming\n";
	}

	void testDoubleClickOrEnterOnAFileConfirmsIt() {
		TempDir dir;
		dir.touch("a.csv");
		dir.touch("b.csv");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".csv");
		std::string confirmedPath;
		dlg.setOnConfirm([&](const std::string& path) { confirmedPath = path; });

		dlg.open();
		step(ctx);
		doubleClickAt(ctx, rowCentre(ctx, fileList(dlg), 1));
		assert(confirmedPath == (dir.path / "b.csv").string());
		assert(!dlg.isOpen());

		// The list has focus as soon as the dialog opens: arrow down, Enter.
		confirmedPath.clear();
		dlg.open();
		step(ctx);
		pressKey(ctx, lvgui::Key::Down);
		pressKey(ctx, lvgui::Key::Enter);
		assert(confirmedPath == (dir.path / "a.csv").string());

		std::cout << "✓ testDoubleClickOrEnterOnAFileConfirmsIt\n";
	}

	void testModifiedAndSizeHeadersSortAndTheOrderIsRememberedAcrossOpen() {
		TempDir dir;
		dir.write("old_big.csv", 9000, 48);
		dir.write("new_small.csv", 10, 1);
		dir.write("mid.csv", 500, 24);

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".csv");
		dlg.open();
		step(ctx);
		auto* list = fileList(dlg);

		clickAt(ctx, centre(list->headerCellRect(ctx, lvgui::FileSortKey::Modified)));
		assert((listedNames(list) == std::vector<std::string>{ "new_small.csv", "mid.csv", "old_big.csv" }));
		clickAt(ctx, centre(list->headerCellRect(ctx, lvgui::FileSortKey::Modified)));
		assert((listedNames(list) == std::vector<std::string>{ "old_big.csv", "mid.csv", "new_small.csv" }));

		clickAt(ctx, centre(list->headerCellRect(ctx, lvgui::FileSortKey::Size)));
		assert((listedNames(list) == std::vector<std::string>{ "old_big.csv", "mid.csv", "new_small.csv" }));
		assert(list->entries()[0].size == 9000);

		clickAt(ctx, centre(cancelButton(dlg)));
		dlg.open();
		step(ctx);
		assert(list->sortKey() == lvgui::FileSortKey::Size && !list->sortAscending());
		assert(listedNames(list).front() == "old_big.csv");

		std::cout << "✓ testModifiedAndSizeHeadersSortAndTheOrderIsRememberedAcrossOpen\n";
	}

	void testTheFolderIsRememberedAcrossOpen() {
		TempDir dir;
		dir.mkdir("deeper");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".csv");
		dlg.open();
		step(ctx);
		doubleClickAt(ctx, rowCentre(ctx, fileList(dlg), 0));
		pressKey(ctx, lvgui::Key::Escape);
		assert(!dlg.isOpen());

		dlg.open();
		step(ctx);
		assert(dlg.directory() == (dir.path / "deeper").string());

		dlg.setDirectory(dir.path.string());   // the application can always reset it
		assert(dlg.directory() == dir.path.string());

		std::cout << "✓ testTheFolderIsRememberedAcrossOpen\n";
	}

	void testSearchNarrowsTheListCaseInsensitivelyAndIsClearedOnReopen() {
		TempDir dir;
		dir.touch("Alpha_run.csv");
		dir.touch("beta_run.csv");
		dir.touch("alphabet.csv");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".csv");
		dlg.open();
		step(ctx);

		clickAt(ctx, centre(dlg.searchField()));
		typeAscii(ctx, "ALPHA");
		step(ctx);
		assert((listedNames(fileList(dlg)) == std::vector<std::string>{ "Alpha_run.csv", "alphabet.csv" }));

		typeAscii(ctx, "_");
		step(ctx);
		assert((listedNames(fileList(dlg)) == std::vector<std::string>{ "Alpha_run.csv" }));

		pressKey(ctx, lvgui::Key::Escape);
		dlg.open();
		step(ctx);
		assert(dlg.searchField()->text().empty());
		assert(fileList(dlg)->entries().size() == 3);

		std::cout << "✓ testSearchNarrowsTheListCaseInsensitivelyAndIsClearedOnReopen\n";
	}

	void testFileTypeFiltersSwitchWhatIsListed() {
		TempDir dir;
		dir.touch("a.csv");
		dir.touch("b.JSON");   // extensions match case-insensitively
		dir.touch("c.png");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".csv");
		dlg.open();
		step(ctx);
		assert((listedNames(fileList(dlg)) == std::vector<std::string>{ "a.csv" }));
		assert(!dlg.fileTypeDropDown()->enabled());   // one filter: nothing to choose
		assert(std::string(dlg.statusLabel()->text()).find("2 files of other types not shown") != std::string::npos);

		dlg.setFileTypeFilters({
			{ "Data (*.csv, *.json)", { ".csv", ".json" } },
			{ "All files", {} },
		});
		step(ctx);
		assert(dlg.fileTypeDropDown()->enabled());
		assert((listedNames(fileList(dlg)) == std::vector<std::string>{ "a.csv", "b.JSON" }));

		dlg.setFileTypeFilterIndex(1);
		step(ctx);
		assert((listedNames(fileList(dlg)) == std::vector<std::string>{ "a.csv", "b.JSON", "c.png" }));
		assert(std::string(dlg.statusLabel()->text()) == "3 files");

		std::cout << "✓ testFileTypeFiltersSwitchWhatIsListed\n";
	}

	void testHiddenFilesOnlyAppearWithTheToggle() {
		TempDir dir;
		dir.touch(".secret.csv");
		dir.touch("plain.csv");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".csv");
		dlg.open();
		step(ctx);
		assert((listedNames(fileList(dlg)) == std::vector<std::string>{ "plain.csv" }));

		clickAt(ctx, centre(dlg.hiddenFilesToggle()));
		assert(dlg.showHiddenFiles());
		assert(fileList(dlg)->entries().size() == 2);

		std::cout << "✓ testHiddenFilesOnlyAppearWithTheToggle\n";
	}

	void testWithNavigationDisabledFoldersAreHiddenAndUpDoesNothing() {
		TempDir dir;
		dir.mkdir("sub");
		dir.touch("a.csv");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".csv");
		dlg.setNavigationEnabled(false);
		dlg.open();
		step(ctx);

		assert((listedNames(fileList(dlg)) == std::vector<std::string>{ "a.csv" }));
		assert(!dlg.upButton()->enabled());
		dlg.navigateUp();
		clickAt(ctx, rowCentre(ctx, fileList(dlg), 0));
		pressKey(ctx, lvgui::Key::Backspace);
		assert(dlg.directory() == dir.path.string());

		std::cout << "✓ testWithNavigationDisabledFoldersAreHiddenAndUpDoesNothing\n";
	}

	void testTypingAPathGoesThereAndABadPathSaysSo() {
		TempDir dir;
		dir.mkdir("target");
		dir.touch("target/found.csv");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".csv");
		dlg.open();
		step(ctx);

		// Relative to the folder being shown.
		dlg.pathField()->setText("target", false);
		clickAt(ctx, centre(dlg.pathField()));
		pressKey(ctx, lvgui::Key::Enter);
		assert(dlg.directory() == (dir.path / "target").string());
		assert((listedNames(fileList(dlg)) == std::vector<std::string>{ "found.csv" }));

		dlg.pathField()->setText((dir.path / "does_not_exist").string(), false);
		clickAt(ctx, centre(dlg.pathField()));
		pressKey(ctx, lvgui::Key::Enter);
		assert(dlg.directory() == (dir.path / "target").string());   // unchanged
		assert(std::string(dlg.pathField()->text()) == dlg.directory());
		assert(std::string(dlg.statusLabel()->text()).rfind("Folder not found", 0) == 0);

		std::cout << "✓ testTypingAPathGoesThereAndABadPathSaysSo\n";
	}

	void testStatusLineCountsFilesAndFolders() {
		TempDir dir;
		dir.mkdir("f1");
		dir.touch("a.csv");
		dir.touch("b.csv");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".csv");
		dlg.open();
		step(ctx);
		assert(std::string(dlg.statusLabel()->text()) == "2 files, 1 folder");

		std::cout << "✓ testStatusLineCountsFilesAndFolders\n";
	}


	// The path field must never name a folder other than the one listed: an edit that
	// is abandoned by clicking elsewhere goes back to the real folder.
	void testAnAbandonedPathEditRevertsToTheCurrentFolder() {
		TempDir dir;
		dir.mkdir("elsewhere");
		dir.touch("a.csv");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFileDialog dlg(ctx, dir.path.string(), ".csv");
		dlg.open();
		step(ctx);

		clickAt(ctx, centre(dlg.pathField()));
		pressKey(ctx, lvgui::Key::End);
		typeAscii(ctx, "/elsewhere");
		assert(std::string(dlg.pathField()->text()) != dlg.directory());
		clickAt(ctx, rowCentre(ctx, fileList(dlg), 1));   // focus leaves the field

		assert(dlg.directory() == dir.path.string());
		assert(std::string(dlg.pathField()->text()) == dlg.directory());

		// With navigation off the field is read-only and Enter in it reports nothing.
		dlg.setNavigationEnabled(false);
		clickAt(ctx, centre(dlg.pathField()));
		pressKey(ctx, lvgui::Key::Enter);
		assert(dlg.directory() == dir.path.string());
		assert(std::string(dlg.statusLabel()->text()).find("not found") == std::string::npos);

		std::cout << "✓ testAnAbandonedPathEditRevertsToTheCurrentFolder\n";
	}

}

int main() {
	testDialogStartsHidden();
	testOpenShowsTheDialog();
	testExistingFilesAreListedAlphabeticallyAndFilteredByExtension();
	testOpenFiresOnConfirmWithFullPathOfSelectedFile();
	testOpenWithNothingSelectedDoesNotConfirmOrClose();
	testCancelClosesAndFiresOnCancelNotOnConfirm();
	testEscapeAlsoFiresOnCancel();
	testReopenRefreshesFileListingAndClearsSelection();
	testFoldersAreListedFirstAndDoubleClickEntersThem();
	testUpButtonAndBackspaceReturnToTheParentSelectingTheFolderLeft();
	testOpenButtonOnAFolderEntersItInsteadOfConfirming();
	testDoubleClickOrEnterOnAFileConfirmsIt();
	testModifiedAndSizeHeadersSortAndTheOrderIsRememberedAcrossOpen();
	testTheFolderIsRememberedAcrossOpen();
	testSearchNarrowsTheListCaseInsensitivelyAndIsClearedOnReopen();
	testFileTypeFiltersSwitchWhatIsListed();
	testHiddenFilesOnlyAppearWithTheToggle();
	testWithNavigationDisabledFoldersAreHiddenAndUpDoesNothing();
	testTypingAPathGoesThereAndABadPathSaysSo();
	testStatusLineCountsFilesAndFolders();
	testAnAbandonedPathEditRevertsToTheCurrentFolder();

	std::cout << "\n✅ All OpenFileDialog tests passed!\n";
	return 0;
}
