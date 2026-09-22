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

// Performance Overlay / Benchmark Scene
//
// A drop-in-style LVGUI panel for evaluating the library's instancing throughput: drag
// the instance-count slider to grow or shrink a grid of shapes at runtime, flip Animate
// on to exercise the per-frame dirty-tracked instance update path the README describes
// (as opposed to a static scene that only pays a render cost), and watch frame time
// react live in the plot below.
//
// Widgets demonstrated:
//   SliderInt (logarithmic) -- instance count, 1 to 50,000
//   RadioButton/RadioGroup  -- global render mode (VkApp::RenderMode)
//   Checkbox   -- Animate (per-frame position updates), Shadows
//   DropDown   -- shape type
//   PlotLine   -- rolling frame-time (ms) history
//   ProgressBar-- frame time against a 16.6ms (60 FPS) budget, as a quick visual "over
//                 budget" indicator
//   CollapsingSection -- groups Scene controls from Performance readouts
//
// What this demo does NOT claim: this renderer fixes MSAA at 1 sample and presents with
// VK_PRESENT_MODE_FIFO_KHR (vsync) at every pipeline/swapchain creation site -- neither
// is wired up as a runtime option here, because neither is runtime-configurable in the
// library today. The panel says so rather than pretending otherwise.
//
// See docs/gui_usage.md for a guide to the GUI layer and docs/gui/05-widgets.md for the
// full per-widget spec.

#include "VkApp.h"
#include <lightVulkanGraphics/ui/Ui.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace lvgui = lightGraphics::ui;

namespace
{

std::string formatFloat(float v, int decimals)
{
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(v));
	return buf;
}

// Cheap HSV->RGB so each instance in the grid gets a distinct, stable colour derived
// from its index alone -- no lookup table, no state.
glm::vec4 hsvToRgb(float h, float s, float v)
{
	h = std::fmod(h, 360.0f);
	if (h < 0.0f) h += 360.0f;
	float c = v * s;
	float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
	float m = v - c;
	float r, g, b;
	if (h < 60.0f) { r = c; g = x; b = 0.0f; }
	else if (h < 120.0f) { r = x; g = c; b = 0.0f; }
	else if (h < 180.0f) { r = 0.0f; g = c; b = x; }
	else if (h < 240.0f) { r = 0.0f; g = x; b = c; }
	else if (h < 300.0f) { r = x; g = 0.0f; b = c; }
	else { r = c; g = 0.0f; b = x; }
	return glm::vec4(r + m, g + m, b + m, 1.0f);
}

// Every shape option offered by the "Shape" dropdown, in display order. LINE/MESH/HUMAN
// are excluded: LINE takes two endpoints rather than a size vector, and MESH/HUMAN are
// for custom/rigged geometry, not the flexible-shape instancing path this demo exercises.
constexpr lightGraphics::ShapeType kShapeChoices[] = {
	lightGraphics::ShapeType::SPHERE,   lightGraphics::ShapeType::CUBE,
	lightGraphics::ShapeType::CONE,     lightGraphics::ShapeType::CYLINDER,
	lightGraphics::ShapeType::CAPSULE,  lightGraphics::ShapeType::HEX,
};
const std::vector<std::string> kShapeLabels = {
	"Sphere", "Cube", "Cone", "Cylinder", "Capsule", "Hexahedral",
};

// A square-ish grid layout: `cols` grows with sqrt(count) and `spacing` shrinks to keep
// the grid's total footprint roughly constant, clamped so it never gets so dense shapes
// overlap or so sparse the camera has to back away absurdly far.
int gridColumns(std::size_t count)
{
	return static_cast<int>(std::ceil(std::sqrt(static_cast<double>(std::max<std::size_t>(count, 1)))));
}

float gridSpacing(int cols)
{
	constexpr float kTargetFootprint = 26.0f;
	return std::clamp(kTargetFootprint / static_cast<float>(std::max(cols, 1)), 0.45f, 2.2f);
}

