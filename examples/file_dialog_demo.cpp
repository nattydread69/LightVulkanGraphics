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

// File Dialog Demo
//
// OpenFileDialog and SaveFileDialog over a small sample folder this demo creates in the
// system temp directory -- files of different types, sizes and ages, a couple of
// subfolders and a hidden file -- so every part of the dialogs has something to show:
//
//   - click the Name / Size / Modified headers to sort (click again to reverse);
//     frame_2.csv sorts before frame_10.csv because names compare "naturally"
//   - double-click a folder (or select it and press Enter) to go in, ↑ or Backspace
//     to come back out, or type a path into the path field
//   - type in Search to narrow the list; switch file type in the dropdown
//   - tick Hidden to show .settings
//   - resize the dialog from its bottom-right corner; the list grows with it
//   - in Save As, pick an existing file and Save to see the "Replace file?" question
//   - switch the Theme dropdown to see them in the light and high-contrast themes
//
// Usage: file_dialog_demo [--save]   (--save starts with Save As instead of Open)

#include "VkApp.h"
#include <lightVulkanGraphics/ui/Ui.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lvgui = lightGraphics::ui;
namespace fs = std::filesystem;

namespace {

	struct SampleFile {
		const char* name;
		std::size_t bytes;
		int hoursOld;
	};

	// Recreated on every run so the ages stay relative to "now".
	fs::path createSampleFolder() {
		fs::path const root = fs::temp_directory_path() / "lvg_file_dialog_demo";
		std::error_code ec;
		fs::remove_all(root, ec);
		fs::create_directories(root / "archive", ec);
		fs::create_directories(root / "renders", ec);

		const SampleFile files[] = {
			{ "frame_1.csv", 18'200, 30 },
			{ "frame_2.csv", 18'950, 29 },
			{ "frame_10.csv", 21'400, 20 },
			{ "results_summary.csv", 2'400'000, 2 },
			{ "Calibration.json", 3'100, 24 * 12 },
			{ "pose_A.pose.json", 640, 24 * 3 },
			{ "pose_B.pose.json", 655, 5 },
			{ "notes.txt", 1'200, 24 * 40 },
			{ "scan_volume.raw", 64'000'000, 24 * 7 },
			{ ".settings", 220, 24 * 90 },
			{ "archive/old_run.csv", 5'000, 24 * 200 },
			{ "renders/still_001.png", 812'000, 1 },
		};
		auto const now = fs::file_time_type::clock::now();
		for (const SampleFile& f : files) {
			fs::path const path = root / f.name;
			std::ofstream(path, std::ios::binary).close();
			// Only the size matters to the dialog, so set the length rather than writing
			// the bytes -- no 64 MB of filler written to disk on every run.
			fs::resize_file(path, f.bytes, ec);
			fs::last_write_time(path, now - std::chrono::hours(f.hoursOld), ec);
		}
		return root;
	}

}

int main(int argc, char** argv)
{
	bool startWithSave = false;
	for (int i = 1; i < argc; ++i) {
		if (std::string_view(argv[i]) == "--save") {
			startWithSave = true;
		}
	}

	try
	{
		lightGraphics::VkApp app;
		app.init(1280, 800, "File Dialog Demo");
		app.setKeyboardCameraEnabled(false); // no 3D scene here
		app.finalizeScene();

		if (!app.hasGui())
		{
			std::cout << "[file-dialog-demo] hasGui() is false -- nothing to show without the GUI" << std::endl;
			app.run();
			return 0;
		}

		auto& gui = app.gui();
		fs::path const sampleDir = createSampleFolder();

		const std::vector<lvgui::FileTypeFilter> filters = {
			{ "Data files (*.csv, *.json)", { ".csv", ".json" } },
			{ "CSV files (*.csv)", { ".csv" } },
			{ "Pose files (*.pose.json)", { ".pose.json" } },
			{ "All files", {} },
		};

		auto* panel = gui.createPanel("File dialogs", { 20.0f, 40.0f, 360.0f, 150.0f });
		auto* resultLabel = panel->add<lvgui::Label>("Nothing chosen yet.");
		resultLabel->setWordWrap(true);

		auto openDialog = std::make_unique<lvgui::OpenFileDialog>(gui, sampleDir.string(), ".csv");
		openDialog->setFileTypeFilters(filters, 0);
		openDialog->setOnConfirm([resultLabel](const std::string& path) { resultLabel->setText("Open: " + path); });
		openDialog->setOnCancel([resultLabel] { resultLabel->setText("Open cancelled."); });

		auto saveDialog = std::make_unique<lvgui::SaveFileDialog>(gui, sampleDir.string(), ".csv");
		saveDialog->setFileTypeFilters(filters, 1);
		saveDialog->setOnConfirm([resultLabel](const std::string& path) { resultLabel->setText("Save: " + path); });
		saveDialog->setOnCancel([resultLabel] { resultLabel->setText("Save cancelled."); });

		auto* row = panel->add<lvgui::Row>();
		row->add<lvgui::Button>("Open...")->setOnClick([&openDialog] { openDialog->open(); });
		row->add<lvgui::Button>("Save As...")->setOnClick([&saveDialog] { saveDialog->open("untitled.csv"); });

		// The dialogs take every colour from the theme (bar the folder icons' amber), so
		// switching here restyles them too.
		auto* themeDropDown = panel->add<lvgui::DropDown>("Theme",
			std::vector<std::string>{ "Dark", "Light", "High contrast" }, 0);
		themeDropDown->setOnChange([&gui](int index) {
			float const fontSize = gui.theme().fontSize;
			gui.theme() = index == 1 ? lvgui::Theme::light()
			            : index == 2 ? lvgui::Theme::highContrast()
			                         : lvgui::Theme::dark();
			gui.theme().fontSize = fontSize;   // presets carry their own default size
		});

		{
			auto file = gui.menuBar().addMenu("File");
			file.addItem("Open...", [&openDialog] { openDialog->open(); }, "Ctrl+O");
			file.addItem("Save As...", [&saveDialog] { saveDialog->open("untitled.csv"); }, "Ctrl+S");
		}

		// Open the first dialog from the first frame rather than here: the GUI only learns
		// the window size once a frame has begun, and open() centres against it.
		bool shownFirstDialog = false;
		app.setUpdateCallback([&](float) {
			if (!shownFirstDialog) {
				shownFirstDialog = true;
				if (startWithSave) {
					saveDialog->open("untitled.csv");
				} else {
					openDialog->open();
				}
			}
		});

		app.run();
	}
	catch (const std::exception& error)
	{
		std::cerr << "file_dialog_demo failed: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
