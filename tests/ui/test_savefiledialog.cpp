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

// SaveFileDialog -- headless, same pattern as test_modal.cpp/test_listbox.cpp: no
// Vulkan device, no window. Reaches the dialog's child widgets via panel()->widgetAt()
// in the fixed order the constructor adds them: Label, ListBox, TextBox, Label
// (status), Button (Save), Button (Cancel).

#include <lightVulkanGraphics/ui/Ui.h>

#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>

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

	void typeAscii(lvgui::GuiContext& ctx, std::string_view ascii) {
		for (char c : ascii) {
			ctx.injectChar(static_cast<unsigned char>(c));
		}
		step(ctx);
	}

	void pressKey(lvgui::GuiContext& ctx, int key, int mods = 0) {
		ctx.injectKey(key, mods, true, false);
		step(ctx);
		ctx.injectKey(key, mods, false, false);
		step(ctx);
	}

	lvgui::Vec2 centre(lvgui::Widget* w) {
		lvgui::Rect b = w->bounds();
		return { b.x + b.w * 0.5f, b.y + b.h * 0.5f };
	}

	lvgui::Vec2 rowCentre(const lvgui::GuiContext& ctx, lvgui::ListBox* lb, int row) {
		const lvgui::Theme& th = ctx.theme();
		lvgui::Rect b = lb->bounds();
		float y = b.y + th.framePadding + (static_cast<float>(row) + 0.5f) * th.rowHeight;
		return { b.x + b.w * 0.5f, y };
	}

	// Fixed constructor order in SaveFileDialog.cpp: Label, ListBox, TextBox, Label
	// (status), Button (Save), Button (Cancel).
	lvgui::ListBox* fileList(lvgui::SaveFileDialog& dlg) {
		return dynamic_cast<lvgui::ListBox*>(dlg.panel()->widgetAt(1));
	}
	lvgui::TextBox* filenameField(lvgui::SaveFileDialog& dlg) {
		return dynamic_cast<lvgui::TextBox*>(dlg.panel()->widgetAt(2));
	}
	lvgui::Button* saveButton(lvgui::SaveFileDialog& dlg) {
		return dynamic_cast<lvgui::Button*>(dlg.panel()->widgetAt(4));
	}
	lvgui::Button* cancelButton(lvgui::SaveFileDialog& dlg) {
		return dynamic_cast<lvgui::Button*>(dlg.panel()->widgetAt(5));
	}

	// RAII temp directory, unique per test run via the object's own address.
	struct TempDir {
		std::filesystem::path path;
		TempDir() {
			path = std::filesystem::temp_directory_path() /
				("lvgui_savefiledialog_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
			std::filesystem::create_directories(path);
		}
		~TempDir() {
			std::error_code ec;
			std::filesystem::remove_all(path, ec);
		}
		void touch(const std::string& name) {
			std::ofstream(path / name) << "{}";
		}
	};

	void testDialogStartsHidden() {
		TempDir dir;
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::SaveFileDialog dlg(ctx, dir.path.string(), ".pose.json");
		step(ctx);

		assert(!dlg.isOpen());

		std::cout << "✓ testDialogStartsHidden\n";
	}

	void testOpenShowsTheDialog() {
		TempDir dir;
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::SaveFileDialog dlg(ctx, dir.path.string(), ".pose.json");

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
		lvgui::SaveFileDialog dlg(ctx, dir.path.string(), ".pose.json");
		dlg.open();
		step(ctx);

		auto* list = fileList(dlg);
		assert(list != nullptr);

		clickAt(ctx, rowCentre(ctx, list, 0));
		assert(list->selectedText() == "a.pose.json");

		clickAt(ctx, rowCentre(ctx, list, 1));
		assert(list->selectedText() == "b.pose.json");

		std::cout << "✓ testExistingFilesAreListedAlphabeticallyAndFilteredByExtension\n";
	}

	void testClickingAnExistingFilePrefillsTheFilenameField() {
		TempDir dir;
		dir.touch("existing.pose.json");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::SaveFileDialog dlg(ctx, dir.path.string(), ".pose.json");
		dlg.open();
		step(ctx);

		clickAt(ctx, rowCentre(ctx, fileList(dlg), 0));

		assert(filenameField(dlg)->text() == "existing.pose.json");

		std::cout << "✓ testClickingAnExistingFilePrefillsTheFilenameField\n";
	}

	void testSaveAppendsMissingExtensionAndFiresOnConfirmWithFullPath() {
		TempDir dir;
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::SaveFileDialog dlg(ctx, dir.path.string(), ".pose.json");

		std::string confirmedPath;
		bool confirmed = false;
		dlg.setOnConfirm([&](const std::string& path) { confirmed = true; confirmedPath = path; });

		dlg.open();
		step(ctx);

		filenameField(dlg)->setText("custom_pose", false);
		clickAt(ctx, centre(saveButton(dlg)));

		assert(confirmed);
		std::filesystem::path const expected = dir.path / "custom_pose.pose.json";
		assert(confirmedPath == expected.string());
		assert(!dlg.isOpen());

		std::cout << "✓ testSaveAppendsMissingExtensionAndFiresOnConfirmWithFullPath\n";
	}

	void testSaveWithEmptyFilenameDoesNotConfirmOrClose() {
		TempDir dir;
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::SaveFileDialog dlg(ctx, dir.path.string(), ".pose.json");

		bool confirmed = false;
		dlg.setOnConfirm([&](const std::string&) { confirmed = true; });

		dlg.open();
		step(ctx);
		clickAt(ctx, centre(saveButton(dlg)));

		assert(!confirmed);
		assert(dlg.isOpen());

		std::cout << "✓ testSaveWithEmptyFilenameDoesNotConfirmOrClose\n";
	}

	void testCancelClosesAndFiresOnCancelNotOnConfirm() {
		TempDir dir;
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::SaveFileDialog dlg(ctx, dir.path.string(), ".pose.json");

		bool confirmed = false, cancelled = false;
		dlg.setOnConfirm([&](const std::string&) { confirmed = true; });
		dlg.setOnCancel([&] { cancelled = true; });

		dlg.open();
		step(ctx);
		filenameField(dlg)->setText("something", false);
		clickAt(ctx, centre(cancelButton(dlg)));

		assert(!confirmed);
		assert(cancelled);
		assert(!dlg.isOpen());

		std::cout << "✓ testCancelClosesAndFiresOnCancelNotOnConfirm\n";
	}

	void testEscapeAlsoFiresOnCancel() {
		TempDir dir;
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::SaveFileDialog dlg(ctx, dir.path.string(), ".pose.json");

		bool cancelled = false;
		dlg.setOnCancel([&] { cancelled = true; });

		dlg.open();
		step(ctx);
		pressKey(ctx, lvgui::Key::Escape);

		assert(cancelled);
		assert(!dlg.isOpen());

		std::cout << "✓ testEscapeAlsoFiresOnCancel\n";
	}

	// The dialog has to own the keyboard for its whole lifetime, not just while
	// its filename field happens to be focused: an application polling its own
	// shortcuts asks wantsKeyboard() (or VkApp::pollKey()) to know whether to
	// stay out of the way. A consumer whose "r" key reloaded the scene lost the
	// pose its user was editing to a filename with an "r" in it.
	// Enter is how a Save As dialog is finished everywhere else, and it has to be
	// Enter alone: onCommit would also fire when the user clicked from the filename
	// field across to the file list, writing the file behind their back.
	void testEnterInTheFilenameFieldSaves() {
		TempDir dir;
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::SaveFileDialog dlg(ctx, dir.path.string(), ".pose.json");
		bool confirmed = false;
		std::string confirmedPath;
		dlg.setOnConfirm([&](const std::string& p) { confirmed = true; confirmedPath = p; });

		dlg.open();
		step(ctx);
		clickAt(ctx, centre(filenameField(dlg)));
		typeAscii(ctx, "typed_by_hand");
		pressKey(ctx, lvgui::Key::Enter);

		std::filesystem::path const expected = dir.path / "typed_by_hand.pose.json";
		assert(confirmed);
		assert(confirmedPath == expected.string());
		assert(!dlg.isOpen());

		std::cout << "✓ testEnterInTheFilenameFieldSaves\n";
	}

	void testEnterWithAnEmptyFilenameDoesNothing() {
		TempDir dir;
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::SaveFileDialog dlg(ctx, dir.path.string(), ".pose.json");
		bool confirmed = false;
		dlg.setOnConfirm([&](const std::string&) { confirmed = true; });

		dlg.open();
		step(ctx);
		clickAt(ctx, centre(filenameField(dlg)));
		pressKey(ctx, lvgui::Key::Enter);

		assert(!confirmed);
		assert(dlg.isOpen());

		std::cout << "✓ testEnterWithAnEmptyFilenameDoesNothing\n";
	}

	// The other half of choosing onSubmit over onCommit.
	void testClickingAwayFromTheFilenameFieldDoesNotSave() {
		TempDir dir;
		dir.touch("existing.pose.json");
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::SaveFileDialog dlg(ctx, dir.path.string(), ".pose.json");
		bool confirmed = false;
		dlg.setOnConfirm([&](const std::string&) { confirmed = true; });

		dlg.open();
		step(ctx);
		clickAt(ctx, centre(filenameField(dlg)));
		typeAscii(ctx, "half_typed");
		clickAt(ctx, rowCentre(ctx, fileList(dlg), 0));   // focus leaves the field

		assert(!confirmed);
		assert(dlg.isOpen());

		std::cout << "✓ testClickingAwayFromTheFilenameFieldDoesNotSave\n";
	}

	void testOpenDialogOwnsTheKeyboardForAppShortcuts() {
		TempDir dir;
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::SaveFileDialog dlg(ctx, dir.path.string(), ".pose.json");
		step(ctx);
		assert(!ctx.wantsKeyboard());   // nothing open: shortcuts belong to the app

		dlg.open();
		step(ctx);
		assert(ctx.wantsKeyboard());    // open, nothing focused yet: still ours

		clickAt(ctx, centre(filenameField(dlg)));
		step(ctx);
		assert(ctx.wantsKeyboard());    // and with the field focused

		pressKey(ctx, lvgui::Key::Escape);
		step(ctx);
		assert(!dlg.isOpen());
		assert(!ctx.wantsKeyboard());   // closed again: the app gets its keys back

		std::cout << "✓ testOpenDialogOwnsTheKeyboardForAppShortcuts\n";
	}

	// Escape is not the only way out: Cancel and the title-bar X hide the panel the
	// same way, and focus left inside a hidden panel used to keep wantsKeyboard() true
	// forever -- the application's shortcuts never came back after one Save As.
	void testCancellingWithTheFieldFocusedReleasesTheKeyboard() {
		TempDir dir;
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::SaveFileDialog dlg(ctx, dir.path.string(), ".pose.json");

		dlg.open();
		step(ctx);
		clickAt(ctx, centre(filenameField(dlg)));
		typeAscii(ctx, "abandoned");
		assert(ctx.wantsKeyboard());

		clickAt(ctx, centre(cancelButton(dlg)));
		step(ctx);

		assert(!dlg.isOpen());
		assert(!ctx.wantsKeyboard());

		std::cout << "✓ testCancellingWithTheFieldFocusedReleasesTheKeyboard\n";
	}

	void testReopenRefreshesFileListing() {
		TempDir dir;
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::SaveFileDialog dlg(ctx, dir.path.string(), ".pose.json");

		dlg.open();
		step(ctx);
		// Nothing written yet -- clicking where a row would be must not select anything.
		clickAt(ctx, rowCentre(ctx, fileList(dlg), 0));
		assert(fileList(dlg)->selectedIndex() < 0);

		dir.touch("fresh.pose.json");
		dlg.open();   // re-scan
		step(ctx);
		clickAt(ctx, rowCentre(ctx, fileList(dlg), 0));
		assert(fileList(dlg)->selectedText() == "fresh.pose.json");

		std::cout << "✓ testReopenRefreshesFileListing\n";
	}

}

int main() {
	testDialogStartsHidden();
	testOpenShowsTheDialog();
	testExistingFilesAreListedAlphabeticallyAndFilteredByExtension();
	testClickingAnExistingFilePrefillsTheFilenameField();
	testSaveAppendsMissingExtensionAndFiresOnConfirmWithFullPath();
	testSaveWithEmptyFilenameDoesNotConfirmOrClose();
	testCancelClosesAndFiresOnCancelNotOnConfirm();
	testEscapeAlsoFiresOnCancel();
	testReopenRefreshesFileListing();
	testOpenDialogOwnsTheKeyboardForAppShortcuts();
	testEnterInTheFilenameFieldSaves();
	testEnterWithAnEmptyFilenameDoesNothing();
	testClickingAwayFromTheFilenameFieldDoesNotSave();
	testCancellingWithTheFieldFocusedReleasesTheKeyboard();

	std::cout << "\n✅ All SaveFileDialog tests passed!\n";
	return 0;
}
