// The permanent, growing LVGUI demo (docs/gui/09-claude-code-prompts.md). Every widget
// implemented so far gets a home in here as it lands, phase by phase, so there is always
// one place to look at (and run) to see the whole library working together. Contrast with
// examples/phase5_gui_smoke_demo.cpp, which was a throwaway visual check for phase 5 only.
//
// Currently demonstrates:
//   Phase 5 -- Label, Separator, Spacer, Button, Panel (drag, z-order)
//   Phase 6 -- Checkbox (incl. tri-state), RadioGroup/RadioButton, Slider (incl. int and
//              logarithmic), DragValue
//   Phase 7 -- TextBox (placeholder, binding, filters, maxLength, password mode, a
//              string long enough to force horizontal scrolling), plus Ctrl-click on
//              Gravity/Mass above for the Slider/DragValue inline text-entry hook
//   Phase 8 (session A) -- DropDown: a short-list combo, a 20-item one to exercise
//              popup scrolling, and a third in its own panel pinned near the bottom
//              edge of the window to exercise the upward flip. CompositeWidget itself
//              has no standalone widget yet (Row/Vec3Field/CollapsingSection land in
//              session B) so there is nothing further to add here for it this phase.

#include "VkApp.h"
#include <lightVulkanGraphics/ui/Ui.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <exception>
#include <iostream>

namespace lvgui = lightGraphics::ui;

