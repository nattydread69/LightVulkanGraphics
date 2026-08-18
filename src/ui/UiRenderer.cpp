#include "UiRenderer.h"

#include <lightVulkanGraphics/ui/Font.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace lightGraphics::ui {

namespace {

void check(VkResult result, const char* what) {
	if (result != VK_SUCCESS) {
		throw std::runtime_error(std::string("UiRenderer: ") + what +
		                          " failed (VkResult " + std::to_string(static_cast<int>(result)) + ")");
	}
}

} // namespace

// A negative origin must clamp to zero AND shrink the extent by the same amount, or the
// scissor covers more than it should and the validation layers reject the offset
// outright. Clamping the edges rather than the origin+extent pair gets that right by
// construction: `left` moving up to 0 leaves `right` untouched, so the width shrinks on
// its own. Comparisons are written in the `!(a > b)` form so NaN clip rects fall out as
// empty instead of reaching an undefined float->int conversion.
VkRect2D UiRenderer::clampToFramebuffer(const Rect& clip, VkExtent2D framebuffer, float scaleX, float scaleY) {
	const VkRect2D empty{ { 0, 0 }, { 0, 0 } };

	float left = clip.left() * scaleX;
	float top = clip.top() * scaleY;
	float right = clip.right() * scaleX;
	float bottom = clip.bottom() * scaleY;

	left = std::max(left, 0.0f);
	top = std::max(top, 0.0f);
	right = std::min(right, static_cast<float>(framebuffer.width));
	bottom = std::min(bottom, static_cast<float>(framebuffer.height));

	if (!(right > left) || !(bottom > top)) {
		return empty;
	}

	auto x = static_cast<std::int32_t>(std::floor(left));
	auto y = static_cast<std::int32_t>(std::floor(top));
	auto r = static_cast<std::int32_t>(std::ceil(right));
	auto b = static_cast<std::int32_t>(std::ceil(bottom));

	// floor/ceil widened the rect; pull it back inside the framebuffer.
	x = std::max(x, 0);
	y = std::max(y, 0);
	r = std::min(r, static_cast<std::int32_t>(framebuffer.width));
	b = std::min(b, static_cast<std::int32_t>(framebuffer.height));

	if (r <= x || b <= y) {
		return empty;
	}

	VkRect2D scissor{};
	scissor.offset = { x, y };
	scissor.extent = { static_cast<std::uint32_t>(r - x), static_cast<std::uint32_t>(b - y) };
	return scissor;
}

UiRenderer::UiRenderer() = default;

UiRenderer::~UiRenderer() {
	destroy();
}

void UiRenderer::init(const UiRendererCreateInfo& createInfo, const Font& font) {
	if (createInfo.device == VK_NULL_HANDLE || createInfo.physicalDevice == VK_NULL_HANDLE ||
		createInfo.commandPool == VK_NULL_HANDLE || createInfo.graphicsQueue == VK_NULL_HANDLE ||
		createInfo.renderPass == VK_NULL_HANDLE) {
		throw std::runtime_error("UiRenderer::init: incomplete UiRendererCreateInfo");
	}
	if (!font.isBaked()) {
		throw std::runtime_error("UiRenderer::init: font has not been baked");
	}
	if (createInfo.framesInFlight == 0) {
		throw std::runtime_error("UiRenderer::init: framesInFlight must be non-zero");
	}

	destroy();

	m_device = createInfo.device;
	m_physicalDevice = createInfo.physicalDevice;
	m_commandPool = createInfo.commandPool;
	m_graphicsQueue = createInfo.graphicsQueue;
	m_renderPass = createInfo.renderPass;
	m_subpass = createInfo.subpass;
	m_sampleCount = createInfo.sampleCount;
	m_vertexShaderPath = createInfo.vertexShaderPath;
	m_fragmentShaderPath = createInfo.fragmentShaderPath;

	m_frames.assign(createInfo.framesInFlight, FrameBuffers{});

	try {
		createAtlas(font);
		createDescriptorSet();
		createPipeline();
	} catch (...) {
		destroy();
		throw;
	}
}

