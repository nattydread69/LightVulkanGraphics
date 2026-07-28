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

#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace lightGraphics
{
	using detail::Instance;
	using detail::Vertex;

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

	void VkApp::recreateSwapChain()
	{
		int w = 0, h = 0;
		glfwGetFramebufferSize(window_, &w, &h);
		while (w == 0 || h == 0)
		{
			glfwGetFramebufferSize(window_, &w, &h);
			glfwWaitEvents();
		}

		vkDeviceWaitIdle(device_);

		cleanupSwapChain();

		createSwapChain();
		createImageViews();
		createRenderPass();
		createGraphicsPipeline();
		createDepthResources();
		createFramebuffers();
		createUniformBuffers();
		createDescriptorPool();
		createDescriptorSets();
		createCommandBuffers();
		rebuildScreenTextMeshes();
	}

	void VkApp::cleanupSwapChain()
	{
		// Depth
		if (depthImageView_ != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device_, depthImageView_, nullptr);
			depthImageView_ = VK_NULL_HANDLE;
		}
		if (depthImage_ != VK_NULL_HANDLE)
		{
			vkDestroyImage(device_, depthImage_, nullptr);
			depthImage_ = VK_NULL_HANDLE;
		}
		if (depthImageMemory_ != VK_NULL_HANDLE)
		{
			vkFreeMemory(device_, depthImageMemory_, nullptr);
			depthImageMemory_ = VK_NULL_HANDLE;
		}

		// Framebuffers
		for (auto fb : swapChainFramebuffers_)
		{
			vkDestroyFramebuffer(device_, fb, nullptr);
		}
		swapChainFramebuffers_.clear();

		// Command buffers
		if (!commandBuffers_.empty())
		{
			vkFreeCommandBuffers(
				device_,
				commandPool_,
				static_cast<uint32_t>(commandBuffers_.size()),
				commandBuffers_.data()
			);
			commandBuffers_.clear();
		}

		// Pipeline and render pass
		if (graphicsPipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
			graphicsPipeline_ = VK_NULL_HANDLE;
		}
		if (flexibleShapePipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, flexibleShapePipeline_, nullptr);
			flexibleShapePipeline_ = VK_NULL_HANDLE;
		}
		if (flexibleShapeOverlayPipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, flexibleShapeOverlayPipeline_, nullptr);
			flexibleShapeOverlayPipeline_ = VK_NULL_HANDLE;
		}
		if (wireframePipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, wireframePipeline_, nullptr);
			wireframePipeline_ = VK_NULL_HANDLE;
		}
		if (unlitPipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, unlitPipeline_, nullptr);
			unlitPipeline_ = VK_NULL_HANDLE;
		}
		if (linePipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, linePipeline_, nullptr);
			linePipeline_ = VK_NULL_HANDLE;
		}
		if (pipelineLayout_ != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
			pipelineLayout_ = VK_NULL_HANDLE;
		}
		if (renderPass_ != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(device_, renderPass_, nullptr);
			renderPass_ = VK_NULL_HANDLE;
		}

		// Image views
		for (auto view : swapChainImageViews_)
		{
			vkDestroyImageView(device_, view, nullptr);
		}
		swapChainImageViews_.clear();

		// Swapchain
		if (swapChain_ != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(device_, swapChain_, nullptr);
			swapChain_ = VK_NULL_HANDLE;
		}


		// Unmap UBO memory before freeing
		for (size_t i = 0; i < uniformBuffersMemory_.size(); i++)
		{
			if (uniformBuffersMemory_[i] != VK_NULL_HANDLE && uniformBuffersMapped_.size() > i)
			{
				if (uniformBuffersMapped_[i] != nullptr)
				{
					vkUnmapMemory(device_, uniformBuffersMemory_[i]);
					uniformBuffersMapped_[i] = nullptr;
				}
			}
		}

		for (size_t i = 0; i < uniformBuffers_.size(); i++)
		{
			if (uniformBuffers_[i] != VK_NULL_HANDLE)
			{
				vkDestroyBuffer(device_, uniformBuffers_[i], nullptr);
				uniformBuffers_[i] = VK_NULL_HANDLE;
			}
			if (uniformBuffersMemory_[i] != VK_NULL_HANDLE)
			{
				vkFreeMemory(device_, uniformBuffersMemory_[i], nullptr);
				uniformBuffersMemory_[i] = VK_NULL_HANDLE;
			}
		}

		for (size_t i = 0; i < uniformBuffers2_.size(); i++)
		{
			if (uniformBuffers2_[i].memory != VK_NULL_HANDLE && uniformBuffersMapped_.size() > i)
			{
				if (uniformBuffersMapped_[i] != nullptr)
				{
					vkUnmapMemory(device_, uniformBuffers2_[i].memory);
					uniformBuffersMapped_[i] = nullptr;
				}
			}
			destroyBuffer(device_, uniformBuffers2_[i]);
		}
		for (size_t i = 0; i < lightingBuffers_.size(); i++)
		{
			if (lightingBuffers_[i].memory != VK_NULL_HANDLE && lightingBuffersMapped_.size() > i)
			{
				if (lightingBuffersMapped_[i] != nullptr)
				{
					vkUnmapMemory(device_, lightingBuffers_[i].memory);
					lightingBuffersMapped_[i] = nullptr;
				}
			}
			destroyBuffer(device_, lightingBuffers_[i]);
		}
		uniformBuffers2_.clear();
		uniformBuffers_.clear();
		uniformBuffersMemory_.clear();
		uniformBuffersMapped_.clear();
		lightingBuffers_.clear();
		lightingBuffersMapped_.clear();

		if (descriptorPool_ != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
			descriptorPool_ = VK_NULL_HANDLE;
		}
		descriptorSets_.clear();
	}

	void VkApp::createSurface()
	{
		VK_CHECK(glfwCreateWindowSurface(inst, window_, nullptr, &surface_));
	}

	void VkApp::createSwapChain()
	{
		VkSurfaceCapabilitiesKHR caps{};
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);
		uint32_t fmtCount=0; vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, nullptr);
		std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, fmts.data());
		VkSurfaceFormatKHR chosen = fmts[0];
		for (auto& f : fmts)
		{
			if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format==VK_FORMAT_B8G8R8A8_SRGB)
			{
				chosen = f;
				break;
			}
		}
		swapChainImageFormat_ = chosen.format;
		const uint32_t WIDTH = width_, HEIGHT = height_;
		swapChainExtent_ = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : VkExtent2D{WIDTH, HEIGHT};
		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // vsync
		uint32_t desiredImageCount = std::max(2u, caps.minImageCount);
		if (caps.maxImageCount > 0)
		{
			desiredImageCount = std::min(desiredImageCount, caps.maxImageCount);
		}

		VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
		sci.surface = surface_;
		sci.minImageCount = desiredImageCount;
		sci.imageFormat = swapChainImageFormat_;
		sci.imageColorSpace = chosen.colorSpace;
		sci.imageExtent = swapChainExtent_;
		sci.imageArrayLayers = 1;
		sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		uint32_t qidx[2] = { qFamGfx, qFamPresent };
		if (qFamGfx != qFamPresent)
		{
			sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			sci.queueFamilyIndexCount = 2;
			sci.pQueueFamilyIndices = qidx;
		}
		else
		{
			sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}
		sci.preTransform = caps.currentTransform;
		sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		sci.presentMode = presentMode;
		sci.clipped = VK_TRUE;
		VK_CHECK(vkCreateSwapchainKHR(device_, &sci, nullptr, &swapChain_));
		uint32_t nImgs=0; vkGetSwapchainImagesKHR(device_, swapChain_, &nImgs, nullptr);
		swapChainImages_.resize(nImgs);
		vkGetSwapchainImagesKHR(device_, swapChain_, &nImgs, swapChainImages_.data());

		if (debugOutput)
		{
			std::ostringstream message;
			message << "Swapchain: format=" << swapChainImageFormat_
			        << ", colorSpace=" << chosen.colorSpace
			        << ", images=" << nImgs
			        << ", extent=" << swapChainExtent_.width << 'x' << swapChainExtent_.height;
			logMessage(LogLevel::Debug, message.str());
		}
	}

	void VkApp::createImageViews()
	{
		for (auto img : swapChainImages_)
		{
			VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
			vi.image = img;
			vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
			vi.format = swapChainImageFormat_;
			vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			vi.subresourceRange.levelCount = 1;
			vi.subresourceRange.layerCount = 1;
			VkImageView view{};
			VK_CHECK(vkCreateImageView(device_, &vi, nullptr, &view));
			swapChainImageViews_.push_back(view);
		}
	}

	void VkApp::createRenderPass()
	{
		VkAttachmentDescription color{};
		color.format = swapChainImageFormat_;
		color.samples = VK_SAMPLE_COUNT_1_BIT;
		color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color.storeOp= VK_ATTACHMENT_STORE_OP_STORE;
		color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		color.finalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentDescription depth{};
		depth.format = findDepthFormat();
		depth.samples = VK_SAMPLE_COUNT_1_BIT;
		depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
		VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

		VkSubpassDescription sub{};
		sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		sub.colorAttachmentCount = 1;
		sub.pColorAttachments = &colorRef;
		sub.pDepthStencilAttachment = &depthRef;

		VkAttachmentDescription attachments[] = {color, depth};
		VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
		rpci.attachmentCount = 2; rpci.pAttachments = attachments;
		rpci.subpassCount = 1; rpci.pSubpasses = &sub;
		VK_CHECK(vkCreateRenderPass(device_, &rpci, nullptr, &renderPass_));
	}

	void VkApp::createShadowRenderPass()
	{
		shadowDepthFormat_ = findShadowDepthFormat();

		VkAttachmentDescription depth{};
		depth.format = shadowDepthFormat_;
		depth.samples = VK_SAMPLE_COUNT_1_BIT;
		depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

		VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

		VkSubpassDescription sub{};
		sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		sub.colorAttachmentCount = 0;
		sub.pDepthStencilAttachment = &depthRef;

		std::array<VkSubpassDependency, 2> dependencies{};
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
		rpci.attachmentCount = 1;
		rpci.pAttachments = &depth;
		rpci.subpassCount = 1;
		rpci.pSubpasses = &sub;
		rpci.dependencyCount = static_cast<uint32_t>(dependencies.size());
		rpci.pDependencies = dependencies.data();
		VK_CHECK(vkCreateRenderPass(device_, &rpci, nullptr, &shadowRenderPass_));
	}

	void VkApp::createDepthResources()
	{
		// Pick a supported depth format
		VkFormat depthFormat = findDepthFormat();

		// Create depth image and allocate device-local memory
		createImage(
			swapChainExtent_.width,
			swapChainExtent_.height,
			depthFormat,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			depthImage_,
			depthImageMemory_,
			VK_SAMPLE_COUNT_1_BIT
		);

		// Create a view for the depth image
		VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (hasStencilComponent(depthFormat))
		{
			aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		depthImageView_ = createImageView(depthImage_, depthFormat, aspect);

		// Transition to a layout suitable for using as a depth attachment
		transitionImageLayout(
			depthImage_,
			depthFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		);
	}

	void VkApp::createShadowResources()
	{
		for (VkFramebuffer framebuffer : shadowFramebuffers_)
		{
			vkDestroyFramebuffer(device_, framebuffer, nullptr);
		}
		shadowFramebuffers_.clear();
		for (VkImageView view : shadowLayerImageViews_)
		{
			vkDestroyImageView(device_, view, nullptr);
		}
		shadowLayerImageViews_.clear();
		if (shadowImageView_ != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device_, shadowImageView_, nullptr);
			shadowImageView_ = VK_NULL_HANDLE;
		}
		if (shadowSampler_ != VK_NULL_HANDLE)
		{
			vkDestroySampler(device_, shadowSampler_, nullptr);
			shadowSampler_ = VK_NULL_HANDLE;
		}
		if (shadowImage_ != VK_NULL_HANDLE)
		{
			vkDestroyImage(device_, shadowImage_, nullptr);
			shadowImage_ = VK_NULL_HANDLE;
		}
		if (shadowImageMemory_ != VK_NULL_HANDLE)
		{
			vkFreeMemory(device_, shadowImageMemory_, nullptr);
			shadowImageMemory_ = VK_NULL_HANDLE;
		}

		shadowLayerCount_ = static_cast<uint32_t>(lightGraphics::MaxForwardLights);
		shadowMapSize_ = 1024;
		for (const auto& light : lights_)
		{
			if (light.castsShadow)
			{
				shadowMapSize_ = std::max(shadowMapSize_, light.shadowMapSize);
			}
		}
		shadowMapSize_ = std::clamp(shadowMapSize_, 256u, 4096u);
		if (shadowDepthFormat_ == VK_FORMAT_UNDEFINED)
		{
			shadowDepthFormat_ = findShadowDepthFormat();
		}

		VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent = {shadowMapSize_, shadowMapSize_, 1};
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = shadowLayerCount_;
		imageInfo.format = shadowDepthFormat_;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK(vkCreateImage(device_, &imageInfo, nullptr, &shadowImage_));

		VkMemoryRequirements memReq{};
		vkGetImageMemoryRequirements(device_, shadowImage_, &memReq);
		VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK(vkAllocateMemory(device_, &allocInfo, nullptr, &shadowImageMemory_));
		vkBindImageMemory(device_, shadowImage_, shadowImageMemory_, 0);

		VkImageViewCreateInfo arrayViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		arrayViewInfo.image = shadowImage_;
		arrayViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		arrayViewInfo.format = shadowDepthFormat_;
		arrayViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		arrayViewInfo.subresourceRange.baseMipLevel = 0;
		arrayViewInfo.subresourceRange.levelCount = 1;
		arrayViewInfo.subresourceRange.baseArrayLayer = 0;
		arrayViewInfo.subresourceRange.layerCount = shadowLayerCount_;
		VK_CHECK(vkCreateImageView(device_, &arrayViewInfo, nullptr, &shadowImageView_));

		shadowLayerImageViews_.reserve(shadowLayerCount_);
		for (uint32_t layer = 0; layer < shadowLayerCount_; ++layer)
		{
			VkImageViewCreateInfo layerViewInfo = arrayViewInfo;
			layerViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			layerViewInfo.subresourceRange.baseArrayLayer = layer;
			layerViewInfo.subresourceRange.layerCount = 1;
			VkImageView layerView = VK_NULL_HANDLE;
			VK_CHECK(vkCreateImageView(device_, &layerViewInfo, nullptr, &layerView));
			shadowLayerImageViews_.push_back(layerView);
		}

		VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
		samplerInfo.magFilter = VK_FILTER_NEAREST;
		samplerInfo.minFilter = VK_FILTER_NEAREST;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		samplerInfo.compareEnable = VK_TRUE;
		samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;
		VK_CHECK(vkCreateSampler(device_, &samplerInfo, nullptr, &shadowSampler_));

		VkCommandBuffer cmd = beginSingleTimeCommands();
		VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = shadowImage_;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = shadowLayerCount_;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cmd,
		                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		                     0,
		                     0, nullptr,
		                     0, nullptr,
		                     1, &barrier);
		endSingleTimeCommands(cmd);

		shadowFramebuffers_.reserve(shadowLayerCount_);
		for (VkImageView layerView : shadowLayerImageViews_)
		{
			VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
			framebufferInfo.renderPass = shadowRenderPass_;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = &layerView;
			framebufferInfo.width = shadowMapSize_;
			framebufferInfo.height = shadowMapSize_;
			framebufferInfo.layers = 1;
			VkFramebuffer framebuffer = VK_NULL_HANDLE;
			VK_CHECK(vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &framebuffer));
			shadowFramebuffers_.push_back(framebuffer);
		}
	}

	VkFormat VkApp::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
	{
		for (VkFormat format : candidates)
		{
			VkFormatProperties props{};
			vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &props);

			if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
			{
				return format;
			}
			else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
			{
				return format;
			}
		}

		throw std::runtime_error("failed to find supported image format");
	}

	VkFormat VkApp::findDepthFormat()
	{
		// Prefer higher precision if available
		return findSupportedFormat(
			{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
			VK_IMAGE_TILING_OPTIMAL,
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
		);
	}

	VkFormat VkApp::findShadowDepthFormat()
	{
		return findSupportedFormat(
			{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
			VK_IMAGE_TILING_OPTIMAL,
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
		);
	}

	bool VkApp::hasStencilComponent(VkFormat format)
	{
		return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
	}

	void VkApp::createFramebuffers()
	{
		logMessage(LogLevel::Debug,
		           "Creating framebuffers for " + std::to_string(swapChainImageViews_.size()) +
		           " swapchain views");
		for (auto view : swapChainImageViews_)
		{
			VkImageView atts[] = { view, depthImageView_ };
			VkFramebufferCreateInfo fbi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
			fbi.renderPass = renderPass_;
			fbi.attachmentCount=2; fbi.pAttachments=atts;
			fbi.width=swapChainExtent_.width; fbi.height=swapChainExtent_.height; fbi.layers=1;
			VkFramebuffer fb{};
			VK_CHECK(vkCreateFramebuffer(device_, &fbi, nullptr, &fb));
			swapChainFramebuffers_.push_back(fb);
		}

		destroyBuffer(device_, vbo);
		destroyBuffer(device_, ibo);
		destroyBuffer(device_, instanceBuf);
		vertexBuffer_ = VK_NULL_HANDLE;
		indexBuffer_ = VK_NULL_HANDLE;
		instanceBuffer_ = VK_NULL_HANDLE;
		indexCount_ = 0;
		instanceCount_ = 0;

		// --- Geometry buffers - Generate separate geometry for each shape type
		std::vector<Vertex> allVertices;
		std::vector<uint32_t> allIndices;

		// Generate geometry for each shape type
		generateAllShapeGeometry(allVertices, allIndices);

		indexCount = (uint32_t) allIndices.size();
		logMessage(LogLevel::Debug, "Generated indexed geometry with " + std::to_string(indexCount) + " indices");

		VkDeviceSize vBytes = sizeof(Vertex) * allVertices.size();
		VkDeviceSize iBytes = sizeof(uint32_t) * allIndices.size();
		createBuffer(vBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vbo);
		createBuffer(iBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ibo);

		void* ptr=nullptr;
		VK_CHECK(vkMapMemory(device_, vbo.memory, 0, vBytes, 0, &ptr));
		std::memcpy(ptr, allVertices.data(), (size_t)vBytes);
		vkUnmapMemory(device_, vbo.memory);
		VK_CHECK(vkMapMemory(device_, ibo.memory, 0, iBytes, 0, &ptr));
		std::memcpy(ptr, allIndices.data(), (size_t)iBytes);
		vkUnmapMemory(device_, ibo.memory);

		// Store shape geometry offsets and counts for rendering
		storeShapeGeometryOffsets();

		// --- Assign geometry buffers to the pipeline-used handles
		vertexBuffer_ = vbo.buffer;
		indexBuffer_ = ibo.buffer;
		// Assign counts to member variables for draw calls
		indexCount_ = indexCount;
		instanceCount = static_cast<uint32_t>(_objects_.size());
		instanceCount_ = instanceCount;
		if (instanceCount > 0)
		{
			VkDeviceSize instBytes = sizeof(Instance) * instanceCount;
			createBuffer(instBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, instanceBuf);
			instanceBuffer_ = instanceBuf.buffer;
			VK_CHECK(vkMapMemory(device_, instanceBuf.memory, 0, instBytes, 0, &ptr));

			std::vector<Instance> instances(instanceCount);
			for (size_t i = 0; i < _objects_.size() && i < instanceCount; ++i)
			{
				const auto& obj = _objects_[i];
				glm::mat4 translation = glm::translate(glm::mat4(1.0f), obj.getPosition());
				glm::mat4 rotation = glm::mat4_cast(obj.getRotation());
				glm::mat4 scale = glm::scale(glm::mat4(1.0f), obj.getSize());

				instances[i].model = translation * rotation * scale;
				instances[i].color = glm::vec3(obj.getColour());
				instances[i].shapeType = static_cast<float>(obj._type);
			}

			std::memcpy(ptr, instances.data(), static_cast<size_t>(instBytes));
			vkUnmapMemory(device_, instanceBuf.memory);
		}
		logMessage(LogLevel::Debug,
		           "Finished framebuffer setup with index count " + std::to_string(indexCount));
	}
}