int main()
{
	try
	{
		lightGraphics::VkApp app;
		app.init(1280, 800, "LVGUI Demo");

		app.setCameraLookAt({ 4.0f, 2.5f, 6.0f }, { 0.0f, 0.0f, 0.0f });
		app.addObject(lightGraphics::ShapeType::SPHERE, glm::vec3(0.0f), glm::vec3(1.0f),
		              glm::vec4(0.2f, 0.55f, 0.9f, 1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
		              "Reference Sphere", 0.0f);
		app.finalizeScene();

		if (!app.hasGui())
		{
			std::cout << "[gui-demo] hasGui() is false -- no GUI available" << std::endl;
			app.run();
			return 0;
		}

		auto& gui = app.gui();

		// A second, smaller panel behind the main one, purely so clicking it demonstrates
		// z-order raising it to the front (docs/gui/05, "Panel").
		gui.createPanel("Back Panel", { 700.0f, 400.0f, 220.0f, 120.0f });

		auto* panel = gui.createPanel("LVGUI Demo", { 40.0f, 40.0f, 340.0f, 560.0f });

		panel->add<lvgui::Label>("Phase 5 -- Label, Separator, Spacer, Button");
		panel->add<lvgui::Label>("Drag this title bar around")->setColor({ 0x9A, 0xA3, 0xAF, 0xFF });
		panel->add<lvgui::Spacer>(4.0f);

		auto* button = panel->add<lvgui::Button>("Press me");
		static int clickCount = 0;
		button->setOnClick([] { std::cout << "[gui-demo] button clicked! (" << ++clickCount << ")" << std::endl; });

		auto* toggleButton = panel->add<lvgui::Button>("Toggle");
		toggleButton->setToggle(true);

		panel->add<lvgui::Separator>("Phase 6 -- value widgets");

		// Checkbox: whole-row click target, box in the control column (docs/gui/05,
		// "Checkbox", convention decision).
		static bool showGrid = true;
		panel->add<lvgui::Checkbox>("Show grid", showGrid)->bind(&showGrid);

		auto* triCheckbox = panel->add<lvgui::Checkbox>("Tri-state (click a few times)", false);
		triCheckbox->setTriState(true);

		// RadioGroup/RadioButton: exclusive selection, arrow keys move it when focused.
		// The group must outlive every RadioButton built against it -- kept alive here for
		// the lifetime of main(), same as the panel itself.
		static lvgui::RadioGroup qualityGroup;
		panel->add<lvgui::RadioButton>("Low quality", &qualityGroup, 0);
		panel->add<lvgui::RadioButton>("Medium quality", &qualityGroup, 1);
		panel->add<lvgui::RadioButton>("High quality", &qualityGroup, 2);
		qualityGroup.setValue(1);
		qualityGroup.setOnChange([](int v) { std::cout << "[gui-demo] quality -> " << v << std::endl; });

		// Slider<float>: drag the handle, Shift-drag for fine adjustment, double-click to
		// reset, drag past the panel edge to confirm capture keeps tracking.
		auto* gravity = panel->add<lvgui::Slider>("Gravity", 0.0f, 30.0f, 9.81f);
		gravity->setUnitSuffix(" m/s^2");
		gravity->setOnChange([](float v) { std::cout << "[gui-demo] gravity (live) = " << v << std::endl; });
		gravity->setOnCommit([](float v) { std::cout << "[gui-demo] gravity (committed) = " << v << std::endl; });

		// SliderInt with stepping: range 0-32 in steps of 4.
		auto* substeps = panel->add<lvgui::SliderInt>("Substeps", 0, 32, 8);
		substeps->setStep(4);

		// Logarithmic slider: a physically-tiny timestep range where a linear slider would
		// be unusable (docs/gui/05, "Logarithmic scale").
		auto* timestep = panel->add<lvgui::Slider>("Timestep", 1.0e-4f, 1.0e-1f, 1.0e-3f);
		timestep->setScale(lvgui::SliderScale::Logarithmic);
		timestep->setFormat("%.5f");
		timestep->setUnitSuffix(" s");

		// DragValue<float>: unbounded scrubber, no track, for quantities where a min/max
		// would be a lie (docs/gui/05, "DragValue").
		auto* mass = panel->add<lvgui::DragValueT<float>>("Mass", 1.0f, 0.05f);
		mass->setFormat("%.2f");

		auto* offsetX = panel->add<lvgui::DragValueT<int>>("Offset X", 0, 1.0f);
		offsetX->setSoftRange(-100, 100);

		panel->add<lvgui::Separator>("Phase 7 -- TextBox");

		// Plain TextBox: bound to a std::string, placeholder shown while empty, both
		// callbacks wired so keystroke-by-keystroke vs. commit-on-Enter/blur is visible
		// in the console (docs/gui/05, "TextBox").
		static std::string simName;
		auto* nameBox = panel->add<lvgui::TextBox>("Sim name", "");
		nameBox->setPlaceholder("untitled");
		nameBox->bind(&simName);
		nameBox->setOnChange([](std::string_view s) { std::cout << "[gui-demo] name (live) = \"" << s << "\"" << std::endl; });
		nameBox->setOnCommit([](std::string_view s) { std::cout << "[gui-demo] name (committed) = \"" << s << "\"" << std::endl; });

		// Deliberately longer than the control column is wide, so the box starts already
		// scrolled and clicking/arrowing through it exercises horizontal scroll -- "text
		// scrolls when it exceeds the box" from the phase 7 acceptance check.
		panel->add<lvgui::TextBox>("Notes",
			"This sentence is long enough that it will not fit inside the text box, so "
			"the caret drags the view sideways as you move through it.");

		// TextFilter::Integer + setMaxLength together: only digits/'-' are accepted, and
		// no more than 4 codepoints of them.
		auto* seedBox = panel->add<lvgui::TextBox>("Seed", "1337");
		seedBox->setFilter(lvgui::TextFilter::Integer);
		seedBox->setMaxLength(4);

		// Password mode: displays bullets, but caret placement is still measured against
		// the displayed bullet string, not the real (here, deliberately multi-byte-safe)
		// source text -- see docs/gui/05, "TextBox".
		auto* tokenBox = panel->add<lvgui::TextBox>("API token", "s3cr3t-key");
		tokenBox->setPasswordMode(true);

		panel->add<lvgui::Separator>("Phase 8 -- DropDown");

		// A short list: exercises open/close, arrow-key highlight vs. selection, and
		// the default open-below placement (docs/gui/05, "DropDown").
		static std::vector<std::string> qualityPresets = { "Low", "Medium", "High", "Ultra" };
		auto* qualityDropdown = panel->add<lvgui::DropDown>("Quality preset", qualityPresets, 1);
		qualityDropdown->setOnChange([](int idx) {
			std::cout << "[gui-demo] quality preset -> " << idx << std::endl;
		});

		// 20 items: more than the 12-item threshold, so the popup scrolls with a
		// scrollbar instead of growing unbounded (docs/gui/05, "DropDown").
		static std::vector<std::string> manyOptions = [] {
			std::vector<std::string> items;
			for (int i = 0; i < 20; ++i) {
				items.push_back("Option " + std::to_string(i + 1));
			}
			return items;
		}();
		panel->add<lvgui::DropDown>("Long list (20)", manyOptions, 0);

		// A second, small panel pinned near the bottom edge of the window: its
		// DropDown's popup has no room to open below, so this is where the upward
		// flip (docs/gui/05: "flips above if it would run off the bottom of the
		// framebuffer") is visible by eye.
		auto* bottomPanel = gui.createPanel("Near bottom edge", { 420.0f, 700.0f, 280.0f, 80.0f });
		bottomPanel->add<lvgui::DropDown>("Flips upward", qualityPresets, 0);

		app.run();
	}
	catch (const std::exception& error)
	{
		std::cerr << "gui_demo failed: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
