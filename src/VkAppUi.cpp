// SPDX-License-Identifier: LGPL-3.0-or-later
//
// LVGUI integration: the core library owns and drives the GUI's Vulkan backend and the
// real GuiContext. See docs/gui/01-architecture.md for the per-frame contract and
// docs/gui/02-rendering.md for the Vulkan backend this file wires up.

#include "VkApp.h"

#ifdef LVG_WITH_UI

#include "ui/UiRenderer.h"
#include "ui/UiPlatformGlfw.h"

#include <lightVulkanGraphics/ui/DrawList.h>
#include <lightVulkanGraphics/ui/Font.h>
#include <lightVulkanGraphics/ui/GuiContext.h>
#include <lightVulkanGraphics/ui/InputState.h>

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace lightGraphics
{
	// Minimal font-payload search, deliberately mirroring the order that
	// findShaderPath() uses for spv/. Still to be formalised (install rules,
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

		// guiCreateInfo_ (setGuiCreateInfo(), VkApp.h) carries whatever the consumer
		// configured before calling init(), defaulted to GuiCreateInfo{} otherwise --
		// which already matches what this function used to hardcode here (14px,
		// Theme::dark(), 512x512). The one field it can never usefully default is
		// fontPath: GuiCreateInfo requires a real path (an empty one throws, since
		// GuiContext itself has no built-in font search order -- see its comment), so an
		// empty fontPath here specifically means "use the bundled font" and gets resolved
		// through the same findFontPath() search every other asset uses, rather than
		// being passed through to throw.
		ui::GuiCreateInfo guiInfo = guiCreateInfo_;
		if (guiInfo.fontPath.empty())
		{
			guiInfo.fontPath = findFontPath("Inter-Regular.ttf");
		}

		// uiPlatform_ (installed in createWindow(), independent of Vulkan readiness) is
		// the sole GLFW-facing input receiver; GuiContext.h must never see a GLFW header,
		// so it cannot itself be an installCallbacks() target. Its clipboard/cursor hooks
		// are still built from the real window here, and forwardInputToGui() (called
		// once per frame from mainLoop()) replays uiPlatform_'s resolved InputState into
		// guiContext_ via the same inject* calls a platform layer would use.
		guiContext_ = std::make_unique<ui::GuiContext>(guiInfo, uiPlatform_->makeHooks(window_));

		ui::UiRendererCreateInfo rendererInfo{};
		rendererInfo.device = device_;
		rendererInfo.physicalDevice = physicalDevice_;
		rendererInfo.commandPool = commandPool_;
		rendererInfo.graphicsQueue = graphicsQueue_;
		rendererInfo.renderPass = renderPass_;
		rendererInfo.subpass = 0;
		// The scene pass has no MSAA (every attachment and pipeline in this project is
		// VK_SAMPLE_COUNT_1_BIT); the UI pipeline must match whatever it is.
		rendererInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
		rendererInfo.framesInFlight = static_cast<std::uint32_t>(MAX_FRAMES_IN_FLIGHT);
		rendererInfo.vertexShaderPath = findShaderPath("ui.vert.spv");
		rendererInfo.fragmentShaderPath = findShaderPath("ui.frag.spv");

		uiRenderer_ = std::make_unique<ui::UiRenderer>();
		uiRenderer_->init(rendererInfo, guiContext_->font());

		if (debugOutput)
		{
			std::ostringstream message;
			message << "LVGUI initialised: atlas " << guiContext_->font().atlasWidth() << 'x'
			        << guiContext_->font().atlasHeight() << ", font " << guiInfo.fontPath;
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
		// guiContext_ owns no GPU or GLFW state, so it can be torn down at any point;
		// doing it here, alongside uiRenderer_, keeps both halves of the GUI's lifetime
		// together.
		guiContext_.reset();
		// uiPlatform_ is deliberately left alone here: it is installed in createWindow()
		// independent of Vulkan readiness, and its destructor (which restores GLFW's
		// previously-installed callbacks) needs the window to still be alive. cleanup()
		// calls destroyUi() well before it destroys the window, so tearing it down here
		// would just mean re-installing input capture is impossible if initUi() ever
		// ran a second time. It is reset alongside window_ in cleanup() instead.
	}

	void VkApp::forwardInputToGui()
	{
		if (!uiPlatform_ || !guiContext_)
		{
			return;
		}

		const ui::InputState& in = uiPlatform_->current();

		guiContext_->injectMousePos(in.mousePos);
		for (int i = 0; i < 3; ++i)
		{
			// mousePressed/mouseReleased can both be true in the same frame (a fast
			// click); replaying both edges in order reproduces that inside guiContext_'s
			// own beginFrame() exactly as uiPlatform_ already resolved it.
			if (in.mousePressed[i])
			{
				guiContext_->injectMouseButton(static_cast<ui::MouseButton>(i), true);
			}
			if (in.mouseReleased[i])
			{
				guiContext_->injectMouseButton(static_cast<ui::MouseButton>(i), false);
			}
		}
		if (in.wheelDelta != 0.0f)
		{
			guiContext_->injectScroll(in.wheelDelta);
		}
		for (const ui::KeyEvent& ev : in.keyQueue)
		{
			guiContext_->injectKey(ev.key, ev.mods, ev.pressed, ev.repeat);
		}
		for (std::uint32_t codepoint : in.charQueue)
		{
			guiContext_->injectChar(codepoint);
		}
	}

	void VkApp::recordUi(VkCommandBuffer cmd)
	{
		if (!uiRenderer_ || !guiContext_)
		{
			return;
		}

		// frameIndex must be the frame-in-flight index whose fence drawFrame() already
		// waited on -- not imageIndex. UiRenderer's buffer growth destroys and recreates
		// immediately with no deferred-deletion queue, and that is only safe because of
		// that wait. The swapchain can hold more images than MAX_FRAMES_IN_FLIGHT, so
		// indexing by imageIndex would also run off the end of the per-frame array.
		uiRenderer_->record(cmd,
		                    guiContext_->drawList(),
		                    static_cast<std::uint32_t>(currentFrame_),
		                    swapChainExtent_,
		                    uiLastDisplaySize_);
	}

	bool VkApp::uiWantsMouse() const
	{
		return guiContext_ && guiContext_->wantsMouse();
	}

	bool VkApp::uiWantsKeyboard() const
	{
		return guiContext_ && guiContext_->wantsKeyboard();
	}

	bool VkApp::uiWantsScroll() const
	{
		return guiContext_ && guiContext_->wantsScroll();
	}

	ui::GuiContext& VkApp::gui()
	{
		assert(guiContext_ && "VkApp::gui() called before the GUI was initialised -- check hasGui() first");
		return *guiContext_;
	}

	ui::TextureId VkApp::registerUiTexture(const std::uint8_t* rgbaPixels, uint32_t width, uint32_t height)
	{
		assert(uiRenderer_ &&
		       "VkApp::registerUiTexture() called before the GUI was initialised -- check hasGui() first");
		return uiRenderer_->registerTexture(rgbaPixels, width, height);
	}

	void VkApp::unregisterUiTexture(ui::TextureId id)
	{
		if (uiRenderer_)
		{
			uiRenderer_->unregisterTexture(id);
		}
	}

} // namespace lightGraphics

#endif // LVG_WITH_UI
