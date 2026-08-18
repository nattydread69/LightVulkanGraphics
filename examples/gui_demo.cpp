// The permanent, growing LVGUI demo. Every widget in the library gets a home in here, so
// there is always one place to look at (and run) to see the whole library working
// together. See docs/gui_usage.md for a guide and docs/gui/05-widgets.md for the full
// per-widget spec.
//
// Currently demonstrates:
//   Basics      -- Label, Separator, Spacer, Button
//   Value       -- Checkbox (incl. tri-state), RadioGroup/RadioButton, Slider (incl. int
//                  and logarithmic), DragValue. Ctrl-click Gravity or Mass for the
//                  inline text-entry hook.
//   Text        -- TextBox (placeholder, binding, filters, maxLength, password mode, and
//                  a string long enough to force horizontal scrolling)
//   DropDown    -- a short-list combo, a 20-item one to exercise popup scrolling, and a
//                  third in its own panel pinned near the bottom edge of the window to
//                  exercise the upward popup flip
//   ListBox     -- always-visible selectable list (unlike DropDown, never collapses
//                  behind a control row); click a row, or Tab-focus + Up/Down
//   Composites  -- Row (horizontal layout by weight), Vec3Field (X/Y/Z with coloured
//                  borders), CollapsingSection (header + collapsible children),
//                  ProgressBar (fraction + indeterminate animation), PlotLine (sparkline
//                  with auto-scale hysteresis), TabBar (named tabs, only the active
//                  one's children are live -- click a tab, or Tab-focus + Left/Right)
//   ColorEdit   -- ColorEdit3 (RGB) and ColorEdit4 (RGBA); click the swatch to open the
//                  SV square + hue strip (+ alpha strip for the RGBA one)
//   Panels      -- dragging and z-order; scrolling (the main panel is deliberately
//                  shorter than its content, so try the wheel and the scrollbar grab);
//                  the resize grip in any panel's bottom-right corner; the title-bar
//                  collapse arrow and close button; an anchored panel (top-right
//                  "Theme", which also hosts the light/dark/high-contrast switcher --
//                  resize the window to watch it hold its corner offset); and a tooltip
//                  (hover "Gravity")
//   Setup       -- VkApp::setGuiCreateInfo(), called before init(), to bake the font at
//                  15px instead of the 14px default
//   Persistence -- GuiContext::saveLayout()/loadLayout(): the main and Theme panels
//                  remember their position/size/collapsed/scroll across runs via
//                  gui_demo_layout.txt next to the executable
//   Image       -- a CPU-generated colormap legend, uploaded via
//                  VkApp::registerUiTexture()
//   ContextMenu -- right-click anywhere in the window: reset gravity, toggle the grid,
//                  or log a message. Detected via app.setUpdateCallback() polling
//                  ctx.input().mouseReleased[Right] -- GuiContext has no built-in
//                  right-click gesture of its own (docs/gui/05, "ContextMenu")
//   LogView     -- a fake solver feeds it a line every half second; wheel or drag its
//                  scrollbar thumb to read history, End to jump back and resume
//                  following new output (docs/gui/05, "LogView")
//   Modal       -- "Clear log..." opens a confirmation dialog that blocks every other
//                  panel and the camera entirely until dismissed (Cancel, Clear it,
//                  the title-bar X, or Escape) -- docs/gui/05, "Panel", "Modal panels"

#include "VkApp.h"
#include <lightVulkanGraphics/ui/Ui.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace lvgui = lightGraphics::ui;