void UiRenderer::destroy() {
	if (m_device == VK_NULL_HANDLE) {
		m_frames.clear();
		return;
	}

	for (FrameBuffers& frame : m_frames) {
		destroyFrameBuffers(frame);
	}
	m_frames.clear();

	destroyPipeline();

	// Before the pool itself: each entry's image/view/memory is NOT owned by the pool
	// (only its descriptor set is), so those need their own explicit destruction
	// regardless of what happens to the pool below.
	for (auto& [id, tex] : m_textures) {
		destroyRegisteredTexture(tex);
	}
	m_textures.clear();

	if (m_descriptorPool != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
		m_descriptorPool = VK_NULL_HANDLE;
		m_descriptorSet = VK_NULL_HANDLE;
	}
	if (m_descriptorSetLayout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
		m_descriptorSetLayout = VK_NULL_HANDLE;
	}

	destroyAtlas();

	m_device = VK_NULL_HANDLE;
	m_physicalDevice = VK_NULL_HANDLE;
	m_commandPool = VK_NULL_HANDLE;
	m_graphicsQueue = VK_NULL_HANDLE;
	m_renderPass = VK_NULL_HANDLE;
}

void UiRenderer::destroyPipeline() {
	if (m_device == VK_NULL_HANDLE) {
		return;
	}
	if (m_pipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(m_device, m_pipeline, nullptr);
		m_pipeline = VK_NULL_HANDLE;
	}
	if (m_pipelineLayout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
		m_pipelineLayout = VK_NULL_HANDLE;
	}
}

void UiRenderer::destroyAtlas() {
	if (m_device == VK_NULL_HANDLE) {
		return;
	}
	if (m_atlasSampler != VK_NULL_HANDLE) {
		vkDestroySampler(m_device, m_atlasSampler, nullptr);
		m_atlasSampler = VK_NULL_HANDLE;
	}
	if (m_atlasView != VK_NULL_HANDLE) {
		vkDestroyImageView(m_device, m_atlasView, nullptr);
		m_atlasView = VK_NULL_HANDLE;
	}
	if (m_atlasImage != VK_NULL_HANDLE) {
		vkDestroyImage(m_device, m_atlasImage, nullptr);
		m_atlasImage = VK_NULL_HANDLE;
	}
	if (m_atlasMemory != VK_NULL_HANDLE) {
		vkFreeMemory(m_device, m_atlasMemory, nullptr);
		m_atlasMemory = VK_NULL_HANDLE;
	}
}

void UiRenderer::destroyFrameBuffers(FrameBuffers& frame) {
	if (frame.vertexMapped != nullptr) {
		vkUnmapMemory(m_device, frame.vertexMemory);
		frame.vertexMapped = nullptr;
	}
	if (frame.vertexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(m_device, frame.vertexBuffer, nullptr);
		frame.vertexBuffer = VK_NULL_HANDLE;
	}
	if (frame.vertexMemory != VK_NULL_HANDLE) {
		vkFreeMemory(m_device, frame.vertexMemory, nullptr);
		frame.vertexMemory = VK_NULL_HANDLE;
	}
	frame.vertexCapacity = 0;

	if (frame.indexMapped != nullptr) {
		vkUnmapMemory(m_device, frame.indexMemory);
		frame.indexMapped = nullptr;
	}
	if (frame.indexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(m_device, frame.indexBuffer, nullptr);
		frame.indexBuffer = VK_NULL_HANDLE;
	}
	if (frame.indexMemory != VK_NULL_HANDLE) {
		vkFreeMemory(m_device, frame.indexMemory, nullptr);
		frame.indexMemory = VK_NULL_HANDLE;
	}
	frame.indexCapacity = 0;
}

std::uint32_t UiRenderer::findMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
	VkPhysicalDeviceMemoryProperties memoryProperties{};
	vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);
	for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
		if ((typeFilter & (1u << i)) &&
			(memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}
	throw std::runtime_error("UiRenderer: no suitable memory type");
}

void UiRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                               VkBuffer& outBuffer, VkDeviceMemory& outMemory) const {
	VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	check(vkCreateBuffer(m_device, &bufferInfo, nullptr, &outBuffer), "vkCreateBuffer");

	VkMemoryRequirements requirements{};
	vkGetBufferMemoryRequirements(m_device, outBuffer, &requirements);

	VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	allocInfo.allocationSize = requirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);
	check(vkAllocateMemory(m_device, &allocInfo, nullptr, &outMemory), "vkAllocateMemory");
	check(vkBindBufferMemory(m_device, outBuffer, outMemory, 0), "vkBindBufferMemory");
}

