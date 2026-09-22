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

// FBX Model Inspector
//
// A practical LVGUI demo: browse and load an FBX file, then inspect its meshes, bones,
// materials and animation clips through the GUI while it renders in the 3D view beside
// the panel. Doubles as a small asset-debugging tool and as a regression check that a
// loaded model's data (mesh/bone/material/animation counts) matches what the loader
// actually produced.
//
// Widgets demonstrated:
//   MenuBar + OpenFileDialog -- File > Open... browses assets/*.fbx
//   ListBox     -- mesh list, bone list, material list (click a row to inspect it)
//   DropDown    -- animation clip picker
//   Slider      -- animation scrub position (drag to seek) and playback speed
//   PlotLine    -- a rolling "timeline" of normalised playback position
//   ProgressBar -- clip progress with a "time / duration" overlay
//   ColorEdit3  -- read-only-ish display of a material's diffuse colour
//   CollapsingSection -- groups Meshes / Bones / Materials / Animation / Load Log
//   LogView     -- load diagnostics (success/failure, counts, errors)
//
// See docs/gui_usage.md for a guide to the GUI layer and docs/gui/05-widgets.md for the
// full per-widget spec.

#include "VkApp.h"
#include "RiggedObject.h"
#include "FBXLoader.h"
#include "LightVulkanGraphicsLogging.h"
#include <lightVulkanGraphics/ui/Ui.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace lvgui = lightGraphics::ui;
namespace fs = std::filesystem;

namespace
{

std::string formatFloat(float v, int decimals = 3)
{
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(v));
	return buf;
}

std::string vec3ToString(const glm::vec3& v)
{
	return "(" + formatFloat(v.x) + ", " + formatFloat(v.y) + ", " + formatFloat(v.z) + ")";
}

std::string toLower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

// Bundled multi-clip rigs (Worker.fbx included) commonly mix clips authored at different
// nominal facings -- e.g. Worker.fbx's own "Hips" bone points at yaw 0 deg in Run/Walk/Roll
// but yaw ~27 deg in Idle/Gun/Interact/Wave, and +-38/90 deg in the one-sided punch/kick/
// strafe clips. RiggedObject::playAnimation() faithfully reproduces whichever convention
// each clip's own keyframes use (that IS the source data), which reads as the character
// visibly spinning to face a different way on every clip switch -- not a skinning or
// pose bug (see the CPU-side hips-up/forward probes this fix was verified against; every
// clip's hips stayed within ~0.1 of straight up throughout, only the yaw varied). This
// finds a stable "facing" bone once per load so selectAnimation() can cancel that per-clip
// baseline out, independent of the actual per-frame animated facing.
int findFacingBoneIndex(const lightGraphics::RiggedModel& model)
{
	for (std::size_t i = 0; i < model.bones.size(); ++i)
	{
		std::string lower = toLower(model.bones[i].name);
		if (lower.find("hip") != std::string::npos)
		{
			return static_cast<int>(i);
		}
	}
	return -1; // no recognisable root-motion bone -- leave clips at their authored facing
}

