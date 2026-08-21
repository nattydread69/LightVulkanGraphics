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

#pragma once

// Layer 3: the Vulkan backend that plays a DrawList back into a command buffer.
//
// This and UiPlatformGlfw.* are the only files under src/ui/ or
// include/lightVulkanGraphics/ui/ permitted to include a Vulkan or GLFW header.
//
// UiRenderer deliberately knows nothing about VkApp: it receives the handles it needs
// through UiRendererCreateInfo and carries its own small memory/command helpers. That
// keeps the dependency running core -> UI, which is what lets the core library own and
// drive a UiRenderer without creating a CMake target cycle (see CMakeLists.txt).

#include <lightVulkanGraphics/ui/DrawList.h>
#include <lightVulkanGraphics/ui/Types.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lightGraphics::ui {

class Font;

struct UiRendererCreateInfo {
	VkDevice device = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkQueue graphicsQueue = VK_NULL_HANDLE;

	// The scene's render pass and subpass. The UI draws inside this pass, after the
	// scene, so it must be created against the same colour format and sample count.
	VkRenderPass renderPass = VK_NULL_HANDLE;
	std::uint32_t subpass = 0;
	VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;

	// One vertex/index buffer set is kept per frame in flight.
	std::uint32_t framesInFlight = 2;

	// Already-resolved .spv paths. Resolution is the caller's job -- the core library
	// has findShaderPath() and its documented search order, and duplicating that here
	// would give the project a second, divergent shader-lookup mechanism.
	std::string vertexShaderPath;
	std::string fragmentShaderPath;
};

class UiRenderer {
public:
	UiRenderer();
	~UiRenderer();

	UiRenderer(const UiRenderer&) = delete;
	UiRenderer& operator=(const UiRenderer&) = delete;

	// Throws std::runtime_error on any failure. `font` must already be baked; its atlas
	// is uploaded here and its dimensions are read from the Font rather than assumed --
	// Font retries at 1024x1024 when the requested atlas cannot fit every glyph, so a
	// hardcoded 512 would silently truncate the upload at larger bake sizes.
	void init(const UiRendererCreateInfo& createInfo, const Font& font);
	void destroy();
	bool isInitialised() const { return m_device != VK_NULL_HANDLE; }

	// Re-uploads the atlas after a content-scale rebake. Safe to call between frames
	// only -- it waits for the device to go idle.
	void rebuildAtlas(const Font& font);

	// The scene render pass is destroyed and recreated on every swapchain resize (see
	// VkApp::recreateSwapChain), which invalidates any pipeline built against it. The
	// owner must call this with the new handle before the next record().
	void onRenderPassRecreated(VkRenderPass renderPass);

	// Plays `list` into `cmd`, which must already be inside the render pass this
	// renderer was created against. `frameIndex` selects the per-frame buffer set and
	// must be the frame-in-flight index whose fence the caller has already waited on --
	// not the swapchain image index. Buffer growth destroys and recreates immediately,
	// with no deferred-deletion queue, and that is only safe because of this.
	//
	// `framebufferExtent` is physical pixels (viewport and scissor). `logicalSize` is
	// the logical-pixel coordinate space the DrawList's vertices are in; pass {0,0} to
	// treat it as identical to framebufferExtent.
	void record(VkCommandBuffer cmd, const DrawList& list, std::uint32_t frameIndex,
	            VkExtent2D framebufferExtent, Vec2 logicalSize = {});

	// ---- consumer-supplied textures (docs/gui/05-widgets.md, "Image") ----
	// Uploads an RGBA8 pixel buffer (row-major, no padding between rows -- `width *
	// height * 4` bytes) as a new sampled image and returns a TextureId a DrawList can
	// pass to addImage(). Synchronous, like createAtlas(): safe to call at any point
	// between frames (not mid-record()), same restriction as rebuildAtlas(). Throws
	// std::runtime_error if more than kMaxRegisteredTextures are currently registered --
	// see that constant's comment for why this backend caps it rather than growing the
	// descriptor pool on demand.
	TextureId registerTexture(const std::uint8_t* rgbaPixels, std::uint32_t width, std::uint32_t height);
	// Waits for the device to go idle before destroying anything -- the same safety
	// requirement rebuildAtlas() already documents: a descriptor set pointing at this
	// texture's VkImageView may still be referenced by a command buffer a previous
	// frame submitted, and destroying the image out from under that is a use-after-free
	// on the GPU. Safe to call with an id that is already unregistered (a no-op) or was
	// never valid (also a no-op) -- not every caller tracks registration state itself.
	void unregisterTexture(TextureId id);

