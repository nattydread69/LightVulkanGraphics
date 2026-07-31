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

#include "VkApp.h"
#include "FBXLoader.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244)
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace lightGraphics
{
	using detail::Buffer;
	using detail::Texture;

	namespace
	{
		void checkVkResult(VkResult result, const char* expression, const char* file, int line)
		{
			if (result == VK_SUCCESS)
			{
				return;
			}

			std::ostringstream message;
			message << "Vulkan call failed (" << static_cast<int>(result) << "): "
			        << expression << " at " << file << ":" << line;
			throw std::runtime_error(message.str());
		}
	}

#define LVG_VK_CHECK(expr) checkVkResult((expr), #expr, __FILE__, __LINE__)
#define VK_CHECK(expr) LVG_VK_CHECK(expr)

	void VkApp::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
	{
		VkCommandBuffer cmd = beginSingleTimeCommands();

		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;

		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (hasStencilComponent(format))
			{
				barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
		}
		else
		{
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}

		VkPipelineStageFlags srcStage{};
		VkPipelineStageFlags dstStage{};

		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		}
		else
		{
			throw std::runtime_error("unsupported layout transition");
		}

		vkCmdPipelineBarrier(
			cmd,
			srcStage,
			dstStage,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		endSingleTimeCommands(cmd);
	}

	void VkApp::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
		VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory,
		VkSampleCountFlagBits samples)
	{
		image = VK_NULL_HANDLE;
		imageMemory = VK_NULL_HANDLE;

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = width;
		imageInfo.extent.height = height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = format;
		imageInfo.tiling = tiling;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = usage;
		imageInfo.samples = samples;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateImage(device_, &imageInfo, nullptr, &image) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create image");
		}

		try
		{
			VkMemoryRequirements memReq{};
			vkGetImageMemoryRequirements(device_, image, &memReq);

			VkMemoryAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memReq.size;
			allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);

			if (vkAllocateMemory(device_, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to allocate image memory");
			}

			if (vkBindImageMemory(device_, image, imageMemory, 0) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to bind image memory");
			}
		}
		catch (...)
		{
			if (imageMemory != VK_NULL_HANDLE)
			{
				vkFreeMemory(device_, imageMemory, nullptr);
				imageMemory = VK_NULL_HANDLE;
			}
			vkDestroyImage(device_, image, nullptr);
			image = VK_NULL_HANDLE;
			throw;
		}
	}

	VkImageView VkApp::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask)
	{
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;

		viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		viewInfo.subresourceRange.aspectMask = aspectMask;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		VkImageView view = VK_NULL_HANDLE;
		if (vkCreateImageView(device_, &viewInfo, nullptr, &view) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create image view");
		}
		return view;
	}

	void VkApp::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
	{
		VkCommandBuffer cmd = beginSingleTimeCommands();

		VkBufferImageCopy region{};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = {0, 0, 0};
		region.imageExtent = {width, height, 1};

		vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		endSingleTimeCommands(cmd);
	}

	void VkApp::createTextureDescriptorPool()
	{
		if (textureDescriptorPool_ != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device_, textureDescriptorPool_, nullptr);
			textureDescriptorPool_ = VK_NULL_HANDLE;
		}

		VkDescriptorPoolSize poolSize{};
		poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSize.descriptorCount = maxTextureDescriptorCount_;

		VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		poolInfo.maxSets = maxTextureDescriptorCount_;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

		VK_CHECK(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &textureDescriptorPool_));
	}

	void VkApp::destroyTextureResources()
	{
		for (auto& entry : textureCache_)
		{
			if (entry.second)
			{
				destroyTexture(*entry.second);
			}
		}
		textureCache_.clear();

		if (defaultTexture_)
		{
			destroyTexture(*defaultTexture_);
			defaultTexture_.reset();
		}

		if (textureDescriptorPool_ != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device_, textureDescriptorPool_, nullptr);
			textureDescriptorPool_ = VK_NULL_HANDLE;
		}
	}

	std::shared_ptr<Texture> VkApp::createTextureFromPixels(
		const void* pixels,
		uint32_t width,
		uint32_t height,
		VkSamplerAddressMode addressMode)
	{
		if (!pixels || width == 0 || height == 0)
		{
			return nullptr;
		}

		Buffer staging{};
		VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;
		createBuffer(
			imageSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			staging
		);

		void* data = nullptr;
		VK_CHECK(vkMapMemory(device_, staging.memory, 0, imageSize, 0, &data));
		std::memcpy(data, pixels, static_cast<size_t>(imageSize));
		vkUnmapMemory(device_, staging.memory);

		auto texture = std::make_shared<Texture>();
		try
		{
			createImage(
				width,
				height,
				VK_FORMAT_R8G8B8A8_SRGB,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				texture->image,
				texture->memory
			);

			transitionImageLayout(
				texture->image,
				VK_FORMAT_R8G8B8A8_SRGB,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
			);
			copyBufferToImage(staging.buffer, texture->image, width, height);
			transitionImageLayout(
				texture->image,
				VK_FORMAT_R8G8B8A8_SRGB,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			);

			texture->view = createImageView(texture->image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

			VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
			samplerInfo.magFilter = VK_FILTER_LINEAR;
			samplerInfo.minFilter = VK_FILTER_LINEAR;
			samplerInfo.addressModeU = addressMode;
			samplerInfo.addressModeV = addressMode;
			samplerInfo.addressModeW = addressMode;
			samplerInfo.anisotropyEnable = VK_FALSE;
			samplerInfo.maxAnisotropy = 1.0f;
			samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
			samplerInfo.unnormalizedCoordinates = VK_FALSE;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

			VK_CHECK(vkCreateSampler(device_, &samplerInfo, nullptr, &texture->sampler));

			if (textureDescriptorPool_ == VK_NULL_HANDLE || textureSetLayout_ == VK_NULL_HANDLE)
			{
				throw std::runtime_error("Texture descriptor resources not initialized");
			}

			VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
			allocInfo.descriptorPool = textureDescriptorPool_;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &textureSetLayout_;

			const VkResult allocResult =
			    vkAllocateDescriptorSets(device_, &allocInfo, &texture->descriptor);
			if (allocResult == VK_ERROR_OUT_OF_POOL_MEMORY ||
			    allocResult == VK_ERROR_FRAGMENTED_POOL)
			{
				throw std::runtime_error(
				    "Texture descriptor pool exhausted: this VkApp was configured for at "
				    "most " + std::to_string(maxTextureDescriptorCount_) +
				    " distinct textures (see setMaxTextureCount(), which must be called "
				    "before init()).");
			}
			VK_CHECK(allocResult);
		}
		catch (...)
		{
			destroyTexture(*texture);
			destroyBuffer(device_, staging);
			throw;
		}

		destroyBuffer(device_, staging);

		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = texture->view;
		imageInfo.sampler = texture->sampler;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = texture->descriptor;
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

		texture->width = width;
		texture->height = height;

		return texture;
	}

	std::shared_ptr<Texture> VkApp::createSolidColorTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
	{
		const uint8_t pixel[4] = {r, g, b, a};
		auto texture = createTextureFromPixels(pixel, 1, 1);
		if (texture)
		{
			texture->path = "solid_color";
		}
		return texture;
	}

	std::shared_ptr<Texture> VkApp::getOrCreateSolidColorTexture(const glm::vec4& color)
	{
		auto toChannel = [](float component) -> uint8_t
		{
			const float clamped = std::clamp(component, 0.0f, 1.0f);
			return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
		};

		const uint8_t r = toChannel(color.r);
		const uint8_t g = toChannel(color.g);
		const uint8_t b = toChannel(color.b);
		const uint8_t a = toChannel(color.a);

		std::ostringstream keyBuilder;
		keyBuilder << "solid_color:" << static_cast<int>(r) << ','
		           << static_cast<int>(g) << ','
		           << static_cast<int>(b) << ','
		           << static_cast<int>(a);
		const std::string cacheKey = keyBuilder.str();

		auto iter = textureCache_.find(cacheKey);
		if (iter != textureCache_.end() && iter->second)
		{
			return iter->second;
		}

		auto texture = createSolidColorTexture(r, g, b, a);
		if (texture)
		{
			texture->path = cacheKey;
			textureCache_[cacheKey] = texture;
		}
		return texture;
	}

	std::shared_ptr<Texture> VkApp::createTextureFromFile(const std::string& path)
	{
		int texWidth = 0;
		int texHeight = 0;
		int texChannels = 0;
		stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		if (!pixels)
		{
			logMessage(LogLevel::Warning, "Failed to load texture: " + path);
			return nullptr;
		}

		if (debugOutput)
		{
			std::ostringstream message;
			message << "[Texture] Loaded '" << path << "' (" << texWidth << "x" << texHeight
			        << ", channels=" << texChannels << ")";
			logMessage(LogLevel::Debug, message.str());
		}
		auto texture = createTextureFromPixels(pixels, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
		stbi_image_free(pixels);
		if (texture)
		{
			texture->path = path;
		}
		return texture;
	}

	std::shared_ptr<Texture> VkApp::createTextureFromEmbedded(const EmbeddedTextureData& source, const std::string& cacheKey)
	{
		if (source.isRawPixels)
		{
			if (source.data.empty() || source.width == 0 || source.height == 0)
			{
				return nullptr;
			}
			auto texture = createTextureFromPixels(source.data.data(), source.width, source.height);
			if (texture)
			{
				texture->path = cacheKey;
			}
			return texture;
		}

		if (source.data.empty())
		{
			return nullptr;
		}

		int texWidth = 0;
		int texHeight = 0;
		int texChannels = 0;
		stbi_uc* pixels = stbi_load_from_memory(
			source.data.data(),
			static_cast<int>(source.data.size()),
			&texWidth,
			&texHeight,
			&texChannels,
			STBI_rgb_alpha
		);

		if (!pixels)
		{
			logMessage(LogLevel::Warning, "Failed to decode embedded texture data");
			return nullptr;
		}

		auto texture = createTextureFromPixels(pixels, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
		stbi_image_free(pixels);
		if (texture)
		{
			texture->path = cacheKey;
		}
		return texture;
	}

	std::shared_ptr<Texture> VkApp::getOrCreateTexture(const std::string& path)
	{
		if (path.empty())
		{
			return nullptr;
		}

		if (path[0] == '*')
		{
			// Embedded texture marker – handled by createTextureFromEmbedded.
			return nullptr;
		}

		auto iter = textureCache_.find(path);
		if (iter != textureCache_.end() && iter->second)
		{
			return iter->second;
		}

		auto texture = createTextureFromFile(path);
		if (!texture)
		{
			return nullptr;
		}
		textureCache_[path] = texture;
		return texture;
	}

	void VkApp::destroyTexture(Texture& texture)
	{
		if (texture.descriptor != VK_NULL_HANDLE && textureDescriptorPool_ != VK_NULL_HANDLE)
		{
			vkFreeDescriptorSets(device_, textureDescriptorPool_, 1, &texture.descriptor);
			texture.descriptor = VK_NULL_HANDLE;
		}
		if (texture.sampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device_, texture.sampler, nullptr);
			texture.sampler = VK_NULL_HANDLE;
		}
		if (texture.view != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device_, texture.view, nullptr);
			texture.view = VK_NULL_HANDLE;
		}
		if (texture.image != VK_NULL_HANDLE)
		{
			vkDestroyImage(device_, texture.image, nullptr);
			texture.image = VK_NULL_HANDLE;
		}
		if (texture.memory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device_, texture.memory, nullptr);
			texture.memory = VK_NULL_HANDLE;
		}
		texture.width = texture.height = 0;
	}
}
