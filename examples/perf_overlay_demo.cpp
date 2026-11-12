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

#include "RiggedAnimation.h"
#include "RiggedObject.h"
#include "VkApp.h"
#include <lightVulkanGraphics/ui/Ui.h>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
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

	// Animated rigged characters (assets/Worker.fbx, loaded once and shared): the
	// skinning load, alongside the flexible-shape instances above.
	std::vector<std::shared_ptr<lightGraphics::RiggedObject>> characters;
	std::vector<lightGraphics::RiggedObjectHandle> characterHandles;
	std::shared_ptr<lightGraphics::RiggedModel> characterModel;
	bool characterModelFailed = false;
	int characterAnimation = -1;
	// --serial-animation: update characters one by one on this thread, for comparison.
	bool serialAnimation = false;
	lvgui::Label* characterLabel = nullptr;

	std::deque<float> frameTimesMs;
	static constexpr std::size_t kFrameWindow = 120; // 2s at 60fps

	// Widgets this struct needs to push data into.
	lvgui::Label* liveCountLabel = nullptr;
	lvgui::Label* statsLabel = nullptr;
	lvgui::Label* cullingLabel = nullptr;
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
		positionCharacters();
	}

	// Characters stand in a square block on top of the shape field, centred on the
	// origin the camera looks at, facing the camera.
	void positionCharacters()
	{
		const int perRow = gridColumns(characters.size());
		constexpr float kCharacterSpacing = 1.3f;
		constexpr float kStandHeight = 0.75f; // just above the bobbing 0.6-unit shapes
		const glm::quat upright = glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		const glm::quat faceCamera = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		for (std::size_t i = 0; i < characters.size(); ++i)
		{
			glm::vec3 position = gridPosition(i, perRow, kCharacterSpacing);
			position.y = kStandHeight;
			characters[i]->setPosition(position);
			characters[i]->setRotation(faceCamera * upright);
		}
	}

	bool ensureCharacterModel()
	{
		if (characterModel)
		{
			return true;
		}
		if (characterModelFailed)
		{
			return false;
		}
		const std::filesystem::path modelPath =
			std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "Worker.fbx";
		auto first = std::make_shared<lightGraphics::RiggedObject>(
			glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			"BenchCharacter", 1.0f, modelPath.string());
		if (!first->getModel())
		{
			std::cerr << "[perf-overlay] Could not load " << modelPath << ": "
			          << first->getLastError() << std::endl;
			characterModelFailed = true;
			return false;
		}
		characterModel = first->getModel();
		characterAnimation = first->getAnimationCount() > 23 ? 23 : (first->getAnimationCount() > 0 ? 0 : -1);
		return true;
	}

	void setCharacterCount(std::size_t newCount)
	{
		newCount = std::min<std::size_t>(newCount, 512);
		if (newCount > 0 && !ensureCharacterModel())
		{
			newCount = 0;
		}
		while (characters.size() < newCount)
		{
			auto character = std::make_shared<lightGraphics::RiggedObject>(
				glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
				"BenchCharacter", 1.0f, characterModel);
			if (characterAnimation >= 0)
			{
				character->playAnimation(characterAnimation, true);
				// Staggered so every character has its own pose each frame.
				character->setAnimationTime(0.37f * static_cast<float>(characters.size()));
			}
			characters.push_back(character);
			positionCharacters();
			characterHandles.push_back(app.addRiggedObjectHandle(character));
		}
		while (characters.size() > newCount)
		{
			app.removeRiggedObject(characterHandles.back());
			characterHandles.pop_back();
			characters.pop_back();
		}
		positionCharacters();
		if (characterLabel)
		{
			characterLabel->setText(std::to_string(characters.size()) + " animated character(s)");
		}
	}

	// Frames the camera so the whole grid stays in view regardless of instance count --
	// an elevated three-quarter view scaled to the grid's current footprint.
	void frameCamera(int cols, float spacing)
	{
		float halfExtent = static_cast<float>(cols) * spacing * 0.5f;
		float distance = std::max(6.0f, halfExtent * 1.6f);
		glm::vec3 eye(distance * 0.6f, distance * 0.75f, distance * 0.6f);
		app.setCameraLookAt(eye, glm::vec3(0.0f));
		// Camera::zFar defaults to 50 (Camera.h) -- comfortably enough for the default
		// grid, but eye-to-origin distance grows with instance count (see `distance`
		// above) and exceeds that default well before 50,000 instances. Once the camera
		// itself sits beyond its own far clip plane, the whole grid is behind zFar and
		// gets discarded regardless of frustum culling being on or off -- not a culling
		// bug, but this demo's own camera framing needs to keep the far plane ahead of
		// wherever it just placed the eye.
		float eyeDistance = glm::length(eye);
		app.setCameraPlanes(0.1f, std::max(50.0f, eyeDistance + halfExtent + 10.0f));
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
		if (animate)
		{
			if (serialAnimation)
			{
				for (const auto& character : characters)
				{
					character->updateAnimation(deltaTime);
				}
			}
			else
			{
				// All characters at once, on LVG's worker threads (same result).
				lightGraphics::updateAnimations(characters, deltaTime);
			}
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
		if (cullingLabel)
		{
			// One frame behind (setUpdateCallback runs before this frame's own
			// recordCommandBuffer()), same as every other reading on this panel -- a lag
			// of one frame is imperceptible and not worth a special first-frame case.
			std::size_t visible = app.getLastFrustumVisibleCount();
			std::size_t culled = app.getLastFrustumCulledCount();
			if (app.getFrustumCullingEnabled())
			{
				cullingLabel->setText(std::to_string(visible) + " visible, " + std::to_string(culled) +
					" culled (" + std::to_string(visible + culled) + " total)");
			}
			else
			{
				cullingLabel->setText(std::to_string(visible + culled) + " total, culling off");
			}
		}
	}
};

