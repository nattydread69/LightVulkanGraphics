// SPDX-License-Identifier: LGPL-3.0-or-later
//
// LVGUI integration: the core library owns and drives the GUI's Vulkan backend.
// See docs/gui/02-rendering.md and docs/gui/08-implementation-plan.md phase 3.

#include "VkApp.h"

#ifdef LVG_WITH_UI

#include "ui/UiRenderer.h"

#include <lightVulkanGraphics/ui/DrawList.h>
#include <lightVulkanGraphics/ui/Font.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace lightGraphics
{
	namespace
	{
		// The logical font size for the phase 3 debug harness. Phase 5 takes this from
		// Theme::fontSize and multiplies by the live content scale.
		constexpr float kUiDebugFontSize = 14.0f;
	}

	// Minimal font-payload search, deliberately mirroring the order that
	// findShaderPath() uses for spv/. Phase 10 formalises this (install rules,
	// FONT_PATHS.md, and the relocated-install test); until then this is enough to
	// resolve the bundled Inter build from a build tree or an install prefix.
	std::string VkApp::findFontPath(const std::string& fontName)
	{
		std::vector<std::filesystem::path> searchRoots;

		if (const char* envPath = std::getenv("LIGHT_VULKAN_GRAPHICS_FONT_PATH"))
		{
			if (envPath[0] != '\0')
			{
				searchRoots.emplace_back(envPath);
			}
		}

#ifdef LVG_BUILD_FONT_DIR
		searchRoots.emplace_back(LVG_BUILD_FONT_DIR);
#endif

		searchRoots.emplace_back("assets/fonts");
		searchRoots.emplace_back("../assets/fonts");
		searchRoots.emplace_back("../share/LightVulkanGraphics/fonts");
		searchRoots.emplace_back("/usr/local/share/LightVulkanGraphics/fonts");
		searchRoots.emplace_back("/usr/share/LightVulkanGraphics/fonts");

		std::ostringstream tried;
		for (const auto& root : searchRoots)
		{
			const std::filesystem::path candidate = root / fontName;
			tried << "\n  " << candidate.string();
			if (std::ifstream(candidate, std::ios::binary).good())
			{
				if (debugOutput)
				{
					logMessage(LogLevel::Debug, "Found font: " + candidate.string());
				}
				return candidate.string();
			}
		}

		// A GUI that silently renders nothing is worse than one that refuses to start,
		// so name every path that was tried.
		throw std::runtime_error("Could not locate GUI font '" + fontName +
		                          "'. Searched:" + tried.str());
	}

	void VkApp::initUi()
	{
		if (device_ == VK_NULL_HANDLE || renderPass_ == VK_NULL_HANDLE)
		{
			return;
		}
		if (uiRenderer_)
		{
			return;
		}

		const std::string fontPath = findFontPath("Inter-Regular.ttf");
		std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
		if (!file)
		{
			throw std::runtime_error("Could not open GUI font: " + fontPath);
		}
		const std::streamsize size = file.tellg();
		file.seekg(0, std::ios::beg);
		std::vector<std::uint8_t> ttf(static_cast<std::size_t>(size));
		if (!file.read(reinterpret_cast<char*>(ttf.data()), size))
		{
			throw std::runtime_error("Could not read GUI font: " + fontPath);
		}

		uiFont_ = std::make_unique<ui::Font>();
		// Content scale is fixed at 1.0 until phase 4/5 supplies the real value from
		// the platform layer; bake() wants fontSize * contentScale as its pixel height.
		uiFont_->bake(ttf, kUiDebugFontSize * 1.0f, 512, 512, 1.0f);

		ui::UiRendererCreateInfo createInfo{};
		createInfo.device = device_;
		createInfo.physicalDevice = physicalDevice_;
		createInfo.commandPool = commandPool_;
		createInfo.graphicsQueue = graphicsQueue_;
		createInfo.renderPass = renderPass_;
		createInfo.subpass = 0;
		// The scene pass has no MSAA (every attachment and pipeline in this project is
		// VK_SAMPLE_COUNT_1_BIT); the UI pipeline must match whatever it is.
		createInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
		createInfo.framesInFlight = static_cast<std::uint32_t>(MAX_FRAMES_IN_FLIGHT);
		createInfo.vertexShaderPath = findShaderPath("ui.vert.spv");
		createInfo.fragmentShaderPath = findShaderPath("ui.frag.spv");

		uiRenderer_ = std::make_unique<ui::UiRenderer>();
		uiRenderer_->init(createInfo, *uiFont_);

		if (const char* debugEnv = std::getenv("LVG_UI_DEBUG_DRAW"))
		{
			if (debugEnv[0] == '1')
			{
				uiDebugDrawEnabled_ = true;
			}
		}

		if (debugOutput)
		{
			std::ostringstream message;
			message << "LVGUI initialised: atlas " << uiFont_->atlasWidth() << 'x'
			        << uiFont_->atlasHeight() << ", font " << fontPath;
			logMessage(LogLevel::Debug, message.str());
		}
	}

	void VkApp::destroyUi()
	{
		if (uiRenderer_)
		{
			uiRenderer_->destroy();
			uiRenderer_.reset();
		}
		uiDebugDrawList_.reset();
		uiFont_.reset();
	}

	void VkApp::buildUiDebugDrawList()
	{
		if (!uiFont_)
		{
			return;
		}
		if (!uiDebugDrawList_)
		{
			uiDebugDrawList_ = std::make_unique<ui::DrawList>();
			// Solid-colour primitives sample this UV so they share the font atlas
			// instead of forcing a texture switch. It must be set before any geometry
			// is emitted, or fills land on an arbitrary glyph texel. DrawList::clear()
			// deliberately does not reset it, so setting it once here is enough.
			//
			// Phase 5 moves this into GuiContext's constructor, which is the first
			// object that legitimately owns both the Font and the DrawList.
			uiDebugDrawList_->setWhitePixelUV(uiFont_->whitePixelUV());
		}

		ui::DrawList& list = *uiDebugDrawList_;
		list.clear();

		// A plain red rect: exercises the solid-fill path and the white-pixel UV.
		list.addRectFilled({ 24.0f, 24.0f, 160.0f, 80.0f }, ui::Color(220, 60, 60, 255));

		// Two rects under different clip rects: the second is visibly cut in half, which
		// is what proves the scissor path is actually being exercised.
		list.pushClipRect({ 24.0f, 120.0f, 160.0f, 40.0f });
		list.addRectFilled({ 24.0f, 120.0f, 160.0f, 80.0f }, ui::Color(60, 160, 220, 255));
		list.popClipRect();

		list.addText(*uiFont_, kUiDebugFontSize, { 24.0f, 180.0f },
		             ui::Color(235, 235, 235, 255), "Hello");
	}

	void VkApp::recordUi(VkCommandBuffer cmd)
	{
		if (!uiRenderer_ || !uiDebugDrawEnabled_)
		{
			return;
		}

		buildUiDebugDrawList();
		if (!uiDebugDrawList_)
		{
			return;
		}

		// frameIndex must be the frame-in-flight index whose fence drawFrame() already
		// waited on -- not imageIndex. UiRenderer's buffer growth destroys and recreates
		// immediately with no deferred-deletion queue, and that is only safe because of
		// that wait. The swapchain can hold more images than MAX_FRAMES_IN_FLIGHT, so
		// indexing by imageIndex would also run off the end of the per-frame array.
		uiRenderer_->record(cmd,
		                    *uiDebugDrawList_,
		                    static_cast<std::uint32_t>(currentFrame_),
		                    swapChainExtent_);
	}

} // namespace lightGraphics

#endif // LVG_WITH_UI
