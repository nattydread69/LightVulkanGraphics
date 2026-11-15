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

// 2D textures (3.4) and the descriptor sets that bind them to Textured / TerrainSplat materials.
//
// State lives in a side table keyed by the VkApp (like bricked volumes), so VkApp's data layout does not grow.

#include "VkApp.h"
#include "ImageLoad.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace lightGraphics
{

namespace
{

struct Texture2DState
{
	std::uint32_t generation = 1;
	bool alive = false;
	Texture2DDescription description;
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;
	std::uint32_t mipLevels = 1;
};

struct AppTexture2D
{
	std::vector<Texture2DState> textures;
	std::vector<std::uint32_t> free;
	VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
	VkDescriptorPool pool = VK_NULL_HANDLE;
	// A 1x1 white texel bound to unused material slots.
	Texture2DHandle white;
};

constexpr std::uint32_t kMaterialTextureSlots = 10;
constexpr std::uint32_t kMaxMaterialSets = 1024;

std::mutex& texture2DMutex()
{
	static std::mutex m;
	return m;
}

std::unordered_map<const void*, AppTexture2D>& texture2DTables()
{
	static std::unordered_map<const void*, AppTexture2D> t;
	return t;
}

void check(VkResult result, const char* operation)
{
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error(std::string(operation) + " (VkResult " + std::to_string(static_cast<int>(result)) + ")");
	}
}

VkFormat vulkanFormat(Texture2DFormat format)
{
	return format == Texture2DFormat::RGBA8_SRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
}

std::uint32_t mipCount(const Texture2DDescription& d)
{
	if (!d.generateMipmaps)
	{
		return 1;
	}
	std::uint32_t levels = 1;
	for (std::uint32_t size = std::max(d.width, d.height); size > 1; size >>= 1)
	{
		++levels;
	}
	return levels;
}

float srgbToLinear(std::uint8_t v)
{
	const float c = static_cast<float>(v) / 255.0f;
	return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

std::uint8_t linearToSrgb(float c)
{
	c = std::clamp(c, 0.0f, 1.0f);
	const float v = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
	return static_cast<std::uint8_t>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
}

// Box-filtered mip chain; level 0 is the source. sRGB colour is averaged in linear light, alpha and linear data directly.
std::vector<std::vector<std::uint8_t>> buildMipChain(const Texture2DDescription& d, const std::uint8_t* rgba, std::uint32_t levels)
{
	std::vector<std::vector<std::uint8_t>> chain;
	chain.emplace_back(rgba, rgba + static_cast<std::size_t>(d.width) * d.height * 4);
	std::uint32_t w = d.width;
	std::uint32_t h = d.height;
	const bool srgb = d.format == Texture2DFormat::RGBA8_SRGB;
	std::array<float, 256> toLinear {};
	for (int i = 0; i < 256; ++i)
	{
		toLinear[static_cast<std::size_t>(i)] = srgbToLinear(static_cast<std::uint8_t>(i));
	}
	for (std::uint32_t level = 1; level < levels; ++level)
	{
		const std::vector<std::uint8_t>& src = chain.back();
		const std::uint32_t nw = std::max(1u, w / 2);
		const std::uint32_t nh = std::max(1u, h / 2);
		std::vector<std::uint8_t> dst(static_cast<std::size_t>(nw) * nh * 4);
		for (std::uint32_t y = 0; y < nh; ++y)
		{
			for (std::uint32_t x = 0; x < nw; ++x)
			{
				float sum[4] = {0, 0, 0, 0};
				for (std::uint32_t dy = 0; dy < 2; ++dy)
				{
					for (std::uint32_t dx = 0; dx < 2; ++dx)
					{
						const std::uint32_t sx = std::min(w - 1, x * 2 + dx);
						const std::uint32_t sy = std::min(h - 1, y * 2 + dy);
						const std::uint8_t* p = &src[(static_cast<std::size_t>(sy) * w + sx) * 4];
						for (int c = 0; c < 3; ++c)
						{
							sum[c] += srgb ? toLinear[p[c]] : static_cast<float>(p[c]);
						}
						sum[3] += static_cast<float>(p[3]);
					}
				}
				std::uint8_t* out = &dst[(static_cast<std::size_t>(y) * nw + x) * 4];
				for (int c = 0; c < 3; ++c)
				{
					out[c] = srgb ? linearToSrgb(sum[c] * 0.25f)
								  : static_cast<std::uint8_t>(std::clamp(sum[c] * 0.25f + 0.5f, 0.0f, 255.0f));
				}
				out[3] = static_cast<std::uint8_t>(std::clamp(sum[3] * 0.25f + 0.5f, 0.0f, 255.0f));
			}
		}
		chain.push_back(std::move(dst));
		w = nw;
		h = nh;
	}
	return chain;
}

AppTexture2D& tableFor(const void* app)
{
	return texture2DTables()[app];
}

Texture2DState& resolve(AppTexture2D& table, Texture2DHandle handle)
{
	if (handle.index >= table.textures.size() || !table.textures[handle.index].alive ||
		table.textures[handle.index].generation != handle.generation)
	{
		throw std::invalid_argument("Texture2D handle is invalid or stale");
	}
	return table.textures[handle.index];
}

}

VkDescriptorSetLayout VkApp::materialTextureSetLayout()
{
	std::lock_guard<std::mutex> lock(texture2DMutex());
	AppTexture2D& table = tableFor(this);
	if (table.setLayout == VK_NULL_HANDLE)
	{
		std::array<VkDescriptorSetLayoutBinding, kMaterialTextureSlots> bindings {};
		for (std::uint32_t i = 0; i < kMaterialTextureSlots; ++i)
		{
			bindings[i].binding = i;
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		}
		VkDescriptorSetLayoutCreateInfo info {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		info.bindingCount = static_cast<std::uint32_t>(bindings.size());
		info.pBindings = bindings.data();
		check(vkCreateDescriptorSetLayout(device_, &info, nullptr, &table.setLayout), "Failed to create the material texture set layout");
		VkDescriptorPoolSize size {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxMaterialSets * kMaterialTextureSlots};
		VkDescriptorPoolCreateInfo poolInfo {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = kMaxMaterialSets;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &size;
		check(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &table.pool), "Failed to create the material texture descriptor pool");
	}
	return table.setLayout;
}

Texture2DHandle VkApp::createTexture2D(const Texture2DDescription& description, const void* rgba8, std::size_t byteSize)
{
	if (description.width == 0 || description.height == 0 || description.width > 16384 || description.height > 16384)
	{
		throw std::invalid_argument("Texture2D size must be 1..16384 in each dimension");
	}
	if (rgba8 == nullptr || byteSize != static_cast<std::size_t>(description.width) * description.height * 4)
	{
		throw std::invalid_argument("Texture2D data must be width * height * 4 bytes");
	}
	const VkFormat format = vulkanFormat(description.format);
	VkFormatProperties properties {};
	vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &properties);
	const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
	if ((properties.optimalTilingFeatures & required) != required)
	{
		throw std::runtime_error("The Vulkan device cannot linearly sample the requested Texture2D format");
	}
	const std::uint32_t levels = mipCount(description);

	Texture2DState state;
	state.description = description;
	state.mipLevels = levels;
	try
	{
		VkImageCreateInfo imageInfo {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent = {description.width, description.height, 1};
		imageInfo.mipLevels = levels;
		imageInfo.arrayLayers = 1;
		imageInfo.format = format;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		check(vkCreateImage(device_, &imageInfo, nullptr, &state.image), "Failed to create the Texture2D image");
		VkMemoryRequirements requirements {};
		vkGetImageMemoryRequirements(device_, state.image, &requirements);
		VkMemoryAllocateInfo allocation {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		allocation.allocationSize = requirements.size;
		allocation.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		check(vkAllocateMemory(device_, &allocation, nullptr, &state.memory), "Failed to allocate Texture2D memory");
		check(vkBindImageMemory(device_, state.image, state.memory, 0), "Failed to bind Texture2D memory");
		VkImageViewCreateInfo viewInfo {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		viewInfo.image = state.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.levelCount = levels;
		viewInfo.subresourceRange.layerCount = 1;
		check(vkCreateImageView(device_, &viewInfo, nullptr, &state.view), "Failed to create the Texture2D view");
		VkSamplerCreateInfo samplerInfo {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		const VkSamplerAddressMode mode = description.repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeU = mode;
		samplerInfo.addressModeV = mode;
		samplerInfo.addressModeW = mode;
		samplerInfo.maxLod = static_cast<float>(levels);
		check(vkCreateSampler(device_, &samplerInfo, nullptr, &state.sampler), "Failed to create the Texture2D sampler");
	}
	catch (...)
	{
		if (state.sampler != VK_NULL_HANDLE) vkDestroySampler(device_, state.sampler, nullptr);
		if (state.view != VK_NULL_HANDLE) vkDestroyImageView(device_, state.view, nullptr);
		if (state.image != VK_NULL_HANDLE) vkDestroyImage(device_, state.image, nullptr);
		if (state.memory != VK_NULL_HANDLE) vkFreeMemory(device_, state.memory, nullptr);
		throw;
	}

	Texture2DHandle handle;
	{
		std::lock_guard<std::mutex> lock(texture2DMutex());
		AppTexture2D& table = tableFor(this);
		std::uint32_t index = 0;
		if (table.free.empty())
		{
			index = static_cast<std::uint32_t>(table.textures.size());
			table.textures.emplace_back();
		}
		else
		{
			index = table.free.back();
			table.free.pop_back();
		}
		const std::uint32_t generation = table.textures[index].generation;
		table.textures[index] = state;
		table.textures[index].generation = generation;
		table.textures[index].alive = true;
		handle = {index, generation};
	}
	try
	{
		updateTexture2D(handle, rgba8, byteSize);
	}
	catch (...)
	{
		destroyTexture2D(handle);
		throw;
	}
	return handle;
}

void VkApp::updateTexture2D(Texture2DHandle handle, const void* rgba8, std::size_t byteSize)
{
	Texture2DState state;
	{
		std::lock_guard<std::mutex> lock(texture2DMutex());
		state = resolve(tableFor(this), handle);
	}
	const Texture2DDescription& d = state.description;
	if (rgba8 == nullptr || byteSize != static_cast<std::size_t>(d.width) * d.height * 4)
	{
		throw std::invalid_argument("Texture2D update must supply width * height * 4 bytes");
	}
	const auto chain = buildMipChain(d, static_cast<const std::uint8_t*>(rgba8), state.mipLevels);
	std::size_t total = 0;
	std::vector<VkBufferImageCopy> regions;
	std::uint32_t w = d.width;
	std::uint32_t h = d.height;
	for (std::uint32_t level = 0; level < state.mipLevels; ++level)
	{
		VkBufferImageCopy region {};
		region.bufferOffset = total;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = level;
		region.imageSubresource.layerCount = 1;
		region.imageExtent = {w, h, 1};
		regions.push_back(region);
		total += chain[level].size();
		w = std::max(1u, w / 2);
		h = std::max(1u, h / 2);
	}
	detail::Buffer staging;
	createBuffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging);
	void* mapped = nullptr;
	check(vkMapMemory(device_, staging.memory, 0, total, 0, &mapped), "Failed to map Texture2D staging memory");
	for (std::uint32_t level = 0; level < state.mipLevels; ++level)
	{
		std::memcpy(static_cast<char*>(mapped) + regions[level].bufferOffset, chain[level].data(), chain[level].size());
	}
	vkUnmapMemory(device_, staging.memory);

	VkCommandBuffer cmd = beginSingleTimeCommands();
	VkImageMemoryBarrier barrier {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = state.image;
	barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, state.mipLevels, 0, 1};
	// The old contents are fully replaced, so the previous layout does not matter; earlier shader reads are ordered before the write.
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	vkCmdCopyBufferToImage(cmd, staging.buffer, state.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<std::uint32_t>(regions.size()), regions.data());
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	endSingleTimeCommands(cmd);
	destroyBuffer(device_, staging);
}

Texture2DHandle VkApp::createTexture2DFromImage(const void* data, std::size_t byteSize, Texture2DFormat format, bool repeat,
	std::uint32_t* width, std::uint32_t* height)
{
	ImageRgba image;
	if (data == nullptr || !decodeImageRgba(data, byteSize, image) || image.width <= 0 || image.height <= 0)
	{
		return {};
	}
	if (width != nullptr) *width = static_cast<std::uint32_t>(image.width);
	if (height != nullptr) *height = static_cast<std::uint32_t>(image.height);
	Texture2DDescription description;
	description.width = static_cast<std::uint32_t>(image.width);
	description.height = static_cast<std::uint32_t>(image.height);
	description.format = format;
	description.repeat = repeat;
	return createTexture2D(description, image.rgba.data(), image.rgba.size());
}

void VkApp::destroyTexture2D(Texture2DHandle handle)
{
	Texture2DState state;
	{
		std::lock_guard<std::mutex> lock(texture2DMutex());
		AppTexture2D& table = tableFor(this);
		Texture2DState& s = resolve(table, handle);
		state = s;
		s.alive = false;
		++s.generation;
		s.image = VK_NULL_HANDLE;
		s.memory = VK_NULL_HANDLE;
		s.view = VK_NULL_HANDLE;
		s.sampler = VK_NULL_HANDLE;
		table.free.push_back(handle.index);
	}
	// A frame in flight may still sample it: destroy once submitted work has completed.
	retire([device = device_, state]()
	{
		vkDestroySampler(device, state.sampler, nullptr);
		vkDestroyImageView(device, state.view, nullptr);
		vkDestroyImage(device, state.image, nullptr);
		vkFreeMemory(device, state.memory, nullptr);
	});
}

VkDescriptorSet VkApp::createMaterialTextureSet(const MaterialDescription& description)
{
	const VkDescriptorSetLayout layout = materialTextureSetLayout();
	bool needWhite = false;
	for (std::uint32_t i = 0; i < kMaterialTextureSlots; ++i)
	{
		needWhite = needWhite || !description.textures[i].isValid();
	}
	if (needWhite)
	{
		bool have = false;
		{
			std::lock_guard<std::mutex> lock(texture2DMutex());
			have = tableFor(this).white.isValid();
		}
		if (!have)
		{
			// A 1x1 white texel for the slots the material does not use.
			const std::uint8_t pixel[4] = {255, 255, 255, 255};
			Texture2DDescription d;
			d.width = 1;
			d.height = 1;
			d.format = Texture2DFormat::RGBA8_UNORM;
			d.generateMipmaps = false;
			const Texture2DHandle white = createTexture2D(d, pixel, sizeof pixel);
			std::lock_guard<std::mutex> lock(texture2DMutex());
			tableFor(this).white = white;
		}
	}
	std::lock_guard<std::mutex> lock(texture2DMutex());
	AppTexture2D& t = tableFor(this);
	std::array<VkDescriptorImageInfo, kMaterialTextureSlots> images {};
	for (std::uint32_t i = 0; i < kMaterialTextureSlots; ++i)
	{
		const Texture2DHandle h = description.textures[i].isValid() ? description.textures[i] : t.white;
		const Texture2DState& s = resolve(t, h);
		images[i] = {s.sampler, s.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	}
	VkDescriptorSetAllocateInfo allocation {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	allocation.descriptorPool = t.pool;
	allocation.descriptorSetCount = 1;
	allocation.pSetLayouts = &layout;
	VkDescriptorSet set = VK_NULL_HANDLE;
	check(vkAllocateDescriptorSets(device_, &allocation, &set), "Failed to allocate a material texture descriptor set (too many textured materials?)");
	std::array<VkWriteDescriptorSet, kMaterialTextureSlots> writes {};
	for (std::uint32_t i = 0; i < kMaterialTextureSlots; ++i)
	{
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = set;
		writes[i].dstBinding = i;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[i].pImageInfo = &images[i];
	}
	vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
	return set;
}

void VkApp::freeMaterialTextureSet(VkDescriptorSet set)
{
	if (set == VK_NULL_HANDLE)
	{
		return;
	}
	VkDescriptorPool pool = VK_NULL_HANDLE;
	{
		std::lock_guard<std::mutex> lock(texture2DMutex());
		pool = tableFor(this).pool;
	}
	if (pool != VK_NULL_HANDLE)
	{
		retire([device = device_, pool, set]() { vkFreeDescriptorSets(device, pool, 1, &set); });
	}
}

void VkApp::destroyTexture2DResources()
{
	AppTexture2D table;
	{
		std::lock_guard<std::mutex> lock(texture2DMutex());
		auto it = texture2DTables().find(this);
		if (it == texture2DTables().end())
		{
			return;
		}
		table = std::move(it->second);
		texture2DTables().erase(it);
	}
	for (const Texture2DState& s : table.textures)
	{
		if (!s.alive) continue;
		vkDestroySampler(device_, s.sampler, nullptr);
		vkDestroyImageView(device_, s.view, nullptr);
		vkDestroyImage(device_, s.image, nullptr);
		vkFreeMemory(device_, s.memory, nullptr);
	}
	if (table.pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, table.pool, nullptr);
	if (table.setLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, table.setLayout, nullptr);
}

}