VkCommandBuffer UiRenderer::beginSingleTimeCommands() const {
	VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = m_commandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer cmd = VK_NULL_HANDLE;
	check(vkAllocateCommandBuffers(m_device, &allocInfo, &cmd), "vkAllocateCommandBuffers");

	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	check(vkBeginCommandBuffer(cmd, &beginInfo), "vkBeginCommandBuffer");
	return cmd;
}

void UiRenderer::endSingleTimeCommands(VkCommandBuffer cmd) const {
	check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

	VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	check(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit");
	check(vkQueueWaitIdle(m_graphicsQueue), "vkQueueWaitIdle");

	vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
}

VkShaderModule UiRenderer::createShaderModule(const std::string& spvPath) const {
	std::ifstream file(spvPath, std::ios::binary | std::ios::ate);
	if (!file) {
		throw std::runtime_error("UiRenderer: could not open shader '" + spvPath + "'");
	}
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);
	std::vector<char> code(static_cast<std::size_t>(size));
	if (!file.read(code.data(), size)) {
		throw std::runtime_error("UiRenderer: could not read shader '" + spvPath + "'");
	}

	VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	moduleInfo.codeSize = code.size();
	moduleInfo.pCode = reinterpret_cast<const std::uint32_t*>(code.data());

	VkShaderModule shaderModule = VK_NULL_HANDLE;
	check(vkCreateShaderModule(m_device, &moduleInfo, nullptr, &shaderModule), "vkCreateShaderModule");
	return shaderModule;
}

void UiRenderer::createAtlas(const Font& font) {
	// Read the dimensions from the Font: it retries at 1024x1024 when the requested
	// atlas cannot fit every glyph, so assuming 512 would truncate the upload.
	const auto width = static_cast<std::uint32_t>(font.atlasWidth());
	const auto height = static_cast<std::uint32_t>(font.atlasHeight());
	const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height; // R8: one byte per texel

	if (width == 0 || height == 0 || font.atlasPixels().size() < imageSize) {
		throw std::runtime_error("UiRenderer: font atlas is empty or undersized");
	}

	VkBuffer staging = VK_NULL_HANDLE;
	VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
	createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	             staging, stagingMemory);

	void* mapped = nullptr;
	check(vkMapMemory(m_device, stagingMemory, 0, imageSize, 0, &mapped), "vkMapMemory");
	std::memcpy(mapped, font.atlasPixels().data(), static_cast<std::size_t>(imageSize));
	vkUnmapMemory(m_device, stagingMemory);

	VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = VK_FORMAT_R8_UNORM;
	imageInfo.extent = { width, height, 1 };
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	check(vkCreateImage(m_device, &imageInfo, nullptr, &m_atlasImage), "vkCreateImage");

	VkMemoryRequirements requirements{};
	vkGetImageMemoryRequirements(m_device, m_atlasImage, &requirements);
	VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	allocInfo.allocationSize = requirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	check(vkAllocateMemory(m_device, &allocInfo, nullptr, &m_atlasMemory), "vkAllocateMemory");
	check(vkBindImageMemory(m_device, m_atlasImage, m_atlasMemory, 0), "vkBindImageMemory");

	VkCommandBuffer cmd = beginSingleTimeCommands();

	VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = m_atlasImage;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &barrier);

	VkBufferImageCopy region{};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = { width, height, 1 };
	vkCmdCopyBufferToImage(cmd, staging, m_atlasImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &barrier);

	endSingleTimeCommands(cmd);

	vkDestroyBuffer(m_device, staging, nullptr);
	vkFreeMemory(m_device, stagingMemory, nullptr);

	VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	viewInfo.image = m_atlasImage;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = VK_FORMAT_R8_UNORM;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;
	check(vkCreateImageView(m_device, &viewInfo, nullptr, &m_atlasView), "vkCreateImageView");

	// CLAMP_TO_EDGE matters: with REPEAT a glyph on the atlas edge samples a sliver of
	// the opposite edge and produces a faint speckle that is miserable to track down.
	VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.anisotropyEnable = VK_FALSE;
	samplerInfo.maxAnisotropy = 1.0f;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 0.0f;
	check(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_atlasSampler), "vkCreateSampler");
}