// Scripted measurement (--bench SECONDS): after --warmup seconds, records the interval
// between update callbacks (one per frame) for SECONDS, prints one "BENCH" line and closes
// the window. Run with LVG_PRESENT_MODE=uncapped so frames are not held at the monitor's
// refresh rate; LIGHT_VULKAN_GRAPHICS_SKINNING_DEVICE=cpu compares CPU skinning.
struct BenchRecorder
{
	double benchSeconds = 0.0;
	double warmupSeconds = 3.0;
	bool started = false;
	bool finished = false;
	std::chrono::steady_clock::time_point startTime{};
	std::chrono::steady_clock::time_point lastFrameTime{};
	double cpuStart = 0.0;
	std::vector<double> frameMs;

	static double processCpuSeconds()
	{
		return static_cast<double>(std::clock()) / static_cast<double>(CLOCKS_PER_SEC);
	}

	// Returns true once the measurement is complete (the caller closes the window).
	bool sample(std::size_t instances, std::size_t characters)
	{
		if (benchSeconds <= 0.0 || finished)
		{
			return false;
		}
		const auto now = std::chrono::steady_clock::now();
		if (!started)
		{
			started = true;
			startTime = now;
			return false;
		}
		const double sinceStart = std::chrono::duration<double>(now - startTime).count();
		if (sinceStart < warmupSeconds)
		{
			lastFrameTime = now;
			cpuStart = processCpuSeconds();
			return false;
		}
		frameMs.push_back(std::chrono::duration<double, std::milli>(now - lastFrameTime).count());
		lastFrameTime = now;
		if (sinceStart < warmupSeconds + benchSeconds)
		{
			return false;
		}

		finished = true;
		const double cpuSeconds = processCpuSeconds() - cpuStart;
		std::vector<double> sorted = frameMs;
		std::sort(sorted.begin(), sorted.end());
		double total = 0.0;
		for (double ms : sorted)
		{
			total += ms;
		}
		const std::size_t count = sorted.size();
		auto percentile = [&](double p)
		{
			return sorted[std::min(count - 1, static_cast<std::size_t>(p * static_cast<double>(count - 1) + 0.5))];
		};
		const double meanMs = total / static_cast<double>(count);
		std::cout << std::fixed << std::setprecision(3)
		          << "BENCH instances=" << instances
		          << " characters=" << characters
		          << " frames=" << count
		          << " mean_ms=" << meanMs
		          << " fps=" << 1000.0 / meanMs
		          << " p50_ms=" << percentile(0.50)
		          << " p95_ms=" << percentile(0.95)
		          << " p99_ms=" << percentile(0.99)
		          << " max_ms=" << sorted.back()
		          << " cpu_ms_per_frame=" << 1000.0 * cpuSeconds / static_cast<double>(count)
		          << std::endl;
		return true;
	}
};

} // namespace

