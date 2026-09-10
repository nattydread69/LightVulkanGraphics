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
// device, no window. Reaches the dialog's child widgets via panel()->widgetAt() in
// the fixed order the constructor adds them: Label, ListBox, Label (status),
// Button (Open), Button (Cancel).

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

	// Fixed constructor order in OpenFileDialog.cpp: Label, ListBox, Label (status),
	// Button (Open), Button (Cancel).
	lvgui::ListBox* fileList(lvgui::OpenFileDialog& dlg) {
		return dynamic_cast<lvgui::ListBox*>(dlg.panel()->widgetAt(1));
	}
	lvgui::Button* openButton(lvgui::OpenFileDialog& dlg) {
		return dynamic_cast<lvgui::Button*>(dlg.panel()->widgetAt(3));
	}
	lvgui::Button* cancelButton(lvgui::OpenFileDialog& dlg) {
		return dynamic_cast<lvgui::Button*>(dlg.panel()->widgetAt(4));
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
		assert(list->selectedText() == "a.pose.json");

		clickAt(ctx, rowCentre(ctx, list, 1));
		assert(list->selectedText() == "b.pose.json");

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
		assert(fileList(dlg)->selectedText() == "first.pose.json");

		dir.touch("second.pose.json");
		dlg.open();   // re-scan; selection must reset, not carry over as a stale index
		step(ctx);
		assert(fileList(dlg)->selectedIndex() < 0);
		clickAt(ctx, rowCentre(ctx, fileList(dlg), 0));
		assert(fileList(dlg)->selectedText() == "first.pose.json");   // still alphabetically first
		clickAt(ctx, rowCentre(ctx, fileList(dlg), 1));
		assert(fileList(dlg)->selectedText() == "second.pose.json");

		std::cout << "✓ testReopenRefreshesFileListingAndClearsSelection\n";
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

	std::cout << "\n✅ All OpenFileDialog tests passed!\n";
	return 0;
}