void UiRenderer::createDescriptorSet() {
	VkDescriptorSetLayoutBinding binding{};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &binding;
	check(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout),
	      "vkCreateDescriptorSetLayout");

	// Sized for the atlas's own set PLUS every slot registerTexture() might ever hand
	// out (kMaxRegisteredTextures, UiRenderer.h) -- allocating additional sets from this
	// same pool well after creation (inside registerTexture(), not here) is ordinary
	// Vulkan usage as long as the pool was sized with enough headroom up front, which
	// this is. FREE_DESCRIPTOR_SET_BIT lets unregisterTexture() give its slot back
	// individually rather than every registration permanently consuming pool capacity.
	VkDescriptorPoolSize poolSize{};
	poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSize.descriptorCount = 1 + kMaxRegisteredTextures;

	VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = 1 + kMaxRegisteredTextures;
	check(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool), "vkCreateDescriptorPool");

	VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	allocInfo.descriptorPool = m_descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &m_descriptorSetLayout;
	check(vkAllocateDescriptorSets(m_device, &allocInfo, &m_descriptorSet), "vkAllocateDescriptorSets");

	VkDescriptorImageInfo imageInfo{};
	imageInfo.sampler = m_atlasSampler;
	imageInfo.imageView = m_atlasView;
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	write.dstSet = m_descriptorSet;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &imageInfo;
	vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void UiRenderer::destroyRegisteredTexture(RegisteredTexture& tex) {
	if (m_device == VK_NULL_HANDLE) {
		return;
	}
	// FREE_DESCRIPTOR_SET_BIT (createDescriptorSet()) is what makes this legal --
	// without it, individual sets can only ever be reclaimed by resetting the whole pool.
	if (tex.descriptorSet != VK_NULL_HANDLE) {
		vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &tex.descriptorSet);
		tex.descriptorSet = VK_NULL_HANDLE;
	}
	if (tex.view != VK_NULL_HANDLE) {
		vkDestroyImageView(m_device, tex.view, nullptr);
		tex.view = VK_NULL_HANDLE;
	}
	if (tex.image != VK_NULL_HANDLE) {
		vkDestroyImage(m_device, tex.image, nullptr);
		tex.image = VK_NULL_HANDLE;
	}
	if (tex.memory != VK_NULL_HANDLE) {
		vkFreeMemory(m_device, tex.memory, nullptr);
		tex.memory = VK_NULL_HANDLE;
	}
}

