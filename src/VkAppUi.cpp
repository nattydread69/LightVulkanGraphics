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
#include <optional>
#include <sstream>
#include <vector>

namespace lightGraphics
{
	namespace
	{
		// std::getenv is a hard MSVC error under this project's /WX (C4996, "may be
		// unsafe"). Same _dupenv_s/getenv split as VkAppRigged.cpp's identical helper
		// (kept as its own copy here rather than shared -- neither file includes the
		// other).
		std::optional<std::string> getEnvironmentVariable(const char* name)
		{
#if defined(_WIN32)
			char* value = nullptr;
			std::size_t length = 0;
			if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
			{
				return std::nullopt;
			}
			std::string result(value);
			free(value);
			return result;
#else
			if (const char* value = std::getenv(name))
			{
				return std::string(value);
			}
			return std::nullopt;
#endif
		}
	}

	// Minimal font-payload search, deliberately mirroring the order that
	// findShaderPath() uses for spv/. Still to be formalised (install rules,
	// FONT_PATHS.md, and the relocated-install test); until then this is enough to
	// resolve the bundled Inter build from a build tree or an install prefix.
	std::string VkApp::findFontPath(const std::string& fontName)
	{
		std::vector<std::filesystem::path> searchRoots;

		if (auto envPath = getEnvironmentVariable("LIGHT_VULKAN_GRAPHICS_FONT_PATH"))
		{
			if (!envPath->empty())
			{
				searchRoots.emplace_back(*envPath);
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

		// Heading face (docs/gui/03-text-and-fonts.md, "Headings"): GuiContext already
		// baked a second Font, CPU-side, if GuiCreateInfo::headingFontSize was set --
		// this is the one-time step that only VkApp (the Vulkan-aware owner) can do,
		// uploading that atlas as a registered texture and reporting its id back.
		registerHeadingFontTexture();

		if (debugOutput)
		{
			std::ostringstream message;
			message << "LVGUI initialised: atlas " << guiContext_->font().atlasWidth() << 'x'
			        << guiContext_->font().atlasHeight() << ", font " << guiInfo.fontPath;
			if (guiContext_->hasHeadingFont())
			{
				message << ", heading atlas " << guiContext_->headingFont().atlasWidth() << 'x'
				        << guiContext_->headingFont().atlasHeight() << " at " << guiContext_->headingFontSize()
				        << "px";
			}
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

	void VkApp::registerHeadingFontTexture()
	{
		if (!guiContext_ || !guiContext_->hasHeadingFont())
		{
			return;
		}

		// Expands the heading Font's R8 (single-channel coverage) atlas into RGBA8 by
		// replicating each byte into the red channel and zeroing the rest.
		// registerUiTexture() only accepts RGBA8 (docs/gui/05-widgets.md, "Image"), but
		// the shared UI fragment shader (shaders/ui.frag) ALWAYS samples only the red
		// channel as coverage -- `float coverage = texture(uAtlas, vUV).r;` -- regardless
		// of whether the bound texture is the primary atlas (uploaded as true R8_UNORM)
		// or a registered RGBA8 image. That means the heading atlas can piggyback on the
		// EXISTING registerUiTexture() path with zero UiRenderer/Vulkan changes -- this
		// expansion is the only new code the trick needs. G/B/A are wasted here (4x the
		// atlas's real memory footprint), an acceptable one-time cost for a single,
		// typically-small heading atlas; not worth a second, R8-specific registration
		// path in UiRenderer for that.
		const ui::Font& heading = guiContext_->headingFont();
		const std::vector<std::uint8_t>& r8 = heading.atlasPixels();
		std::vector<std::uint8_t> rgba(r8.size() * 4, 0);
		for (std::size_t i = 0; i < r8.size(); ++i)
		{
			rgba[i * 4] = r8[i];
		}

		ui::TextureId id = registerUiTexture(rgba.data(), static_cast<uint32_t>(heading.atlasWidth()),
		                                      static_cast<uint32_t>(heading.atlasHeight()));
		guiContext_->setHeadingFontTextureId(id);
	}

} // namespace lightGraphics

#endif // LVG_WITH_UI