	// Maps a DrawList clip rect (logical pixels) onto a Vulkan scissor (physical
	// pixels), clamped to the framebuffer. Public and static purely so it can be tested
	// headlessly: it is pure arithmetic, it needs no device, and it is the single most
	// likely source of validation errors in this backend, so it earns direct coverage
	// rather than being inferred from a clean validation run.
	static VkRect2D clampToFramebuffer(const Rect& clip, VkExtent2D framebuffer,
	                                    float scaleX = 1.0f, float scaleY = 1.0f);

private:
	struct FrameBuffers {
		VkBuffer vertexBuffer = VK_NULL_HANDLE;
		VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
		void* vertexMapped = nullptr;
		VkDeviceSize vertexCapacity = 0;

		VkBuffer indexBuffer = VK_NULL_HANDLE;
		VkDeviceMemory indexMemory = VK_NULL_HANDLE;
		void* indexMapped = nullptr;
		VkDeviceSize indexCapacity = 0;
	};

	struct PushConstants {
		float scale[2];
		float translate[2];
	};
	static_assert(sizeof(PushConstants) == 16, "UI push constants must be 16 bytes");

	// A consumer-registered texture (registerTexture()/unregisterTexture()) -- deliberately
	// its own small struct rather than sharing the atlas's m_atlasImage/m_atlasMemory/
	// m_atlasView members: the atlas is exactly one image with its own rebuild lifecycle
	// (rebuildAtlas(), tied to DPI changes), while these are added and removed
	// individually and arbitrarily by a consumer, so giving them separate identity here
	// avoids the atlas's own code ever needing to reason about "or maybe N of these".
	struct RegisteredTexture {
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	};

	// Sized into the descriptor pool up front (createDescriptorSet()) rather than grown
	// on demand: this backend targets a modest number of static UI images (legends,
	// icons, thumbnails) registered once near startup, not a dynamic, high-churn texture
	// stream -- growing the pool at runtime would mean either over-allocating "just in
	// case" or a pool-recreation path this feature doesn't otherwise need. Raise this if
	// a real use case needs more; nothing else here assumes this exact value.
	static constexpr std::uint32_t kMaxRegisteredTextures = 64;

	void createPipeline();
	void destroyPipeline();
	void createAtlas(const Font& font);
	void destroyAtlas();
	void createDescriptorSet();
	void destroyRegisteredTexture(RegisteredTexture&);
	void ensureCapacity(FrameBuffers& buffers, std::size_t vertexCount, std::size_t indexCount);
	void destroyFrameBuffers(FrameBuffers& buffers);

	std::uint32_t findMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
	void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
	                   VkBuffer& outBuffer, VkDeviceMemory& outMemory) const;
	VkCommandBuffer beginSingleTimeCommands() const;
	void endSingleTimeCommands(VkCommandBuffer cmd) const;
	VkShaderModule createShaderModule(const std::string& spvPath) const;

	VkDevice m_device = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkCommandPool m_commandPool = VK_NULL_HANDLE;
	VkQueue m_graphicsQueue = VK_NULL_HANDLE;
	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	std::uint32_t m_subpass = 0;
	VkSampleCountFlagBits m_sampleCount = VK_SAMPLE_COUNT_1_BIT;

	std::string m_vertexShaderPath;
	std::string m_fragmentShaderPath;

	VkPipeline m_pipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

	VkImage m_atlasImage = VK_NULL_HANDLE;
	VkDeviceMemory m_atlasMemory = VK_NULL_HANDLE;
	VkImageView m_atlasView = VK_NULL_HANDLE;
	// Shared by the atlas AND every registered texture (registerTexture()) -- filtering/
	// addressing are not format-specific, and CLAMP_TO_EDGE + LINEAR is exactly what a
	// legend/icon/thumbnail wants too, so a second sampler per texture would be pure
	// duplication.
	VkSampler m_atlasSampler = VK_NULL_HANDLE;

	std::unordered_map<TextureId, RegisteredTexture> m_textures;
	TextureId m_nextTextureId = 1;   // 0 is kAtlasTextureId (Types.h) -- never handed out

	std::vector<FrameBuffers> m_frames;
};

} // namespace lightGraphics::ui
