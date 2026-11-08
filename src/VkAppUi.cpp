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

#include <algorithm>
#include <string_view>
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
		// Optional: without it colour images fall back to coverage drawing (an install
		// whose shader directory predates ui_image.frag).
		try
		{
			rendererInfo.imageFragmentShaderPath = findShaderPath("ui_image.frag.spv");
		}
		catch (const std::exception&)
		{
			logMessage(LogLevel::Warning, "ui_image.frag.spv not found: LVGUI images will draw as coverage, not colour");
		}

		// Optional: without it SDF faces (GuiCreateInfo::headingFontSdf) draw as plain coverage.
		try
		{
			rendererInfo.sdfFragmentShaderPath = findShaderPath("ui_sdf.frag.spv");
		}
		catch (const std::exception&)
		{
			logMessage(LogLevel::Warning, "ui_sdf.frag.spv not found: SDF fonts will look blurry");
		}
		// sRGB swapchains blend linearly already; the gamma-space coverage compensation in
		// ui.frag / ui_sdf.frag would double-correct there.
		rendererInfo.textContrast =
			(swapChainImageFormat_ == VK_FORMAT_B8G8R8A8_SRGB || swapChainImageFormat_ == VK_FORMAT_R8G8B8A8_SRGB)
				? 0.0f
				: 1.0f;

		uiRenderer_ = std::make_unique<ui::UiRenderer>();
		uiRenderer_->init(rendererInfo, guiContext_->font());
		guiContext_->markFontUploaded(ui::FontStyle::Regular);

		// Secondary faces (heading / bold / monospace; docs/gui/03-text-and-fonts.md):
		// GuiContext already baked each configured Font CPU-side -- this is the one-time
		// step that only VkApp (the Vulkan-aware owner) can do, uploading each atlas as a
		// registered texture and reporting its id back.
		registerHeadingFontTexture();

		if (debugOutput)
		{
			std::ostringstream message;
			message << "LVGUI initialised: atlas " << guiContext_->font().atlasWidth() << 'x'
			        << guiContext_->font().atlasHeight() << ", font " << guiInfo.fontPath;
			if (guiContext_->hasStyleFont(ui::FontStyle::Monospace))
			{
				message << ", mono face";
			}
			if (guiContext_->hasStyleFont(ui::FontStyle::Bold))
			{
				message << ", bold face";
			}
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
		                    uiFrameListActive_ ? uiFrameList_ : guiContext_->drawList(),
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

	namespace
	{
		// Expands a Font's R8 atlas into the RGBA8 buffer UiRenderer::registerTexture()
		// takes, replicating each byte into the red channel (the coverage and SDF shaders
		// read only .r). G/B/A are wasted -- 4x the atlas's real footprint -- an acceptable
		// cost for the few small secondary atlases.
		std::vector<std::uint8_t> expandAtlasToRgba(const ui::Font& font)
		{
			const std::vector<std::uint8_t>& r8 = font.atlasPixels();
			std::vector<std::uint8_t> rgba(r8.size() * 4, 0);
			for (std::size_t i = 0; i < r8.size(); ++i)
			{
				rgba[i * 4] = r8[i];
			}
			return rgba;
		}

		constexpr ui::FontStyle kSecondaryStyles[] = {
			ui::FontStyle::Heading, ui::FontStyle::Bold, ui::FontStyle::Monospace};
	}

	void VkApp::registerHeadingFontTexture()
	{
		if (!guiContext_ || !uiRenderer_)
		{
			return;
		}
		for (ui::FontStyle style : kSecondaryStyles)
		{
			if (!guiContext_->hasStyleFont(style))
			{
				continue;
			}
			const ui::Font& font = guiContext_->styledFont(style).font;
			const std::vector<std::uint8_t> rgba = expandAtlasToRgba(font);
			// Coverage faces draw through ui.frag; SDF faces need the distance-field shader.
			const ui::TextureKind kind = font.isSdf() ? ui::TextureKind::Sdf : ui::TextureKind::Coverage;
			ui::TextureId id = uiRenderer_->registerTexture(
				rgba.data(), static_cast<uint32_t>(font.atlasWidth()),
				static_cast<uint32_t>(font.atlasHeight()), kind);
			guiContext_->setStyleFontTextureId(style, id);
			guiContext_->markFontUploaded(style);
		}
	}

	void VkApp::buildUiFrameList()
	{
		uiFrameListActive_ = false;
		if (!guiContext_ || !uiRenderer_)
		{
			return;
		}

		std::vector<const ScreenTextResource*> items;
		for (const ScreenTextResource& resource : screenTexts_)
		{
			if (resource.alive && resource.atlas && resource.description.visible &&
			    !resource.description.text.empty())
			{
				items.push_back(&resource);
			}
		}
		if (items.empty())
		{
			return;
		}
		std::stable_sort(items.begin(), items.end(), [](const ScreenTextResource* a, const ScreenTextResource* b) {
			return a->description.sortKey < b->description.sortKey;
		});

		// Screen-text coordinates are framebuffer pixels; the GUI list is logical pixels.
		const float contentScale = (uiLastDisplaySize_.x > 0.0f)
			? static_cast<float>(swapChainExtent_.width) / uiLastDisplaySize_.x
			: 1.0f;
		const float inv = 1.0f / contentScale;

		uiFrameList_.clear();
		uiFrameList_.setWhitePixelUV(guiContext_->font().whitePixelUV());
		uiFrameList_.setPixelScale(contentScale);

		for (const ScreenTextResource* item : items)
		{
			const ScreenTextDescription& d = item->description;
			const ui::FontStyle style = d.monospace ? ui::FontStyle::Monospace
			                                        : (d.bold ? ui::FontStyle::Bold : ui::FontStyle::Regular);
			const ui::StyledFont sf = guiContext_->styledFont(style);
			const float size = (d.fontPixelSize > 0.0f ? d.fontPixelSize * inv : sf.pixelSize) * d.scale;
			const float lineHeight = sf.font.lineHeight(size);

			std::vector<std::string_view> lines;
			for (std::size_t start = 0; start <= d.text.size();)
			{
				std::size_t end = d.text.find('\n', start);
				if (end == std::string::npos)
				{
					end = d.text.size();
				}
				lines.emplace_back(d.text.data() + start, end - start);
				start = end + 1;
			}

			float width = 0.0f;
			for (std::string_view line : lines)
			{
				width = std::max(width, sf.font.measureText(line, size).x);
			}
			const float height = lineHeight * static_cast<float>(lines.size());
			const ui::Vec2 origin{d.positionPixels.x * inv, d.positionPixels.y * inv};

			if (d.backgroundEnabled)
			{
				// Cover the shadow too, as the stroke-font path always has.
				ui::Vec2 lo = origin;
				ui::Vec2 hi{origin.x + width, origin.y + height};
				if (d.shadowEnabled)
				{
					lo.x = std::min(lo.x, origin.x + d.shadowOffsetPixels.x * inv);
					lo.y = std::min(lo.y, origin.y + d.shadowOffsetPixels.y * inv);
					hi.x = std::max(hi.x, origin.x + width + d.shadowOffsetPixels.x * inv);
					hi.y = std::max(hi.y, origin.y + height + d.shadowOffsetPixels.y * inv);
				}
				const float pad = d.backgroundPaddingPixels * inv;
				uiFrameList_.addRectFilled(
					ui::Rect{lo.x - pad, lo.y - pad, hi.x - lo.x + 2.0f * pad, hi.y - lo.y + 2.0f * pad},
					ui::Color::fromFloats(d.backgroundColor.r, d.backgroundColor.g, d.backgroundColor.b,
					                      d.backgroundColor.a));
			}

			auto drawPass = [&](ui::Vec2 offset, const glm::vec4& color) {
				const ui::Color c = ui::Color::fromFloats(color.r, color.g, color.b, color.a);
				float y = origin.y + offset.y;
				for (std::string_view line : lines)
				{
					if (!line.empty())
					{
						uiFrameList_.addText(sf.font, size, {origin.x + offset.x, y}, c, line,
						                     ui::TextFlags::None, sf.textureId);
					}
					y += lineHeight;
				}
			};
			if (d.shadowEnabled)
			{
				drawPass({d.shadowOffsetPixels.x * inv, d.shadowOffsetPixels.y * inv}, d.shadowColor);
			}
			drawPass({0.0f, 0.0f}, d.color);
		}

		uiFrameList_.append(guiContext_->drawList());
		uiFrameListActive_ = true;
	}

	void VkApp::syncUiFontAtlases()
	{
		if (!guiContext_ || !uiRenderer_)
		{
			return;
		}
		// Dynamic glyphs (Font::atlasRevision()) are baked while the frame's draw list is
		// being built; push any atlas that changed to the GPU before it is recorded.
		if (guiContext_->fontNeedsUpload(ui::FontStyle::Regular))
		{
			uiRenderer_->syncAtlas(guiContext_->font());
			guiContext_->markFontUploaded(ui::FontStyle::Regular);
		}
		for (ui::FontStyle style : kSecondaryStyles)
		{
			if (!guiContext_->hasStyleFont(style) || !guiContext_->fontNeedsUpload(style))
			{
				continue;
			}
			const ui::Font& font = guiContext_->styledFont(style).font;
			const std::vector<std::uint8_t> rgba = expandAtlasToRgba(font);
			uiRenderer_->updateTexture(guiContext_->styleFontTextureId(style), rgba.data(),
			                           static_cast<uint32_t>(font.atlasWidth()),
			                           static_cast<uint32_t>(font.atlasHeight()));
			guiContext_->markFontUploaded(style);
		}
	}

} // namespace lightGraphics

#endif // LVG_WITH_UI