// The bone's own composed local-to-model rotation, decoupled from RiggedObject's own
// setRotation() (never referenced by calculateBoneTransforms()), so this can be
// recomputed after applying a correction without feeding back into itself.
float facingYawDegrees(const lightGraphics::RiggedObject& object, const std::string& boneName)
{
	glm::mat4 transform = object.getBoneTransform(boneName);
	glm::vec3 forward = glm::vec3(transform * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
	if (glm::length(forward) < 1.0e-6f)
	{
		return 0.0f;
	}
	forward = glm::normalize(forward);
	return glm::degrees(std::atan2(forward.x, forward.z));
}

// One row per unique material name across every mesh in the model -- a mesh's own
// diffuseColor/diffuseTexturePath (RiggedMesh) is the only place this data lives; there
// is no separate global material table on RiggedModel.
struct MaterialSummary
{
	std::string name;
	glm::vec4 diffuseColor{ 1.0f };
	std::string texturePath;
	bool hasEmbeddedTexture = false;
};

// Everything the panel needs to know about the currently loaded model, rebuilt in full
// each time a new file is loaded. Kept separate from the widgets themselves so
// rebuildX() only has to write into ListBox::setItems() etc., never re-derive this data.
struct ModelSummary
{
	std::vector<std::string> meshLabels;
	std::vector<std::string> boneLabels;
	std::vector<MaterialSummary> materials;
	std::vector<std::string> animationLabels;
	bool valid = false;
};

ModelSummary summarize(const std::shared_ptr<lightGraphics::RiggedModel>& model)
{
	ModelSummary summary;
	if (!model)
	{
		return summary;
	}
	summary.valid = true;

	for (std::size_t i = 0; i < model->meshes.size(); ++i)
	{
		const auto& mesh = model->meshes[i];
		std::string label = "[" + std::to_string(i) + "] ";
		label += mesh.nodeName.empty() ? std::string("(unnamed mesh)") : mesh.nodeName;
		summary.meshLabels.push_back(std::move(label));

		// Fold this mesh's material into the summary list, deduplicated by name (an FBX
		// commonly reuses one material across many meshes).
		const std::string& matName = mesh.materialName.empty() ? std::string("(no material)") : mesh.materialName;
		bool alreadyListed = std::any_of(summary.materials.begin(), summary.materials.end(),
			[&](const MaterialSummary& m) { return m.name == matName; });
		if (!alreadyListed)
		{
			MaterialSummary m;
			m.name = matName;
			m.diffuseColor = mesh.diffuseColor;
			m.texturePath = mesh.diffuseTexturePath;
			m.hasEmbeddedTexture = static_cast<bool>(mesh.embeddedTexture);
			summary.materials.push_back(std::move(m));
		}
	}

	for (std::size_t i = 0; i < model->bones.size(); ++i)
	{
		const auto& bone = model->bones[i];
		summary.boneLabels.push_back("[" + std::to_string(i) + "] " + bone.name);
	}

	for (const auto& anim : model->animations)
	{
		std::string label = anim.name.empty() ? std::string("(unnamed clip)") : anim.name;
		summary.animationLabels.push_back(std::move(label));
	}

	return summary;
}

// All mutable inspector state and the widget pointers that display it, gathered in one
// place so the MenuBar/OpenFileDialog callbacks (which need to reach across the whole
// panel) don't have to close over a dozen separate locals.
struct Inspector
{
	lightGraphics::VkApp& app;

	std::shared_ptr<lightGraphics::RiggedObject> riggedObject;
	lightGraphics::RiggedObjectHandle riggedHandle;
	bool hasRiggedHandle = false;
	std::string currentPath;
	ModelSummary summary;

	int selectedAnimation = -1;
	float animSpeed = 1.0f;
	bool scrubbing = false; // true for the one frame a scrub commit just landed

	// See findFacingBoneIndex()'s comment: different clips in the same file can be
	// authored at different nominal yaws. -1 means no suitable bone was found (leaves
	// every clip at its own authored facing, same as before this existed).
	int facingBoneIndex = -1;
	bool keepFacingConsistent = true;

	// Widgets this struct needs to push data into after a (re)load.
	lvgui::Label* fileLabel = nullptr;
	lvgui::LogView* logView = nullptr;

	lvgui::Label* meshCountLabel = nullptr;
	lvgui::ListBox* meshList = nullptr;
	// One Label per line of detail -- Label has no embedded-newline support (docs/gui/
	// 05-widgets.md, "Label"; TextWrap.h wraps by width only), so a multi-line readout is
	// several Labels stacked in the panel's row layout, not one Label with "\n" in it.
	std::array<lvgui::Label*, 4> meshDetailLines{};

	lvgui::Label* boneCountLabel = nullptr;
	lvgui::ListBox* boneList = nullptr;
	std::array<lvgui::Label*, 5> boneDetailLines{};

	lvgui::ListBox* materialList = nullptr;
	lvgui::ColorEdit3* materialColor = nullptr;
	lvgui::Label* materialDetailLabel = nullptr;

	lvgui::DropDown* animDropdown = nullptr;
	lvgui::Button* playPauseButton = nullptr;
	lvgui::Slider* scrubSlider = nullptr;
	lvgui::Slider* speedSlider = nullptr;
	lvgui::ProgressBar* animProgress = nullptr;
	lvgui::PlotLine* timelinePlot = nullptr;
	lvgui::Label* animDetailLabel = nullptr;

	void log(const std::string& line)
	{
		if (logView)
		{
			logView->push(line);
		}
		std::cout << "[fbx-inspector] " << line << std::endl;
	}

	void updateMeshDetail(int index)
	{
		if (!meshDetailLines[0])
		{
			return;
		}
		if (!summary.valid || index < 0 || static_cast<std::size_t>(index) >= riggedObject->getModel()->meshes.size())
		{
			meshDetailLines[0]->setText("Select a mesh to see its details.");
			for (std::size_t i = 1; i < meshDetailLines.size(); ++i)
			{
				meshDetailLines[i]->setText("");
			}
			return;
		}
		const auto& mesh = riggedObject->getModel()->meshes[static_cast<std::size_t>(index)];
		meshDetailLines[0]->setText("Material: " + (mesh.materialName.empty() ? std::string("(none)") : mesh.materialName));
		meshDetailLines[1]->setText("Vertices: " + std::to_string(mesh.vertices.size()));
		meshDetailLines[2]->setText("Triangles: " + std::to_string(mesh.indices.size() / 3));
		meshDetailLines[3]->setText("Bones used: " + std::to_string(mesh.bones.size()));
	}

	void updateBoneDetail(int index)
	{
		if (!boneDetailLines[0])
		{
			return;
		}
		const auto& bones = riggedObject->getModel()->bones;
		if (!summary.valid || index < 0 || static_cast<std::size_t>(index) >= bones.size())
		{
			boneDetailLines[0]->setText("Select a bone to see its details.");
			for (std::size_t i = 1; i < boneDetailLines.size(); ++i)
			{
				boneDetailLines[i]->setText("");
			}
			return;
		}
		const auto& bone = bones[static_cast<std::size_t>(index)];
		std::string parentName = (bone.parentIndex >= 0 && static_cast<std::size_t>(bone.parentIndex) < bones.size())
			? bones[static_cast<std::size_t>(bone.parentIndex)].name
			: std::string("(root)");
		boneDetailLines[0]->setText("Parent: " + parentName);
		boneDetailLines[1]->setText("Children: " + std::to_string(bone.children.size()));
		boneDetailLines[2]->setText("Bind position: " + vec3ToString(bone.bindPosition));
		boneDetailLines[3]->setText("Bind scale: " + vec3ToString(bone.bindScale));
		boneDetailLines[4]->setText(std::string("Skinned by a mesh: ") + (bone.hasSkinBindTransform ? "yes" : "no (hierarchy-only)"));
	}

	void updateMaterialDetail(int index)
	{
		if (!materialDetailLabel)
		{
			return;
		}
		if (!summary.valid || index < 0 || static_cast<std::size_t>(index) >= summary.materials.size())
		{
			materialDetailLabel->setText("Select a material to see its details.");
			if (materialColor)
			{
				materialColor->setValue(lvgui::Color{ 200, 200, 200, 255 });
			}
			return;
		}
		const auto& mat = summary.materials[static_cast<std::size_t>(index)];
		std::string texInfo = mat.hasEmbeddedTexture ? std::string("embedded texture")
			: (mat.texturePath.empty() ? std::string("no texture") : mat.texturePath);
		materialDetailLabel->setText("Texture: " + texInfo);
		if (materialColor)
		{
			materialColor->setValue(lvgui::Color::fromFloats(mat.diffuseColor.r, mat.diffuseColor.g,
				mat.diffuseColor.b, mat.diffuseColor.a));
		}
	}

	void selectAnimation(int index)
	{
		selectedAnimation = index;
		if (!riggedObject || index < 0)
		{
			return;
		}
		riggedObject->playAnimation(index, true); // synchronously poses frame 0 -- see its own comment
		riggedObject->setAnimationSpeed(animSpeed);
		applyFacingCorrection();
		if (scrubSlider)
		{
			float duration = riggedObject->getAnimationDuration();
			scrubSlider->setRange(0.0f, duration > 0.0f ? duration : 1.0f);
			scrubSlider->setValue(0.0f, false);
		}
		log("Playing clip: " + summary.animationLabels[static_cast<std::size_t>(index)]);
	}

	// Cancels out this clip's own baked facing (see findFacingBoneIndex()'s comment) by
	// rotating the whole RiggedObject -- the outer transform VkApp composes OUTSIDE the
	// bone hierarchy (see VkAppRigged.cpp), so this never feeds back into
	// facingYawDegrees()'s own measurement, which reads bone-local space only. A no-op
	// (identity rotation) whenever the toggle is off or no facing bone was found, so
	// turning it off always shows each clip exactly as authored.
	void applyFacingCorrection()
	{
		if (!riggedObject)
		{
			return;
		}
		if (!keepFacingConsistent || facingBoneIndex < 0 ||
			static_cast<std::size_t>(facingBoneIndex) >= riggedObject->getModel()->bones.size())
		{
			riggedObject->setRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
			return;
		}
		const std::string& boneName = riggedObject->getModel()->bones[static_cast<std::size_t>(facingBoneIndex)].name;
		float yawDeg = facingYawDegrees(*riggedObject, boneName);
		riggedObject->setRotation(glm::angleAxis(glm::radians(-yawDeg), glm::vec3(0.0f, 1.0f, 0.0f)));
	}

	// Rebuilds every list/label from the freshly (re)loaded model. Called once right
	// after a successful load, never per-frame.
	void rebuildFromModel()
	{
		summary = summarize(riggedObject->getModel());
		facingBoneIndex = findFacingBoneIndex(*riggedObject->getModel());

		if (meshCountLabel)
		{
			meshCountLabel->setText(std::to_string(summary.meshLabels.size()) + " mesh(es)");
		}
		if (meshList)
		{
			meshList->setItems(summary.meshLabels);
			meshList->setSelectedIndex(summary.meshLabels.empty() ? -1 : 0, true);
		}

		if (boneCountLabel)
		{
			boneCountLabel->setText(std::to_string(summary.boneLabels.size()) + " bone(s)");
		}
		if (boneList)
		{
			boneList->setItems(summary.boneLabels);
			boneList->setSelectedIndex(summary.boneLabels.empty() ? -1 : 0, true);
		}

		if (materialList)
		{
			std::vector<std::string> names;
			names.reserve(summary.materials.size());
			for (const auto& m : summary.materials)
			{
				names.push_back(m.name);
			}
			materialList->setItems(names);
			materialList->setSelectedIndex(names.empty() ? -1 : 0, true);
		}

		if (animDropdown)
		{
			animDropdown->setItems(summary.animationLabels);
		}
		selectedAnimation = -1;
		if (!summary.animationLabels.empty())
		{
			if (animDropdown)
			{
				animDropdown->setSelectedIndex(0, true);
			}
			else
			{
				selectAnimation(0);
			}
		}
		else if (animDetailLabel)
		{
			animDetailLabel->setText("This model has no animation clips.");
		}
	}

	// Loads `path`, swapping it into the live scene in place of whatever rigged object
	// (if any) is currently shown. Safe to call for the very first load too (hasRiggedHandle
	// starts false).
	void loadModel(const std::string& path)
	{
		log("Loading " + path + " ...");

		// A model load is a synchronous disk read plus mesh/bone/animation processing --
		// potentially slow enough that nothing is pumping window-manager events for a
		// visible moment. Hiding around it avoids the OS reading that gap as a hung
		// application (see VkApp::hideWindow()'s doc comment).
		app.hideWindow();
		auto newObject = std::make_shared<lightGraphics::RiggedObject>(
			glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			fs::path(path).stem().string(), 1.0f, path);
		app.showWindow();

		if (!newObject->getModel())
		{
			log("FAILED to load " + path + ": " + newObject->getLastError());
			return;
		}

		if (hasRiggedHandle)
		{
			app.removeRiggedObject(riggedHandle);
		}
		riggedHandle = app.addRiggedObjectHandle(newObject);
		hasRiggedHandle = true;
		riggedObject = newObject;
		currentPath = path;

		if (fileLabel)
		{
			fileLabel->setText("File: " + fs::path(path).filename().string());
		}

		rebuildFromModel();
		updateMeshDetail(meshList ? meshList->selectedIndex() : -1);
		updateBoneDetail(boneList ? boneList->selectedIndex() : -1);
		updateMaterialDetail(materialList ? materialList->selectedIndex() : -1);

		log("Loaded OK: " + std::to_string(summary.meshLabels.size()) + " mesh(es), " +
			std::to_string(summary.boneLabels.size()) + " bone(s), " +
			std::to_string(summary.materials.size()) + " material(s), " +
			std::to_string(summary.animationLabels.size()) + " animation clip(s).");
	}

	// Advances playback and refreshes the widgets that show a live readout (scrub
	// position, progress bar, timeline plot) -- called once per frame from
	// VkApp::setUpdateCallback.
	// Left/Right (or PageUp/PageDown) step to the previous/next clip, Space toggles
	// play/pause -- global, not routed through GuiContext's own focus/Tab model, so they
	// work regardless of what (if anything) is Tab-focused. Gated on !wantsKeyboard() so
	// they never fire while a modal (the Open dialog) is up; this demo has no TextBox, so
	// that's the only thing that condition ever excludes here. Doubles as this app's own
	// scriptable-for-testing surface: `xdotool key Right` reliably steps one clip where a
	// synthetic click on a specific popup row is not (see docs/gui_usage.md and this
	// commit's own history for why that matters for a GUI with no accessibility tree).
	void handleGlobalShortcuts(lvgui::GuiContext& gui)
	{
		if (!riggedObject || summary.animationLabels.empty() || gui.wantsKeyboard())
		{
			return;
		}
		const int count = static_cast<int>(summary.animationLabels.size());
		for (const lvgui::KeyEvent& ev : gui.input().keyQueue)
		{
			if (!ev.pressed || ev.repeat)
			{
				continue;
			}
			if (ev.key == lvgui::Key::Right || ev.key == lvgui::Key::PageDown)
			{
				int next = (selectedAnimation + 1 + count) % count;
				if (animDropdown) animDropdown->setSelectedIndex(next, true);
				else selectAnimation(next);
			}
			else if (ev.key == lvgui::Key::Left || ev.key == lvgui::Key::PageUp)
			{
				int prev = (selectedAnimation - 1 + count) % count;
				if (animDropdown) animDropdown->setSelectedIndex(prev, true);
				else selectAnimation(prev);
			}
			else if (ev.key == lvgui::Key::Space && riggedObject)
			{
				// isAnimating() alone doesn't reflect the separate pause flag -- see
				// RiggedObject::isAnimationPaused()'s own doc comment.
				if (riggedObject->isAnimating() && !riggedObject->isAnimationPaused()) riggedObject->pauseAnimation();
				else riggedObject->resumeAnimation();
			}
		}
	}

	void tick(float deltaTime)
	{
		if (!riggedObject || !riggedObject->getModel())
		{
			return;
		}
		if (riggedObject->isAnimating())
		{
			riggedObject->updateAnimation(deltaTime);
		}

		float duration = riggedObject->getAnimationDuration();
		float time = riggedObject->getAnimationTime();
		float fraction = duration > 0.0f ? std::clamp(time / duration, 0.0f, 1.0f) : 0.0f;

		if (scrubSlider && !scrubbing)
		{
			scrubSlider->setValue(time, false); // false: reflect state, don't re-fire a seek
		}
		if (animProgress)
		{
			animProgress->setFraction(fraction);
			animProgress->setOverlayText(formatFloat(time, 2) + " / " + formatFloat(duration, 2) + " s");
		}
		if (timelinePlot && selectedAnimation >= 0)
		{
			timelinePlot->push(fraction);
		}
		if (playPauseButton)
		{
			playPauseButton->setLabel(riggedObject->isAnimating() && !riggedObject->isAnimationPaused() ? "Pause" : "Play");
		}
	}
};

} // namespace