TextureId UiRenderer::registerTexture(const std::uint8_t* rgbaPixels, std::uint32_t width, std::uint32_t height) {
	if (m_device == VK_NULL_HANDLE) {
		throw std::runtime_error("UiRenderer::registerTexture: not initialised");
	}
	if (rgbaPixels == nullptr || width == 0 || height == 0) {
		throw std::runtime_error("UiRenderer::registerTexture: empty pixel buffer");
	}
	if (m_textures.size() >= kMaxRegisteredTextures) {
		throw std::runtime_error("UiRenderer::registerTexture: kMaxRegisteredTextures (" +
		                          std::to_string(kMaxRegisteredTextures) +
		                          ") are already registered -- see UiRenderer.h's comment "
		                          "on that constant");
	}

	// Same staging-buffer-then-copy-then-barrier shape as createAtlas() (this file,
	// above), duplicated rather than shared -- see RegisteredTexture's header comment on
	// why this and the atlas are kept structurally separate. RGBA8 here vs. the atlas's
	// R8 is the only format difference.
	const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

	VkBuffer staging = VK_NULL_HANDLE;
	VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
	createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	             staging, stagingMemory);

	void* mapped = nullptr;
	check(vkMapMemory(m_device, stagingMemory, 0, imageSize, 0, &mapped), "vkMapMemory");
	std::memcpy(mapped, rgbaPixels, static_cast<std::size_t>(imageSize));
	vkUnmapMemory(m_device, stagingMemory);

	RegisteredTexture tex;
	try {
		VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		imageInfo.extent = { width, height, 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		check(vkCreateImage(m_device, &imageInfo, nullptr, &tex.image), "vkCreateImage");

		VkMemoryRequirements requirements{};
		vkGetImageMemoryRequirements(m_device, tex.image, &requirements);
		VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		allocInfo.allocationSize = requirements.size;
		allocInfo.memoryTypeIndex =
			findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		check(vkAllocateMemory(m_device, &allocInfo, nullptr, &tex.memory), "vkAllocateMemory");
		check(vkBindImageMemory(m_device, tex.image, tex.memory, 0), "vkBindImageMemory");

		VkCommandBuffer cmd = beginSingleTimeCommands();

		VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = tex.image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                     0, 0, nullptr, 0, nullptr, 1, &barrier);

		VkBufferImageCopy region{};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = 1;
		region.imageExtent = { width, height, 1 };
		vkCmdCopyBufferToImage(cmd, staging, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		                     0, 0, nullptr, 0, nullptr, 1, &barrier);

		endSingleTimeCommands(cmd);

		VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		viewInfo.image = tex.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.layerCount = 1;
		check(vkCreateImageView(m_device, &viewInfo, nullptr, &tex.view), "vkCreateImageView");

		VkDescriptorSetAllocateInfo dsAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		dsAllocInfo.descriptorPool = m_descriptorPool;
		dsAllocInfo.descriptorSetCount = 1;
		dsAllocInfo.pSetLayouts = &m_descriptorSetLayout;
		check(vkAllocateDescriptorSets(m_device, &dsAllocInfo, &tex.descriptorSet), "vkAllocateDescriptorSets");

		// Reuses m_atlasSampler -- see its own comment (UiRenderer.h) on why one sampler
		// serves the atlas and every registered texture.
		VkDescriptorImageInfo descImageInfo{};
		descImageInfo.sampler = m_atlasSampler;
		descImageInfo.imageView = tex.view;
		descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.dstSet = tex.descriptorSet;
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &descImageInfo;
		vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
	} catch (...) {
		vkDestroyBuffer(m_device, staging, nullptr);
		vkFreeMemory(m_device, stagingMemory, nullptr);
		destroyRegisteredTexture(tex);
		throw;
	}

	vkDestroyBuffer(m_device, staging, nullptr);
	vkFreeMemory(m_device, stagingMemory, nullptr);

	TextureId id = m_nextTextureId++;
	m_textures.emplace(id, tex);
	return id;
}

void UiRenderer::unregisterTexture(TextureId id) {
	auto it = m_textures.find(id);
	if (it == m_textures.end()) {
		return;   // already gone, or never valid -- see this method's header comment
	}
	// See this method's header comment (UiRenderer.h): a previous frame's already-
	// submitted command buffer may still be reading the descriptor set this texture
	// backs, same reason rebuildAtlas() waits before touching the atlas image.
	vkDeviceWaitIdle(m_device);
	destroyRegisteredTexture(it->second);
	m_textures.erase(it);
}

void UiRenderer::createPipeline() {
	VkShaderModule vertexModule = createShaderModule(m_vertexShaderPath);
	VkShaderModule fragmentModule = VK_NULL_HANDLE;
	try {
		fragmentModule = createShaderModule(m_fragmentShaderPath);
	} catch (...) {
		vkDestroyShaderModule(m_device, vertexModule, nullptr);
		throw;
	}

	VkPushConstantRange pushRange{};
	pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pushRange.offset = 0;
	pushRange.size = sizeof(PushConstants);

	VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &m_descriptorSetLayout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &pushRange;

	VkVertexInputBindingDescription vertexBinding{};
	vertexBinding.binding = 0;
	vertexBinding.stride = sizeof(UiVertex);
	vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	// R8G8B8A8_UNORM as a vertex attribute is normalised to a vec4 by the hardware, so
	// the shader needs no unpacking.
	VkVertexInputAttributeDescription attributes[3]{};
	attributes[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, pos) };
	attributes[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, uv) };
	attributes[2] = { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(UiVertex, color) };

	VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &vertexBinding;
	vertexInput.vertexAttributeDescriptionCount = 3;
	vertexInput.pVertexAttributeDescriptions = attributes;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	// Viewport and scissor are dynamic; the counts still have to be declared.
	VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterization{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rasterization.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization.cullMode = VK_CULL_MODE_NONE; // draw-list triangles are not consistently wound
	rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterization.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisample.rasterizationSamples = m_sampleCount;

	VkPipelineColorBlendAttachmentState blendAttachment{};
	blendAttachment.blendEnable = VK_TRUE;
	blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo colorBlend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	colorBlend.attachmentCount = 1;
	colorBlend.pAttachments = &blendAttachment;

	// The scene subpass has a depth attachment, so this state must be supplied even
	// though the UI neither tests nor writes depth.
	VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	depthStencil.depthTestEnable = VK_FALSE;
	depthStencil.depthWriteEnable = VK_FALSE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vertexModule;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fragmentModule;
	stages[1].pName = "main";

	try {
		check(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout),
		      "vkCreatePipelineLayout");

		VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = stages;
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterization;
		pipelineInfo.pMultisampleState = &multisample;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlend;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = m_pipelineLayout;
		pipelineInfo.renderPass = m_renderPass;
		pipelineInfo.subpass = m_subpass;
		check(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline),
		      "vkCreateGraphicsPipelines");
	} catch (...) {
		vkDestroyShaderModule(m_device, vertexModule, nullptr);
		vkDestroyShaderModule(m_device, fragmentModule, nullptr);
		throw;
	}

	vkDestroyShaderModule(m_device, vertexModule, nullptr);
	vkDestroyShaderModule(m_device, fragmentModule, nullptr);
}

