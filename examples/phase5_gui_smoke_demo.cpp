// THROWAWAY -- phase 5 visual smoke test for LVGUI. Not part of the deliverable; not
// wired into a permanent CMake target. Delete this file (and its CMakeLists.txt entry)
// once the phase 5 integration has been confirmed by eye.

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
		app.init(1280, 800, "LVGUI Phase 5 Smoke Test");

		app.setCameraLookAt({ 4.0f, 2.5f, 6.0f }, { 0.0f, 0.0f, 0.0f });
		app.addObject(lightGraphics::ShapeType::SPHERE, glm::vec3(0.0f), glm::vec3(1.0f),
		              glm::vec4(0.2f, 0.55f, 0.9f, 1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
		              "Reference Sphere", 0.0f);
		app.finalizeScene();

		if (app.hasGui())
		{
			auto& gui = app.gui();

			// A second, smaller panel behind the main one -- purely so clicking it can
			// demonstrate z-order raising it to the front.
			gui.createPanel("Back Panel", { 260.0f, 90.0f, 220.0f, 120.0f });

			auto* panel = gui.createPanel("LVGUI Demo", { 40.0f, 40.0f, 300.0f, 220.0f });
			panel->add<lvgui::Label>("Phase 5 smoke test");
			panel->add<lvgui::Label>("Drag this title bar around");
			panel->add<lvgui::Separator>();
			auto* button = panel->add<lvgui::Button>("Press me");
			button->setOnClick([] { std::cout << "[phase5-demo] button clicked!" << std::endl; });
		}
		else
		{
			std::cout << "[phase5-demo] hasGui() is false -- no GUI available" << std::endl;
		}

		app.run();
	}
	catch (const std::exception& error)
	{
		std::cerr << "phase5_gui_smoke_demo failed: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