int main()
{
	try
	{
		lightGraphics::VkApp app;
		app.init(1280, 800, "FBX Model Inspector");
		app.setCameraLookAt(glm::vec3(0.0f, 2.0f, 5.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		app.setKeyboardCameraEnabled(true);

		Inspector inspector{ app };

		// A flat ground plane purely for visual context -- the model itself has no floor.
		app.addObject(lightGraphics::ShapeType::CUBE, glm::vec3(0.0f, -0.05f, 0.0f),
			glm::vec3(8.0f, 0.1f, 8.0f), glm::vec4(0.25f, 0.27f, 0.30f, 1.0f),
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f), "Ground", 0.0f);

		app.finalizeScene();

		if (!app.hasGui())
		{
			std::cout << "[fbx-inspector] hasGui() is false -- running without the GUI" << std::endl;
			app.run();
			return 0;
		}

		auto& gui = app.gui();

		// assets/ is where the bundled Worker.fbx lives; drop any other .fbx you want to
		// inspect in the same folder and File > Open... will list it too.
		const fs::path assetsDir = fs::path(__FILE__).parent_path().parent_path() / "assets";

		auto openDialog = std::make_unique<lvgui::OpenFileDialog>(gui, assetsDir.string(), ".fbx");
		openDialog->setOnConfirm([&inspector](const std::string& path) { inspector.loadModel(path); });

		{
			auto file = gui.menuBar().addMenu("File");
			file.addItem("Open...", [&openDialog] { openDialog->open(); }, "Ctrl+O");
			file.addItem("Reload", [&inspector] {
				if (!inspector.currentPath.empty())
				{
					inspector.loadModel(inspector.currentPath);
				}
			}, "Ctrl+R");

			auto view = gui.menuBar().addMenu("View");
			view.addItem("Reset Camera", [&app] {
				app.setCameraLookAt(glm::vec3(0.0f, 2.0f, 5.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			});
		}

		auto* panel = gui.createPanel("Model Inspector", { 20.0f, 40.0f, 380.0f, 720.0f });
		panel->setPersistenceId("fbx-inspector-main");

		panel->add<lvgui::Label>("FBX Model Inspector")->setHeading(true);
		inspector.fileLabel = panel->add<lvgui::Label>("No file loaded -- File > Open...");
		panel->add<lvgui::Spacer>(4.0f);

		auto* meshSection = panel->add<lvgui::CollapsingSection>("Meshes", true);
		inspector.meshCountLabel = meshSection->add<lvgui::Label>("0 mesh(es)");
		inspector.meshList = meshSection->add<lvgui::ListBox>("", std::vector<std::string>{}, -1);
		inspector.meshList->setVisibleRows(5);
		inspector.meshDetailLines[0] = meshSection->add<lvgui::Label>("Select a mesh to see its details.");
		for (std::size_t i = 1; i < inspector.meshDetailLines.size(); ++i)
		{
			inspector.meshDetailLines[i] = meshSection->add<lvgui::Label>("");
		}
		inspector.meshList->setOnChange([&inspector](int idx) { inspector.updateMeshDetail(idx); });

		auto* boneSection = panel->add<lvgui::CollapsingSection>("Bones", false);
		inspector.boneCountLabel = boneSection->add<lvgui::Label>("0 bone(s)");
		inspector.boneList = boneSection->add<lvgui::ListBox>("", std::vector<std::string>{}, -1);
		inspector.boneList->setVisibleRows(6);
		inspector.boneDetailLines[0] = boneSection->add<lvgui::Label>("Select a bone to see its details.");
		for (std::size_t i = 1; i < inspector.boneDetailLines.size(); ++i)
		{
			inspector.boneDetailLines[i] = boneSection->add<lvgui::Label>("");
		}
		inspector.boneList->setOnChange([&inspector](int idx) { inspector.updateBoneDetail(idx); });

		auto* materialSection = panel->add<lvgui::CollapsingSection>("Materials", true);
		inspector.materialList = materialSection->add<lvgui::ListBox>("", std::vector<std::string>{}, -1);
		inspector.materialList->setVisibleRows(4);
		inspector.materialColor = materialSection->add<lvgui::ColorEdit3>("Diffuse", lvgui::Color{ 200, 200, 200, 255 });
		inspector.materialColor->setEnabled(false); // display-only: this demo doesn't write materials back
		inspector.materialDetailLabel = materialSection->add<lvgui::Label>("Select a material to see its details.");
		inspector.materialDetailLabel->setWordWrap(true);
		inspector.materialList->setOnChange([&inspector](int idx) { inspector.updateMaterialDetail(idx); });

		auto* animSection = panel->add<lvgui::CollapsingSection>("Animation", true);
		inspector.animDropdown = animSection->add<lvgui::DropDown>("Clip", std::vector<std::string>{}, 0);
		inspector.animDropdown->setOnChange([&inspector](int idx) { inspector.selectAnimation(idx); });

		static bool keepFacingState = inspector.keepFacingConsistent;
		auto* keepFacingCheckbox = animSection->add<lvgui::Checkbox>("Keep facing consistent across clips", keepFacingState);
		keepFacingCheckbox->bind(&keepFacingState);
		keepFacingCheckbox->setTooltip("Different clips in the same file are often authored facing different "
			"nominal directions (Worker.fbx's own Run/Walk/Roll face a different way than its Idle/Gun/Interact "
			"clips, for instance) -- on cancels that per-clip baseline out so switching clips doesn't visibly spin "
			"the character; off shows each clip exactly as authored.");
		keepFacingCheckbox->setOnChange([&inspector](bool v) {
			inspector.keepFacingConsistent = v;
			inspector.applyFacingCorrection();
		});

		auto* shortcutHintLabel = animSection->add<lvgui::Label>(
			"Keyboard: Left/Right (or PageUp/PageDown) = prev/next clip, Space = play/pause.");
		shortcutHintLabel->setWordWrap(true);
		shortcutHintLabel->setColor(lvgui::Color{ 0x9A, 0xA3, 0xAF, 0xFF });

		auto* transportRow = animSection->add<lvgui::Row>();
		inspector.playPauseButton = transportRow->add<lvgui::Button>("Play");
		inspector.playPauseButton->setOnClick([&inspector] {
			if (!inspector.riggedObject)
			{
				return;
			}
			// isAnimating() alone doesn't reflect the separate pause flag -- see
			// RiggedObject::isAnimationPaused()'s own doc comment.
			if (inspector.riggedObject->isAnimating() && !inspector.riggedObject->isAnimationPaused())
			{
				inspector.riggedObject->pauseAnimation();
			}
			else
			{
				inspector.riggedObject->resumeAnimation();
			}
		});
		auto* stopButton = transportRow->add<lvgui::Button>("Restart");
		stopButton->setOnClick([&inspector] {
			if (inspector.selectedAnimation >= 0)
			{
				inspector.selectAnimation(inspector.selectedAnimation);
			}
		});

		inspector.scrubSlider = animSection->add<lvgui::Slider>("Position", 0.0f, 1.0f, 0.0f);
		inspector.scrubSlider->setUnitSuffix(" s");
		inspector.scrubSlider->setFormat("%.2f");
		inspector.scrubSlider->setTooltip("Drag to seek within the current clip.");
		// Seek exactly once, on release -- not on every intermediate onChange tick, which
		// would fight tick()'s own per-frame setValue() while the drag is still in flight.
		inspector.scrubSlider->setOnChange([&inspector](float) { inspector.scrubbing = true; });
		inspector.scrubSlider->setOnCommit([&inspector](float target) {
			inspector.scrubbing = false;
			if (!inspector.riggedObject || inspector.selectedAnimation < 0)
			{
				return;
			}
			float delta = target - inspector.riggedObject->getAnimationTime();
			float speed = inspector.animSpeed != 0.0f ? inspector.animSpeed : 1.0f;
			inspector.riggedObject->updateAnimation(delta / speed);
		});

		inspector.animProgress = animSection->add<lvgui::ProgressBar>("Progress");

		inspector.speedSlider = animSection->add<lvgui::Slider>("Speed", 0.1f, 3.0f, 1.0f);
		inspector.speedSlider->setUnitSuffix("x");
		inspector.speedSlider->bind(&inspector.animSpeed);
		inspector.speedSlider->setOnChange([&inspector](float v) {
			if (inspector.riggedObject)
			{
				inspector.riggedObject->setAnimationSpeed(v);
			}
		});

		inspector.timelinePlot = animSection->add<lvgui::PlotLine>("Timeline", 300);
		inspector.timelinePlot->setHeight(36.0f);
		inspector.timelinePlot->setRange(0.0f, 1.0f);
		inspector.timelinePlot->setTooltip("Normalised playback position over the last few seconds.");

		inspector.animDetailLabel = animSection->add<lvgui::Label>("");

		auto* logSection = panel->add<lvgui::CollapsingSection>("Load Log", false);
		inspector.logView = logSection->add<lvgui::LogView>("");
		inspector.logView->setHeight(120.0f);
		auto* clearLogButton = logSection->add<lvgui::Button>("Clear log");
		clearLogButton->setOnClick([&inspector] { inspector.logView->clear(); });

		app.setUpdateCallback([&inspector, &gui, &openDialog](float deltaTime) {
			// Real accelerators for the File menu's shortcutHint text (MenuBar's own hint
			// is display-only -- see its header comment -- so this is what actually makes
			// Ctrl+O/Ctrl+R do something, the same "poll the key queue in the consumer's
			// own update callback" pattern gui_demo uses for its right-click context menu.
			if (!gui.wantsKeyboard())
			{
				for (const lvgui::KeyEvent& ev : gui.input().keyQueue)
				{
					if (!ev.pressed || ev.repeat || !(ev.mods & lvgui::Mod::Ctrl))
					{
						continue;
					}
					if (ev.key == lvgui::Key::O)
					{
						openDialog->open();
					}
					else if (ev.key == lvgui::Key::R && !inspector.currentPath.empty())
					{
						inspector.loadModel(inspector.currentPath);
					}
				}
			}
			inspector.handleGlobalShortcuts(gui);
			inspector.tick(deltaTime);
		});

		// Load the bundled asset on startup so there's something to inspect immediately.
		inspector.loadModel((assetsDir / "Worker.fbx").string());

		app.run();
		return 0;
	}
	catch (const std::exception& e)
	{
		lightGraphics::consoleErrorStream() << "Error: " << e.what() << std::endl;
		return -1;
	}
}