void UiRenderer::onRenderPassRecreated(VkRenderPass renderPass) {
	if (m_device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE) {
		return;
	}
	m_renderPass = renderPass;
	destroyPipeline();
	createPipeline();
}

void UiRenderer::rebuildAtlas(const Font& font) {
	if (m_device == VK_NULL_HANDLE) {
		return;
	}
	if (!font.isBaked()) {
		throw std::runtime_error("UiRenderer::rebuildAtlas: font has not been baked");
	}

	// The descriptor set points at the old image/sampler, so nothing may still be
	// reading them when they are destroyed.
	vkDeviceWaitIdle(m_device);

	destroyAtlas();
	createAtlas(font);

	VkDescriptorImageInfo imageInfo{};
	imageInfo.sampler = m_atlasSampler;
	imageInfo.imageView = m_atlasView;
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	write.dstSet = m_descriptorSet;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &imageInfo;
	vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void UiRenderer::ensureCapacity(FrameBuffers& frame, std::size_t vertexCount, std::size_t indexCount) {
	const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>(vertexCount) * sizeof(UiVertex);
	const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(indexCount) * sizeof(std::uint32_t);

	// Grow to 1.5x the requirement with a 4096-vertex floor, so a panel that creeps up
	// in size does not reallocate every frame. This deliberately differs from the
	// exact-fit growth in VkApp::ensureInstanceBufferSizeForFrame; see
	// docs/gui/02-rendering.md.
	if (frame.vertexCapacity < vertexBytes) {
		if (frame.vertexMapped != nullptr) {
			vkUnmapMemory(m_device, frame.vertexMemory);
			frame.vertexMapped = nullptr;
		}
		if (frame.vertexBuffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(m_device, frame.vertexBuffer, nullptr);
			frame.vertexBuffer = VK_NULL_HANDLE;
		}
		if (frame.vertexMemory != VK_NULL_HANDLE) {
			vkFreeMemory(m_device, frame.vertexMemory, nullptr);
			frame.vertexMemory = VK_NULL_HANDLE;
		}

		const VkDeviceSize newSize = std::max<VkDeviceSize>(vertexBytes * 3 / 2, 4096 * sizeof(UiVertex));
		createBuffer(newSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		             frame.vertexBuffer, frame.vertexMemory);
		check(vkMapMemory(m_device, frame.vertexMemory, 0, newSize, 0, &frame.vertexMapped), "vkMapMemory");
		frame.vertexCapacity = newSize;
	}

	if (frame.indexCapacity < indexBytes) {
		if (frame.indexMapped != nullptr) {
			vkUnmapMemory(m_device, frame.indexMemory);
			frame.indexMapped = nullptr;
		}
		if (frame.indexBuffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(m_device, frame.indexBuffer, nullptr);
			frame.indexBuffer = VK_NULL_HANDLE;
		}
		if (frame.indexMemory != VK_NULL_HANDLE) {
			vkFreeMemory(m_device, frame.indexMemory, nullptr);
			frame.indexMemory = VK_NULL_HANDLE;
		}

		const VkDeviceSize newSize = std::max<VkDeviceSize>(indexBytes * 3 / 2, 4096 * sizeof(std::uint32_t));
		createBuffer(newSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		             frame.indexBuffer, frame.indexMemory);
		check(vkMapMemory(m_device, frame.indexMemory, 0, newSize, 0, &frame.indexMapped), "vkMapMemory");
		frame.indexCapacity = newSize;
	}
}

void UiRenderer::record(VkCommandBuffer cmd, const DrawList& list, std::uint32_t frameIndex,
                         VkExtent2D framebufferExtent, Vec2 logicalSize) {
	if (m_device == VK_NULL_HANDLE || m_pipeline == VK_NULL_HANDLE) {
		return;
	}
	if (list.indices().empty() || list.vertices().empty()) {
		return;
	}
	if (framebufferExtent.width == 0 || framebufferExtent.height == 0) {
		return;
	}
	if (frameIndex >= m_frames.size()) {
		throw std::runtime_error("UiRenderer::record: frameIndex out of range");
	}

	// DrawList vertices are in logical pixels; the viewport and scissor are physical.
	float logicalWidth = (logicalSize.x > 0.0f) ? logicalSize.x : static_cast<float>(framebufferExtent.width);
	float logicalHeight = (logicalSize.y > 0.0f) ? logicalSize.y : static_cast<float>(framebufferExtent.height);
	const float scissorScaleX = static_cast<float>(framebufferExtent.width) / logicalWidth;
	const float scissorScaleY = static_cast<float>(framebufferExtent.height) / logicalHeight;

	FrameBuffers& frame = m_frames[frameIndex];
	ensureCapacity(frame, list.vertices().size(), list.indices().size());

	std::memcpy(frame.vertexMapped, list.vertices().data(), list.vertices().size() * sizeof(UiVertex));
	std::memcpy(frame.indexMapped, list.indices().data(), list.indices().size() * sizeof(std::uint32_t));

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &frame.vertexBuffer, &offset);
	vkCmdBindIndexBuffer(cmd, frame.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

	// Pixel space -> clip space. Vulkan clip space already has +Y down, so no flip.
	PushConstants push{};
	push.scale[0] = 2.0f / logicalWidth;
	push.scale[1] = 2.0f / logicalHeight;
	push.translate[0] = -1.0f;
	push.translate[1] = -1.0f;
	vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(framebufferExtent.width);
	viewport.height = static_cast<float>(framebufferExtent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	// Bound once before the loop when there is nothing registered (the overwhelmingly
	// common case: every DrawCmd::textureId is kAtlasTextureId, so every iteration below
	// would just rebind the identical set) would be a premature optimisation for a
	// backend whose whole per-frame draw-call count is already in the dozens -- binding
	// per command, unconditionally, keeps this loop the single place that decides which
	// image a draw call samples, rather than splitting that decision between here and a
	// bind call above it.
	for (const DrawCmd& drawCmd : list.commands()) {
		if (drawCmd.indexCount == 0) {
			continue;
		}
		VkRect2D scissor = clampToFramebuffer(drawCmd.clipRect, framebufferExtent, scissorScaleX, scissorScaleY);
		if (scissor.extent.width == 0 || scissor.extent.height == 0) {
			continue;
		}

		VkDescriptorSet set = m_descriptorSet;   // kAtlasTextureId, or an unrecognised id
		if (drawCmd.textureId != kAtlasTextureId) {
			auto it = m_textures.find(drawCmd.textureId);
			if (it != m_textures.end()) {
				set = it->second.descriptorSet;
			}
			// else: falls back to the atlas set rather than binding VK_NULL_HANDLE (a
			// validation error) for a stale or unregistered id -- the draw samples the
			// wrong image instead of crashing, matching this backend's "a widget must
			// never be able to take down a running simulation" policy
			// (docs/gui/07-public-api.md, "Error handling policy").
		}
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
		                        0, 1, &set, 0, nullptr);

		vkCmdSetScissor(cmd, 0, 1, &scissor);
		vkCmdDrawIndexed(cmd, drawCmd.indexCount, 1, drawCmd.indexOffset, 0, 0);
	}
}

} // namespace lightGraphics::ui