glm::vec3 gridPosition(std::size_t index, int cols, float spacing)
{
	int col = static_cast<int>(index) % cols;
	int row = static_cast<int>(index) / cols;
	float x = (static_cast<float>(col) - static_cast<float>(cols - 1) * 0.5f) * spacing;
	float z = (static_cast<float>(row) - static_cast<float>(cols - 1) * 0.5f) * spacing;
	return glm::vec3(x, 0.0f, z);
}

// All mutable benchmark state, gathered in one place so the widget callbacks (built
// inline in main()) can reach it without a long lambda-capture list each.
struct BenchmarkScene
{
	lightGraphics::VkApp& app;

	int shapeIndex = 0; // index into kShapeChoices/kShapeLabels
	std::size_t targetCount = 1000;
	bool animate = true;

	std::vector<lightGraphics::ObjectHandle> handles;
	std::vector<glm::vec3> basePositions;         // grid position, index-aligned with handles
	// Reused every frame while animating, resized only when handles.size() changes -- see
	// tick()'s comment on why this avoids a per-frame allocation for tens of thousands of
	// instances.
	std::vector<std::pair<std::size_t, glm::vec3>> updateScratch;

	float animTime = 0.0f;

	std::deque<float> frameTimesMs;
	static constexpr std::size_t kFrameWindow = 120; // 2s at 60fps

	// Widgets this struct needs to push data into.
	lvgui::Label* liveCountLabel = nullptr;
	lvgui::Label* statsLabel = nullptr;
	lvgui::ProgressBar* budgetBar = nullptr;
	lvgui::PlotLine* frameTimePlot = nullptr;

	lightGraphics::ShapeType currentShape() const { return kShapeChoices[shapeIndex]; }

	// Removes every handle this scene owns. Deliberately per-handle (not
	// VkApp::clearObjects(), which would also remove anything else in the scene) so this
	// panel composes safely if a consumer ever adds their own objects alongside it.
	// Handles are removed from the END first (see removeObject()'s doc comment on this
	// file's rationale, below) so each erase is O(1) rather than shifting the rest of the
	// object array.
	void clearHandles()
	{
		for (auto it = handles.rbegin(); it != handles.rend(); ++it)
		{
			app.removeObject(*it);
		}
		handles.clear();
		basePositions.clear();
	}

	// Repositions every live handle from scratch using the current handle count's grid
	// layout, via one batch call rather than `handles.size()` individual ones.
	void relayout()
	{
		int cols = gridColumns(handles.size());
		float spacing = gridSpacing(cols);
		basePositions.resize(handles.size());
		std::vector<std::pair<std::size_t, glm::vec3>> updates;
		updates.reserve(handles.size());
		for (std::size_t i = 0; i < handles.size(); ++i)
		{
			glm::vec3 pos = gridPosition(i, cols, spacing);
			basePositions[i] = pos;
			updates.emplace_back(app.resolveObjectHandle(handles[i]), pos);
		}
		if (!updates.empty())
		{
			app.updateObjectPositions(updates);
		}
		updateScratch.resize(handles.size());
		frameCamera(cols, spacing);
	}

	// Frames the camera so the whole grid stays in view regardless of instance count --
	// an elevated three-quarter view scaled to the grid's current footprint.
	void frameCamera(int cols, float spacing)
	{
		float halfExtent = static_cast<float>(cols) * spacing * 0.5f;
		float distance = std::max(6.0f, halfExtent * 1.6f);
		glm::vec3 eye(distance * 0.6f, distance * 0.75f, distance * 0.6f);
		app.setCameraLookAt(eye, glm::vec3(0.0f));
	}