int main()
{
	try
	{
		lightGraphics::VkApp app;

		// setGuiCreateInfo() must be called before init() -- it configures the GuiContext
		// init() constructs. A slightly larger font than the 14px default, set once here:
		// GuiCreateInfo::fontSize is authoritative over theme.fontSize (docs/gui/07-public-
		// api.md), so this is the only place that needs to know the chosen size.
		lvgui::GuiCreateInfo guiInfo;
		guiInfo.fontSize = 15.0f;
		app.setGuiCreateInfo(guiInfo);

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

		// Deliberately shorter than the content below has accumulated into
		// it, so the panel needs its own scrollbar to show everything below the fold --
		// try the wheel over the panel, or drag the scrollbar grab in the right-hand
		// gutter. The bottom-right corner is also a resize grip (Resizable is on by
		// default, PanelFlags::Default); drag it to see the minimum-size clamp.
		auto* panel = gui.createPanel("LVGUI Demo", { 40.0f, 40.0f, 340.0f, 700.0f });
		// Opts this panel into GuiContext::saveLayout()/loadLayout() (docs/gui/05,
		// "Panel", "Layout persistence") -- see the loadLayout()/saveLayout() calls
		// bracketing app.run() below.
		panel->setPersistenceId("main");

		panel->add<lvgui::Label>("Basics -- Label, Separator, Spacer, Button");
		panel->add<lvgui::Label>("Drag this title bar around")->setColor({ 0x9A, 0xA3, 0xAF, 0xFF });
		panel->add<lvgui::Spacer>(4.0f);

		auto* button = panel->add<lvgui::Button>("Press me");
		static int clickCount = 0;
		button->setOnClick([] { std::cout << "[gui-demo] button clicked! (" << ++clickCount << ")" << std::endl; });

		auto* toggleButton = panel->add<lvgui::Button>("Toggle");
		toggleButton->setToggle(true);

		panel->add<lvgui::Separator>("Value widgets");

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
		// Hover for theme.tooltipDelay seconds to see it appear below-right of
		// the cursor (docs/gui/06, "Tooltips").
		gravity->setTooltip("Acceleration due to gravity, applied every simulation step.");
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

		panel->add<lvgui::Separator>("TextBox");

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
		// scrolls when it exceeds the box" from the TextBox spec.
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

		panel->add<lvgui::Separator>("DropDown");

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

		panel->add<lvgui::Separator>("ListBox");

		// Unlike DropDown, always visible -- the right shape when the selection itself
		// is the point, not a setting tucked behind a control row (docs/gui/05,
		// "ListBox"). Click a row, or Tab-focus it and use Up/Down.
		static std::vector<std::string> timesteps = [] {
			std::vector<std::string> items;
			for (int i = 0; i < 12; ++i) {
				items.push_back("t = " + std::to_string(i) + ".0s");
			}
			return items;
		}();
		auto* timestepList = panel->add<lvgui::ListBox>("Timestep", timesteps, 0);
		timestepList->setVisibleRows(4);
		timestepList->setOnChange([](int idx) {
			std::cout << "[gui-demo] timestep -> " << idx << std::endl;
		});

		// CompositeWidget-derived and simple value widgets
		panel->add<lvgui::Separator>("Row, Vec3Field, CollapsingSection");

		// Row: horizontal layout by weight (1:2:1)
		auto* row = panel->add<lvgui::Row>();
		row->add<lvgui::Label>("Short");
		row->add<lvgui::Label>("This takes twice the space");
		row->add<lvgui::Label>("Short");
		row->setWeights({ 1.0f, 2.0f, 1.0f });

		// Vec3Field: X/Y/Z with colour-coded borders (this will be implemented with
		// coloured borders in future when DragValue gains border-tinting; for now it's
		// three DragValues in a row)
		glm::vec3 position{ 1.5f, 2.3f, -0.8f };
		auto* posField = panel->add<lvgui::Vec3Field>("Position", position);
		posField->setOnChange([](glm::vec3 v) {
			std::cout << "[gui-demo] position -> (" << v.x << ", " << v.y << ", " << v.z << ")" << std::endl;
		});

		// CollapsingSection: header + collapsible children
		auto* section = panel->add<lvgui::CollapsingSection>("Advanced Settings", false);
		section->add<lvgui::Checkbox>("Enable optimization");
		section->add<lvgui::Checkbox>("Use caching");

		// ColorEdit3/ColorEdit4: click the swatch to open the picker (SV square + hue
		// strip, plus an alpha strip for the RGBA variant).
		auto* tint = panel->add<lvgui::ColorEdit3>("Tint", lvgui::Color::fromHex(0x3D8BFD));
		tint->setOnChange([](lvgui::Color c) {
			std::cout << "[gui-demo] tint -> (" << static_cast<int>(c.r) << ", " << static_cast<int>(c.g)
			          << ", " << static_cast<int>(c.b) << ")" << std::endl;
		});
		panel->add<lvgui::ColorEdit4>("Glow (RGBA)", lvgui::Color{ 0x5A, 0xD0, 0x9A, 0x80 });

		// Image: a colormap legend, generated on the CPU as a 64x1 RGBA strip and
		// uploaded once via VkApp::registerUiTexture() (docs/gui/05-widgets.md, "Image").
		// A 1px-tall source stretched to 20px draws a smooth gradient, not a blocky one --
		// the atlas sampler (shared with every registered texture, UiRenderer.h) filters
		// linearly. This is the exact "colormap legend" use case docs/gui/ROADMAP.md
		// named when texture support was still on the roadmap instead of built.
		constexpr int kLegendWidth = 64;
		std::vector<std::uint8_t> legendPixels(static_cast<std::size_t>(kLegendWidth) * 4);
		for (int x = 0; x < kLegendWidth; ++x) {
			float t = static_cast<float>(x) / static_cast<float>(kLegendWidth - 1);
			lvgui::Color c = lvgui::Color::fromHSV(240.0f * (1.0f - t), 1.0f, 1.0f);   // blue -> red
			legendPixels[static_cast<std::size_t>(x) * 4 + 0] = c.r;
			legendPixels[static_cast<std::size_t>(x) * 4 + 1] = c.g;
			legendPixels[static_cast<std::size_t>(x) * 4 + 2] = c.b;
			legendPixels[static_cast<std::size_t>(x) * 4 + 3] = 0xFF;
		}
		lvgui::TextureId legendTexture = app.registerUiTexture(legendPixels.data(), kLegendWidth, 1);
		panel->add<lvgui::Image>("Colormap", legendTexture, lvgui::Vec2{ 200.0f, 20.0f });

		// ProgressBar with indeterminate animation
		auto* progressBar = panel->add<lvgui::ProgressBar>("Loading");
		progressBar->setIndeterminate(true);
		progressBar->setOverlayText("Processing...");

		// PlotLine: sparkline (starts with sample data showing a sine pattern)
		auto* plotLine = panel->add<lvgui::PlotLine>("Frame data", 100);
		plotLine->setShowLatestValue(true);
		plotLine->setHeight(60.0f);
		// Populate with a sine wave for demonstration
		for (int i = 0; i < 50; ++i) {
			float angle = (i / 50.0f) * 6.28f;  // 0 to 2π
			plotLine->push(std::sin(angle) * 0.5f + 0.5f);
		}

		panel->add<lvgui::Separator>("TabBar");

		// TabBar: groups content under named tabs instead of vertical stacking -- only
		// the active tab's children exist as far as update/draw/hit-test/layout are
		// concerned. Click a tab, or Tab-focus the bar and use Left/Right.
		auto* tabs = panel->add<lvgui::TabBar>();
		auto physicsTab = tabs->addTab("Physics");
		auto renderTab = tabs->addTab("Render");
		physicsTab.add<lvgui::Checkbox>("Enable collisions", true);
		physicsTab.add<lvgui::Slider>("Damping", 0.0f, 1.0f, 0.1f);
		renderTab.add<lvgui::Checkbox>("Wireframe", false);
		renderTab.add<lvgui::Checkbox>("Show normals", false);

		// A Closable panel: the title bar grows an X at its trailing edge, and clicking it
		// hides the panel and fires setOnClose(). The handler here just logs -- to remove
		// the panel for good instead, defer the destroy by a frame (see Panel::setOnClose).
		// PanelFlags::Default has no Closable bit, so it has to be asked for explicitly.
		auto* closablePanel = gui.createPanel("Closable (try the X)",
			{ 700.0f, 560.0f, 240.0f, 100.0f },
			lvgui::PanelFlags::Default | lvgui::PanelFlags::Closable);
		closablePanel->add<lvgui::Label>("Click the X in my title bar.");
		closablePanel->add<lvgui::Label>("Collapse me with the arrow first.");
		closablePanel->setOnClose([] {
			std::cout << "[gui-demo] closable panel closed -- restart the demo to get it back"
			          << std::endl;
		});

		// An anchored panel (docs/gui/05-widgets.md, "Panel") pinned to the top-right
		// corner -- resize the window and it holds its offset from that corner instead of
		// floating wherever it happened to start. Doubles as the theme switcher, since a
		// DropDown was already built (docs/gui/06-layout-and-theme.md, "Theme": "a way to
		// switch between them at runtime -- a DropDown is the obvious choice and you
		// already have one").
		auto* themePanel = gui.createPanel("Theme", { 1280.0f - 220.0f, 20.0f, 200.0f, 60.0f });
		themePanel->setAnchor(lvgui::Panel::Anchor::TopRight);
		themePanel->setPersistenceId("theme");
		static std::vector<std::string> themeNames = { "Dark", "Light", "High Contrast" };
		auto* themeDropdown = themePanel->add<lvgui::DropDown>("Theme", themeNames, 0);
		themeDropdown->setOnChange([&gui](int idx) {
			switch (idx) {
				case 1:  gui.theme() = lvgui::Theme::light(); break;
				case 2:  gui.theme() = lvgui::Theme::highContrast(); break;
				default: gui.theme() = lvgui::Theme::dark(); break;
			}
		});

		// The "Near bottom edge" panel anchored to the bottom-left corner --
		// thematically fitting, since it already exists to sit near an edge (its
		// DropDown's upward popup flip).
		bottomPanel->setAnchor(lvgui::Panel::Anchor::BottomLeft);

		panel->add<lvgui::Separator>("LogView");

		// Read-only, word-wrapping, internally scrollable (docs/gui/05, "LogView") --
		// unlike ListBox, this owns its OWN scroll state rather than a Panel's, and
		// auto-follows new content until you scroll away to read history (try the wheel,
		// or drag the scrollbar thumb -- unlike DropDown/ListBox's popup scrollbars,
		// this one IS draggable).
		auto* solverLog = panel->add<lvgui::LogView>("Solver output");
		solverLog->setHeight(120.0f);
		solverLog->setMaxLines(200);
		solverLog->push("[gui-demo] log view ready -- watch this fill as the demo runs");

		panel->add<lvgui::Separator>("Modal");

		// A confirmation dialog -- created once, hidden until needed. It's an ordinary
		// Panel; PanelFlags::Modal is what makes GuiContext block every other panel
		// (and the camera hand-off entirely) while it's visible (docs/gui/05, "Panel",
		// "Modal panels"). PanelFlags::Closable adds the title-bar X and lets Escape
		// dismiss it, same as Cancel.
		auto* confirmModal = gui.createPanel("Clear log?", { 460.0f, 260.0f, 300.0f, 140.0f },
			lvgui::PanelFlags::Modal | lvgui::PanelFlags::Closable);
		confirmModal->setVisible(false);
		confirmModal->add<lvgui::Label>("This clears every line currently in the solver log.")
			->setWordWrap(true);
		auto* confirmRow = confirmModal->add<lvgui::Row>();
		auto* cancelButton = confirmRow->add<lvgui::Button>("Cancel");
		auto* confirmButton = confirmRow->add<lvgui::Button>("Clear it");
		cancelButton->setOnClick([confirmModal] { confirmModal->setVisible(false); });
		confirmButton->setOnClick([confirmModal, solverLog] {
			solverLog->clear();
			confirmModal->setVisible(false);
		});

		auto* clearLogButton = panel->add<lvgui::Button>("Clear log...");
		clearLogButton->setOnClick([confirmModal] {
			confirmModal->setVisible(true);
			// Deliberately NOT calling bringToFront(): the active modal draws through
			// the overlay list regardless of its own raw z-order position
			// (docs/gui/05, "Panel", "Modal panels"), so a modal shown again after
			// being hidden doesn't need reordering to appear on top and receive input.
		});

		// ContextMenu: right-click anywhere in the window for it. GuiContext has no
		// built-in right-click gesture of its own (docs/gui/05, "ContextMenu") -- this
		// polls ctx.input().mouseReleased[Right] from the application's OWN per-frame
		// callback, exactly the way any consumer would drive one, whether from here or
		// from inside a specific widget's own update().
		auto* rightClickMenu = panel->add<lvgui::ContextMenu>();
		rightClickMenu->addItem("Reset gravity to 9.81", [gravity] { gravity->setValue(9.81f, true); });
		rightClickMenu->addItem("Toggle grid", [] { showGrid = !showGrid; });
		rightClickMenu->addSeparator();
		rightClickMenu->addItem("Log a message", [solverLog] { solverLog->push("[gui-demo] context menu action"); });

		app.setUpdateCallback([&gui, rightClickMenu, solverLog](float dt) {
			const lvgui::InputState& in = gui.input();
			if (in.mouseReleased[static_cast<int>(lvgui::MouseButton::Right)]) {
				rightClickMenu->open(in.mousePos);
			}

			// A fake "solver" feeding the log view periodically, so isFollowingBottom()'s
			// stick-to-bottom behaviour is visible without needing the context menu.
			static float logAccum = 0.0f;
			static int logStep = 0;
			logAccum += dt;
			if (logAccum >= 0.5f) {
				logAccum -= 0.5f;
				++logStep;
				solverLog->push("[step " + std::to_string(logStep) + "] residual = " +
				                 std::to_string(1.0f / static_cast<float>(logStep + 1)));
			}
		});

		// Restore "main" and "theme"'s saved position/size/collapsed/scroll from the last
		// run, if there is one -- both panels above already exist by this point, which is
		// what loadLayout() requires (docs/gui_usage.md, "Remembering where panels were
		// left"). Safe even before either has ever had a layout() pass run against it.
		const char* kLayoutPath = "gui_demo_layout.txt";
		if (std::ifstream layoutIn(kLayoutPath); layoutIn) {
			std::ostringstream buf;
			buf << layoutIn.rdbuf();
			gui.loadLayout(buf.str());
			std::cout << "[gui-demo] restored panel layout from " << kLayoutPath << std::endl;
		}

		app.run();

		// app.run() returns once the window closes -- save whatever position/size/
		// collapsed/scroll state "main" and "theme" ended up with for next launch.
		std::ofstream layoutOut(kLayoutPath);
		layoutOut << gui.saveLayout();
		std::cout << "[gui-demo] saved panel layout to " << kLayoutPath << std::endl;
	}
	catch (const std::exception& error)
	{
		std::cerr << "gui_demo failed: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