int main(int argc, char* argv[])
{
	std::size_t initialInstances = 1000;
	std::size_t initialCharacters = 0;
	BenchRecorder bench;
	// --static: nothing moves, so --capture is deterministic. --no-cull: frustum culling
	// off, which must not change the picture (culled objects are off-screen), so the pair
	// of captures checks culling and the shadow pass. --capture: save the 30th frame as a
	// PNG and exit.
	bool staticScene = false;
	bool serialAnimation = false;
	// --churn K: every K frames remove one character and add one back, to measure the
	// hitch of adding or removing rigged objects in a live scene (see BENCH max_ms).
	int churnEvery = 0;
	bool noCull = false;
	std::string capturePath;
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		const bool hasValue = i + 1 < argc;
		if (arg == "--instances" && hasValue)
		{
			initialInstances = static_cast<std::size_t>(std::max(1, std::atoi(argv[++i])));
		}
		else if (arg == "--characters" && hasValue)
		{
			initialCharacters = static_cast<std::size_t>(std::max(0, std::atoi(argv[++i])));
		}
		else if (arg == "--bench" && hasValue)
		{
			bench.benchSeconds = std::max(0.0, std::atof(argv[++i]));
		}
		else if (arg == "--warmup" && hasValue)
		{
			bench.warmupSeconds = std::max(0.0, std::atof(argv[++i]));
		}
		else if (arg == "--churn" && hasValue)
		{
			churnEvery = std::max(0, std::atoi(argv[++i]));
		}
		else if (arg == "--serial-animation")
		{
			serialAnimation = true;
		}
		else if (arg == "--static")
		{
			staticScene = true;
		}
		else if (arg == "--no-cull")
		{
			noCull = true;
		}
		else if (arg == "--capture" && hasValue)
		{
			capturePath = argv[++i];
		}
		else
		{
			std::cout << "Usage: perf_overlay_demo [--instances N] [--characters N]"
			             " [--bench SECONDS] [--warmup SECONDS] [--static] [--no-cull]"
			             " [--capture OUT.png] [--churn FRAMES] [--serial-animation]" << std::endl;
			return 1;
		}
	}

	try
	{
		lightGraphics::VkApp app;
		app.init(1280, 800, "Performance Overlay");
		app.setKeyboardCameraEnabled(true);
		if (noCull)
		{
			app.setFrustumCullingEnabled(false);
		}
		app.finalizeScene();

		if (!app.hasGui())
		{
			std::cout << "[perf-overlay] hasGui() is false -- running without the GUI" << std::endl;
			app.run();
			return 0;
		}

		auto& gui = app.gui();
		BenchmarkScene scene{ app };
		scene.animate = !staticScene;
		scene.serialAnimation = serialAnimation;

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

		auto* characterSlider = sceneSection->add<lvgui::SliderInt>("Characters", 0, 512,
			static_cast<int>(initialCharacters));
		characterSlider->setScale(lvgui::SliderScale::Logarithmic);
		characterSlider->setTooltip("Animated rigged characters (Worker.fbx), applied on release. "
			"Each is skinned every frame: on the GPU by default, on the CPU with "
			"LIGHT_VULKAN_GRAPHICS_SKINNING_DEVICE=cpu.");
		characterSlider->setOnCommit([&scene](int v) { scene.setCharacterCount(static_cast<std::size_t>(v)); });
		scene.characterLabel = sceneSection->add<lvgui::Label>("0 animated character(s)");

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

		static bool frustumCullState = app.getFrustumCullingEnabled();
		auto* frustumCullCheckbox = sceneSection->add<lvgui::Checkbox>("Frustum culling", frustumCullState);
		frustumCullCheckbox->bind(&frustumCullState);
		frustumCullCheckbox->setTooltip("Skip instances whose bounding sphere falls entirely outside the "
			"camera's view before they reach the instance buffer or a draw call. Toggle off to compare -- "
			"the Visible/Culled readout below shows the effect directly. Only affects the flexible-shape "
			"pass this demo exercises, not shadow casters.");
		frustumCullCheckbox->setOnChange([&app](bool v) { app.setFrustumCullingEnabled(v); });

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
			"Note: MSAA is fixed at 1 sample, and frames are paced to the monitor's refresh "
			"rate. Launch with LVG_PRESENT_MODE=uncapped to measure beyond it.");
		fixedNote->setWordWrap(true);
		fixedNote->setColor(lvgui::Color{ 0x9A, 0xA3, 0xAF, 0xFF });

		auto* shortcutHintLabel = sceneSection->add<lvgui::Label>(
			"Keyboard: Left/Right = render mode, PageUp/PageDown = instance-count preset.");
		shortcutHintLabel->setWordWrap(true);
		shortcutHintLabel->setColor(lvgui::Color{ 0x9A, 0xA3, 0xAF, 0xFF });

		auto* perfSection = panel->add<lvgui::CollapsingSection>("Performance", true);
		scene.statsLabel = perfSection->add<lvgui::Label>("-- FPS");
		scene.cullingLabel = perfSection->add<lvgui::Label>("");
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
		int captureFrame = 0;
		bool captureRequested = false;
		int churnFrame = 0;
		app.setUpdateCallback([&scene, &gui, &app, &bench, countSlider, &capturePath, &captureFrame,
			&captureRequested, churnEvery, &churnFrame](float deltaTime) {
			if (!capturePath.empty() && !captureRequested && ++captureFrame >= 30)
			{
				captureRequested = true;
				app.requestFrameCapture([&app, &capturePath](const lightGraphics::CapturedFrame& frame)
				{
					std::string error;
					if (!frame.ok || !lightGraphics::VkApp::writePng(capturePath, frame, &error))
					{
						std::cout << "CAPTURE failed: " << (frame.ok ? error : frame.error) << std::endl;
					}
					else
					{
						std::cout << "CAPTURE wrote " << capturePath << std::endl;
					}
					glfwSetWindowShouldClose(app.getWindowPointer(), GLFW_TRUE);
				});
			}
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
			if (churnEvery > 0 && !scene.characters.empty() && ++churnFrame % churnEvery == 0)
			{
				const std::size_t count = scene.characters.size();
				scene.setCharacterCount(count - 1);
				scene.setCharacterCount(count);
			}
			if (bench.sample(scene.handles.size(), scene.characters.size()))
			{
				glfwSetWindowShouldClose(app.getWindowPointer(), GLFW_TRUE);
			}
		});

		// Seed the initial grid now that every widget it touches has been created.
		const auto setupStart = std::chrono::steady_clock::now();
		scene.setInstanceCount(initialInstances);
		countSlider->setValue(static_cast<int>(scene.targetCount), false);
		scene.setCharacterCount(initialCharacters);
		std::cout << "SETUP instances=" << scene.handles.size()
		          << " characters=" << scene.characters.size() << " ms="
		          << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - setupStart).count()
		          << std::endl;

		app.run();
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return -1;
	}
}