	// Adds/removes handles to reach `targetCount` (growing/shrinking only the delta, not
	// a full rebuild -- see setShape() for the one case that DOES need a full rebuild:
	// the shape itself changing), then relays out the whole grid, since column count can
	// change even for a small delta.
	void setInstanceCount(std::size_t newCount)
	{
		newCount = std::clamp<std::size_t>(newCount, 1, 50000);
		lightGraphics::ShapeType shape = currentShape();

		while (handles.size() < newCount)
		{
			std::size_t i = handles.size();
			float hue = std::fmod(static_cast<float>(i) * 137.508f, 360.0f); // golden-angle spread
			glm::vec4 color = hsvToRgb(hue, 0.65f, 0.95f);
			handles.push_back(app.addObject(shape, glm::vec3(0.0f), glm::vec3(0.6f), color,
				glm::quat(1.0f, 0.0f, 0.0f, 0.0f), "BenchInstance", 1.0f));
		}
		while (handles.size() > newCount)
		{
			app.removeObject(handles.back());
			handles.pop_back();
		}
		targetCount = newCount;
		relayout();
		if (liveCountLabel)
		{
			liveCountLabel->setText(std::to_string(handles.size()) + " instance(s)");
		}
	}

	// A shape change can't be applied to existing instances (no setObjectShapeType()) --
	// every handle is torn down and respawned at the same count with the new shape.
	void setShape(int newShapeIndex)
	{
		shapeIndex = newShapeIndex;
		std::size_t count = targetCount;
		clearHandles();
		setInstanceCount(count);
	}

	void resetStats()
	{
		frameTimesMs.clear();
		if (frameTimePlot)
		{
			frameTimePlot->setValues({});
		}
	}

	// Called once per frame from VkApp::setUpdateCallback. Two separate costs are
	// deliberately kept distinguishable here: rebuilding `updateScratch` and calling
	// updateObjectPositions() (the per-frame CPU update path, only paid when Animate is
	// on) versus everything else in this function, which runs regardless (bookkeeping
	// and GUI readouts, not the renderer).
	void tick(float deltaTime)
	{
		animTime += deltaTime;
		if (animate && !handles.empty())
		{
			for (std::size_t i = 0; i < handles.size(); ++i)
			{
				float bob = 0.4f * std::sin(animTime * 2.0f + static_cast<float>(i) * 0.15f);
				updateScratch[i].first = app.resolveObjectHandle(handles[i]);
				updateScratch[i].second = basePositions[i] + glm::vec3(0.0f, bob, 0.0f);
			}
			app.updateObjectPositions(updateScratch);
		}

		float frameMs = deltaTime * 1000.0f;
		frameTimesMs.push_back(frameMs);
		while (frameTimesMs.size() > kFrameWindow)
		{
			frameTimesMs.pop_front();
		}

		if (frameTimePlot)
		{
			frameTimePlot->push(frameMs);
		}
		if (budgetBar)
		{
			// Fraction of a 60 FPS (16.6ms) frame budget this frame actually cost --
			// deliberately allowed to exceed 1.0 (ProgressBar clamps its bar fill, but
			// the overlay text still reports the true percentage) so "how far over
			// budget" remains visible instead of pinning at a meaningless 100%.
			constexpr float kBudgetMs = 1000.0f / 60.0f;
			budgetBar->setFraction(frameMs / kBudgetMs);
			budgetBar->setOverlayText(formatFloat(frameMs / kBudgetMs * 100.0f, 0) + "% of 60fps budget");
		}
		if (statsLabel && !frameTimesMs.empty())
		{
			float sum = 0.0f, lo = frameTimesMs.front(), hi = frameTimesMs.front();
			for (float t : frameTimesMs)
			{
				sum += t;
				lo = std::min(lo, t);
				hi = std::max(hi, t);
			}
			float avg = sum / static_cast<float>(frameTimesMs.size());
			float fps = avg > 0.0f ? 1000.0f / avg : 0.0f;
			statsLabel->setText(formatFloat(fps, 1) + " FPS  |  avg " + formatFloat(avg, 2) +
				" ms, min " + formatFloat(lo, 2) + ", max " + formatFloat(hi, 2) +
				" (last " + std::to_string(frameTimesMs.size()) + " frames)");
		}
	}
};

} // namespace

