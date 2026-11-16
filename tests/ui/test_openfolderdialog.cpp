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

// OpenFolderDialog -- headless, same pattern as test_openfiledialog.cpp: no Vulkan
// device, no window; real directory scanning against a temp directory it creates and
// removes itself.

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

	lvgui::FileListView* fileList(lvgui::OpenFolderDialog& dlg) {
		return dlg.fileList();
	}
	lvgui::Button* openButton(lvgui::OpenFolderDialog& dlg) {
		return dlg.confirmButton();
	}
	lvgui::Button* cancelButton(lvgui::OpenFolderDialog& dlg) {
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
				("lvgui_openfolderdialog_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
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

	void testOnlyFoldersAreListed() {
		TempDir dir;
		dir.mkdir("beta");
		dir.mkdir("alpha");
		dir.touch("file.txt");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFolderDialog dlg(ctx, dir.path.string());
		assert(!dlg.isOpen());
		dlg.open();
		step(ctx);
		assert(dlg.isOpen());

		const std::vector<std::string> names = listedNames(fileList(dlg));
		assert((names == std::vector<std::string>{ "alpha", "beta" }));
		assert(!dlg.fileTypeDropDown()->visible());

		std::cout << "\u2713 testOnlyFoldersAreListed\n";
	}

	void testSelectWithNothingHighlightedChoosesTheListedFolder() {
		TempDir dir;
		dir.mkdir("sub");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFolderDialog dlg(ctx, dir.path.string());
		std::string chosen;
		int cancelled = 0;
		dlg.setOnConfirm([&](const std::string& p) { chosen = p; });
		dlg.setOnCancel([&]() { ++cancelled; });
		dlg.open();
		step(ctx);

		clickAt(ctx, centre(dlg.confirmButton()));
		assert(std::filesystem::equivalent(chosen, dir.path));
		assert(!dlg.isOpen());
		assert(cancelled == 0);

		std::cout << "\u2713 testSelectWithNothingHighlightedChoosesTheListedFolder\n";
	}

	void testSelectChoosesTheHighlightedFolder() {
		TempDir dir;
		dir.mkdir("sub");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFolderDialog dlg(ctx, dir.path.string());
		std::string chosen;
		dlg.setOnConfirm([&](const std::string& p) { chosen = p; });
		dlg.open();
		step(ctx);

		clickAt(ctx, rowCentre(ctx, fileList(dlg), 0));
		assert(selectedName(fileList(dlg)) == "sub");
		clickAt(ctx, centre(dlg.confirmButton()));
		assert(std::filesystem::equivalent(chosen, dir.path / "sub"));

		std::cout << "\u2713 testSelectChoosesTheHighlightedFolder\n";
	}

	void testDoubleClickEntersTheFolderWithoutConfirming() {
		TempDir dir;
		dir.mkdir("outer");
		dir.mkdir("outer/inner");

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFolderDialog dlg(ctx, dir.path.string());
		bool confirmed = false;
		dlg.setOnConfirm([&](const std::string&) { confirmed = true; });
		dlg.open();
		step(ctx);

		doubleClickAt(ctx, rowCentre(ctx, fileList(dlg), 0));
		assert(!confirmed);
		assert(dlg.isOpen());
		assert((listedNames(fileList(dlg)) == std::vector<std::string>{ "inner" }));
		assert(std::filesystem::equivalent(dlg.directory(), dir.path / "outer"));

		std::cout << "\u2713 testDoubleClickEntersTheFolderWithoutConfirming\n";
	}

	void testCancelDoesNotConfirm() {
		TempDir dir;
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		lvgui::OpenFolderDialog dlg(ctx, dir.path.string());
		bool confirmed = false;
		int cancelled = 0;
		dlg.setOnConfirm([&](const std::string&) { confirmed = true; });
		dlg.setOnCancel([&]() { ++cancelled; });
		dlg.open();
		step(ctx);

		clickAt(ctx, centre(dlg.cancelButton()));
		assert(!confirmed);
		assert(cancelled == 1);
		assert(!dlg.isOpen());

		std::cout << "\u2713 testCancelDoesNotConfirm\n";
	}

} // namespace

int main() {
	testOnlyFoldersAreListed();
	testSelectWithNothingHighlightedChoosesTheListedFolder();
	testSelectChoosesTheHighlightedFolder();
	testDoubleClickEntersTheFolderWithoutConfirming();
	testCancelDoesNotConfirm();
	std::cout << "All OpenFolderDialog tests passed\n";
	return 0;
}