int main()
{
	try
	{
		lightGraphics::VkApp app;
		app.init(1280, 800, "Performance Overlay");
		app.setKeyboardCameraEnabled(true);
		app.finalizeScene();

		if (!app.hasGui())
		{
			std::cout << "[perf-overlay] hasGui() is false -- running without the GUI" << std::endl;
			app.run();
			return 0;
		}

		auto& gui = app.gui();
		BenchmarkScene scene{ app };

		{
			auto view = gui.menuBar().addMenu("View");
			view.addItem("Reset stats", [&scene] { scene.resetStats(); });
		}

		auto* panel = gui.createPanel("Performance Overlay", { 20.0f, 40.0f, 360.0f, 620.0f });
		panel->setPersistenceId("perf-overlay-main");

		panel->add<lvgui::Label>("Performance Overlay")->setHeading(true);
		panel->add<lvgui::Label>("Instance-count/render-mode benchmark for the flexible-shape instancing path.")
			->setWordWrap(true);
		panel->add<lvgui::Spacer>(4.0f);

		auto* sceneSection = panel->add<lvgui::CollapsingSection>("Scene", true);

		auto* shapeDropdown = sceneSection->add<lvgui::DropDown>("Shape", kShapeLabels, 0);
		shapeDropdown->setOnChange([&scene](int idx) { scene.setShape(idx); });

		auto* countSlider = sceneSection->add<lvgui::SliderInt>("Instances", 1, 50000,
			static_cast<int>(scene.targetCount));
		countSlider->setScale(lvgui::SliderScale::Logarithmic);
		countSlider->setTooltip("Drag to resize the grid; applied once you release (avoids "
			"spawning/despawning thousands of objects on every intermediate drag frame).");
		// Applied on release only (setOnCommit), not on every drag tick (setOnChange) --
		// growing/shrinking the object list is O(delta), so firing it continuously mid-drag
		// across a 1..50000 range would spawn or despawn far more objects than the user
		// actually dragged past.
		countSlider->setOnCommit([&scene](int v) { scene.setInstanceCount(static_cast<std::size_t>(v)); });

		static constexpr int kInstancePresets[] = { 100, 1000, 10000, 50000 };
		static int presetIndex = 1; // matches scene.targetCount's 1000 default

		auto* presetRow = sceneSection->add<lvgui::Row>();
		for (int i = 0; i < 4; ++i)
		{
			int preset = kInstancePresets[i];
			std::string label = preset >= 1000 ? std::to_string(preset / 1000) + "k" : std::to_string(preset);
			auto* presetButton = presetRow->add<lvgui::Button>(label);
			presetButton->setOnClick([&scene, countSlider, preset, i] {
				scene.setInstanceCount(static_cast<std::size_t>(preset));
				countSlider->setValue(preset, false);
				presetIndex = i;
			});
		}

		scene.liveCountLabel = sceneSection->add<lvgui::Label>("0 instance(s)");

		static bool animateState = scene.animate;
		auto* animateCheckbox = sceneSection->add<lvgui::Checkbox>("Animate (per-frame position updates)", animateState);
		animateCheckbox->bind(&animateState);
		animateCheckbox->setTooltip("Off: a purely static scene (render cost only). On: every "
			"instance's position is rewritten and re-uploaded every frame, exercising the "
			"dirty-tracked instance-update path the README describes.");
		animateCheckbox->setOnChange([&scene](bool v) { scene.animate = v; });

		static bool shadowsState = app.getShadowRenderingEnabled();
		auto* shadowCheckbox = sceneSection->add<lvgui::Checkbox>("Shadows", shadowsState);
		shadowCheckbox->bind(&shadowsState);
		shadowCheckbox->setOnChange([&app](bool v) { app.setShadowRenderingEnabled(v); });

		sceneSection->add<lvgui::Separator>("Render mode");
		static lvgui::RadioGroup renderModeGroup;
		sceneSection->add<lvgui::RadioButton>("Flexible shapes (lit)", &renderModeGroup, 0);
		sceneSection->add<lvgui::RadioButton>("Wireframe", &renderModeGroup, 1);
		sceneSection->add<lvgui::RadioButton>("Unlit", &renderModeGroup, 2);
		renderModeGroup.setValue(0);
		renderModeGroup.setOnChange([&app](int v) {
			switch (v)
			{
				case 1: app.setRenderMode(lightGraphics::VkApp::RenderMode::WIREFRAME); break;
				case 2: app.setRenderMode(lightGraphics::VkApp::RenderMode::UNLIT); break;
				default: app.setRenderMode(lightGraphics::VkApp::RenderMode::FLEXIBLE_SHAPES); break;
			}
		});

		auto* fixedNote = sceneSection->add<lvgui::Label>(
			"Note: this build fixes MSAA at 1 sample and V-Sync on (FIFO) -- neither is a "
			"runtime option in the renderer today.");
		fixedNote->setWordWrap(true);
		fixedNote->setColor(lvgui::Color{ 0x9A, 0xA3, 0xAF, 0xFF });

		auto* shortcutHintLabel = sceneSection->add<lvgui::Label>(
			"Keyboard: Left/Right = render mode, PageUp/PageDown = instance-count preset.");
		shortcutHintLabel->setWordWrap(true);
		shortcutHintLabel->setColor(lvgui::Color{ 0x9A, 0xA3, 0xAF, 0xFF });

		auto* perfSection = panel->add<lvgui::CollapsingSection>("Performance", true);
		scene.statsLabel = perfSection->add<lvgui::Label>("-- FPS");
		scene.budgetBar = perfSection->add<lvgui::ProgressBar>("Frame budget");
		scene.frameTimePlot = perfSection->add<lvgui::PlotLine>("Frame time (ms)", 300);
		scene.frameTimePlot->setHeight(60.0f);
		scene.frameTimePlot->setTooltip("Rolling per-frame time in milliseconds (auto-scaled).");
		auto* resetButton = perfSection->add<lvgui::Button>("Reset stats");
		resetButton->setOnClick([&scene] { scene.resetStats(); });

		// Global keyboard shortcuts, polled from the key queue the same way gui_demo polls
		// for its right-click context menu. This demo has no TextBox, so !wantsKeyboard()
		// only ever excludes the rare case a modal is open. Also this demo's most reliable
		// path for scripted testing: `xdotool key Left` cycles the render-mode radio group
		// where a synthetic click on a specific radio button is not.
		app.setUpdateCallback([&scene, &gui, countSlider](float deltaTime) {
			if (!gui.wantsKeyboard())
			{
				for (const lvgui::KeyEvent& ev : gui.input().keyQueue)
				{
					if (!ev.pressed || ev.repeat)
					{
						continue;
					}
					if (ev.key == lvgui::Key::Right)
					{
						renderModeGroup.select((renderModeGroup.value() + 1) % 3);
					}
					else if (ev.key == lvgui::Key::Left)
					{
						renderModeGroup.select((renderModeGroup.value() + 2) % 3);
					}
					else if (ev.key == lvgui::Key::PageDown || ev.key == lvgui::Key::PageUp)
					{
						presetIndex = (presetIndex + (ev.key == lvgui::Key::PageDown ? 1 : 3)) % 4;
						scene.setInstanceCount(static_cast<std::size_t>(kInstancePresets[presetIndex]));
						countSlider->setValue(kInstancePresets[presetIndex], false);
					}
				}
			}
			scene.tick(deltaTime);
		});

		// Seed the initial grid now that every widget it touches has been created.
		scene.setInstanceCount(scene.targetCount);

		app.run();
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return -1;
	}
}
