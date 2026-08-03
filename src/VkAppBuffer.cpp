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
#include "SceneGraph.h"

#include <array>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace lightGraphics
{
	using detail::Buffer;
	using detail::Instance;
	using detail::LightingBufferObject;
	using detail::UniformBufferObject;

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

	void VkApp::createCommandPool()
	{
		VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		cpci.queueFamilyIndex = qFamGfx;
		cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK(vkCreateCommandPool(device_, &cpci, nullptr, &commandPool_));
	}

	VkCommandBuffer VkApp::beginSingleTimeCommands()
	{
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = commandPool_;
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer cmd{};
		if (vkAllocateCommandBuffers(device_, &allocInfo, &cmd) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to allocate command buffer");
		}

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to begin command buffer");
		}

		return cmd;
	}

	void VkApp::endSingleTimeCommands(VkCommandBuffer cmd)
	{
		if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to record command buffer");
		}

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmd;

		if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to submit single-time command buffer");
		}
		vkQueueWaitIdle(graphicsQueue_);

		vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
	}

	void VkApp::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
		Buffer& out)
	{
		// Create using the raw helper
		createBufferRaw(size, usage, properties, out.buffer, out.memory);
		out.size = size;
	}

	void VkApp::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
	{
		if (src == VK_NULL_HANDLE || dst == VK_NULL_HANDLE || size == 0)
		{
			return;
		}

		VkCommandBuffer cmd = beginSingleTimeCommands();

		VkBufferCopy copyRegion{};
		copyRegion.srcOffset = 0;
		copyRegion.dstOffset = 0;
		copyRegion.size = size;
		vkCmdCopyBuffer(cmd, src, dst, 1, &copyRegion);

		VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer = dst;
		barrier.offset = 0;
		barrier.size = size;
		vkCmdPipelineBarrier(cmd,
		                     VK_PIPELINE_STAGE_TRANSFER_BIT,
		                     VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		                     0,
		                     0,
		                     nullptr,
		                     1,
		                     &barrier,
		                     0,
		                     nullptr);

		endSingleTimeCommands(cmd);
	}

	// New raw helper that the rest of this file can use
	void VkApp::createBufferRaw(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
								VkBuffer& buffer, VkDeviceMemory& memory)
	{
		buffer = VK_NULL_HANDLE;
		memory = VK_NULL_HANDLE;

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create buffer");
		}

		try
		{
			VkMemoryRequirements memReq{};
			vkGetBufferMemoryRequirements(device_, buffer, &memReq);

			VkMemoryAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memReq.size;
			allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);

			if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to allocate buffer memory");
			}

			if (vkBindBufferMemory(device_, buffer, memory, 0) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to bind buffer memory");
			}
		}
		catch (...)
		{
			if (memory != VK_NULL_HANDLE)
			{
				vkFreeMemory(device_, memory, nullptr);
				memory = VK_NULL_HANDLE;
			}
			vkDestroyBuffer(device_, buffer, nullptr);
			buffer = VK_NULL_HANDLE;
			throw;
		}
	}


	void VkApp::createUniformBuffers()
	{
		VkDeviceSize bufferSize = sizeof(UniformBufferObject);
		VkDeviceSize lightingBufferSize = sizeof(LightingBufferObject);
		size_t count = swapChainImages_.size();

		uniformBuffers2_.resize(count);
		uniformBuffersMapped_.resize(count, nullptr);
		lightingBuffers_.resize(count);
		lightingBuffersMapped_.resize(count, nullptr);

		for (size_t i = 0; i < count; i++)
		{
			createBuffer(
				bufferSize,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				uniformBuffers2_[i]
			);

			void* data = nullptr;
			VkResult mapRes = vkMapMemory(
				device_,
				uniformBuffers2_[i].memory,
				0,
				bufferSize,
				0,
				&data
			);
			if (mapRes != VK_SUCCESS || data == nullptr)
			{
				throw std::runtime_error("failed to map uniform buffer memory");
			}
			uniformBuffersMapped_[i] = data;

			createBuffer(
				lightingBufferSize,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				lightingBuffers_[i]
			);

			void* lightingData = nullptr;
			mapRes = vkMapMemory(
				device_,
				lightingBuffers_[i].memory,
				0,
				lightingBufferSize,
				0,
				&lightingData
			);
			if (mapRes != VK_SUCCESS || lightingData == nullptr)
			{
				throw std::runtime_error("failed to map lighting uniform buffer memory");
			}
			lightingBuffersMapped_[i] = lightingData;
		}
	}

	void VkApp::destroyBuffer(VkDevice device, Buffer& buf)
	{
		if (buf.buffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, buf.buffer, nullptr);
			buf.buffer = VK_NULL_HANDLE;
		}
		if (buf.memory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, buf.memory, nullptr);
			buf.memory = VK_NULL_HANDLE;
		}
		buf.size = 0;
	}

	void VkApp::createDescriptorPool()
	{
		// --- Descriptors
		std::array<VkDescriptorPoolSize, 2> poolSizes{};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = static_cast<uint32_t>(uniformBuffers2_.size() * 2);
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = static_cast<uint32_t>(uniformBuffers2_.size());
		VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
		dpci.maxSets = (uint32_t)uniformBuffers2_.size();
		dpci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		dpci.pPoolSizes = poolSizes.data();
		VK_CHECK(vkCreateDescriptorPool(device_, &dpci, nullptr, &descriptorPool_));
	}

	void VkApp::createDescriptorSets()
	{
		std::vector<VkDescriptorSetLayout> layouts(uniformBuffers2_.size(), descriptorSetLayout_);
		VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
		dsai.descriptorPool = descriptorPool_;
		dsai.descriptorSetCount = (uint32_t)layouts.size();
		dsai.pSetLayouts = layouts.data();
		descriptorSets_.resize(layouts.size());
		VK_CHECK(vkAllocateDescriptorSets(device_, &dsai, descriptorSets_.data()));

		for (size_t i=0;i < uniformBuffers2_.size();++i)
		{
			VkDescriptorBufferInfo bi{uniformBuffers2_[i].buffer, 0, sizeof(UniformBufferObject)};
			VkDescriptorBufferInfo lightBi{lightingBuffers_[i].buffer, 0, sizeof(LightingBufferObject)};
			VkDescriptorImageInfo shadowBi{};
			shadowBi.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			shadowBi.imageView = shadowImageView_;
			shadowBi.sampler = shadowSampler_;

			std::array<VkWriteDescriptorSet, 3> writes{};
			writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[0].dstSet = descriptorSets_[i];
			writes[0].dstBinding = 0;
			writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writes[0].descriptorCount = 1;
			writes[0].pBufferInfo = &bi;

			writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[1].dstSet = descriptorSets_[i];
			writes[1].dstBinding = 1;
			writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writes[1].descriptorCount = 1;
			writes[1].pBufferInfo = &lightBi;

			writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[2].dstSet = descriptorSets_[i];
			writes[2].dstBinding = 2;
			writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[2].descriptorCount = 1;
			writes[2].pImageInfo = &shadowBi;

			vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		}
	}

	void VkApp::createSceneResources()
	{
		// TODO: Create sphere mesh/instance buffers and any textures needed
		// No throw here so you can skip if you hook into your existing scene code elsewhere
	}

	void VkApp::createCommandBuffers()
	{
		// Free any old buffers first (safe if empty)
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

		// Sanity: these must match
		if (swapChainFramebuffers_.size() != swapChainImages_.size())
		{
			throw std::runtime_error("createCommandBuffers: framebuffer count != swapchain images");
		}

		// Allocate one primary CB per swapchain image
		commandBuffers_.resize(swapChainImages_.size(), VK_NULL_HANDLE);

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = commandPool_;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());

		if (vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()) != VK_SUCCESS)
		{
			throw std::runtime_error("createCommandBuffers: allocation failed");
		}

		// Record each command buffer
		for (uint32_t i = 0; i < static_cast<uint32_t>(commandBuffers_.size()); i++)
		{
			recordCommandBuffer(commandBuffers_[i], i);
		}
	}


	uint32_t VkApp::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props)
	{
		VkPhysicalDeviceMemoryProperties mp{};
		vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mp);
		for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
		{
			if ((typeFilter & (1<<i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
				return i;
		}
		throw std::runtime_error("No suitable memory type");
	}

	void VkApp::ensureInstanceBufferSizeForFrame(uint32_t frameIndex, VkDeviceSize requiredSize)
	{
		if (instanceBufferSizes_[frameIndex] < requiredSize)
		{
			// Clean up old buffer if it exists
			if (instanceBufs_[frameIndex].buffer != VK_NULL_HANDLE)
			{
				if (instanceBufferMappedPerFrame_[frameIndex] != nullptr &&
				    instanceBufs_[frameIndex].memory != VK_NULL_HANDLE)
				{
					vkUnmapMemory(device_, instanceBufs_[frameIndex].memory);
					instanceBufferMappedPerFrame_[frameIndex] = nullptr;
				}
				vkDestroyBuffer(device_, instanceBufs_[frameIndex].buffer, nullptr);
				vkFreeMemory(device_, instanceBufs_[frameIndex].memory, nullptr);
			}

			// Create new larger buffer for this frame
			createBuffer(requiredSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
						instanceBufs_[frameIndex]);

			// Map the buffer persistently
			VK_CHECK(vkMapMemory(device_, instanceBufs_[frameIndex].memory, 0, requiredSize, 0, &instanceBufferMappedPerFrame_[frameIndex]));
			instanceBufferSizes_[frameIndex] = requiredSize;
		}
	}

	void VkApp::updateInstanceDataOptimized()
	{
		if (_objects_.empty())
		{
			instanceCount = 0;
			instanceCount_ = 0;
			dirtyObjects_.clear();
			instanceDataCache_.clear();
			instanceDataDirty_ = false;
			return;
		}

		if (!instanceDataDirty_)
		{
			return;
		}

		// Update instance count
		instanceCount_ = static_cast<uint32_t>(_objects_.size());

		// Ensure we have enough space in the dirty tracking array
		if (dirtyObjects_.size() < _objects_.size())
		{
			dirtyObjects_.resize(_objects_.size(), false);
		}

		// Ensure we have enough space in the instance data cache
		if (instanceDataCache_.size() < _objects_.size())
		{
			instanceDataCache_.resize(_objects_.size());
		}

		// Only update dirty objects
		bool anyDirty = false;
		for (size_t i = 0; i < _objects_.size(); ++i)
		{
			if (dirtyObjects_[i])
			{
				instanceDataCache_[i] = makeInstanceForObject(i);

				dirtyObjects_[i] = false;
				anyDirty = true;
			}
		}

		if (!anyDirty)
		{
			instanceDataDirty_ = false;
			return;
		}

		// Every frame-in-flight buffer must be resized here, not just
		// currentFrame_'s: recordCommandBuffer is driven by the swapchain's
		// imageIndex/currentFrame_ on whichever frame comes next, which may
		// not be this one, and this function's dirty flag is cleared below
		// so a later call may not revisit an under-sized sibling buffer
		// before it's used -- leaving it undersized causes an out-of-bounds
		// write (and a segfault) the next time that frame is drawn.
		VkDeviceSize instBytes = sizeof(Instance) * _objects_.size();
		for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
		{
			ensureInstanceBufferSizeForFrame(frameIndex, instBytes);
			void* mapped = instanceBufferMappedPerFrame_[frameIndex];
			if (mapped)
			{
				std::memcpy(mapped, instanceDataCache_.data(), (size_t)instBytes);
			}
		}

		instanceDataDirty_ = false;
	}

	void VkApp::flushPendingUpdates()
	{
		if (sceneFinalized_ && device_ != VK_NULL_HANDLE && currentFrame_ < inFlight_.size())
		{
			VK_CHECK(vkWaitForFences(device_, 1, &inFlight_[currentFrame_], VK_TRUE, UINT64_MAX));
		}
		sceneGraph_->updateWorldTransforms();
		sceneGraph_->syncToRenderer();
		updateInstanceDataOptimized();
		updateRiggedInstances();
	}

	void VkApp::updateInstanceData()
	{
		if (_objects_.empty())
		{
			instanceCount = 0;
			instanceCount_ = 0;
			dirtyObjects_.clear();
			instanceDataCache_.clear();
			instanceDataDirty_ = false;
			return;
		}

		// Update instance count
		instanceCount = static_cast<uint32_t>(_objects_.size());
		instanceCount_ = static_cast<uint32_t>(_objects_.size());

		// Generate instance data from objects
		std::vector<Instance> instances(instanceCount_);
		for (size_t i = 0; i < _objects_.size() && i < instanceCount_; ++i)
		{
			instances[i] = makeInstanceForObject(i);
		}

		// Update the instance buffer
		VkDeviceSize instBytes = sizeof(Instance) * instances.size();

		// If the buffer is too small, recreate it
		if (instanceBuf.size < instBytes)
		{
			if (instanceBuf.buffer != VK_NULL_HANDLE)
			{
				vkDestroyBuffer(device_, instanceBuf.buffer, nullptr);
				vkFreeMemory(device_, instanceBuf.memory, nullptr);
			}
			createBuffer(instBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
						instanceBuf);
		}

		// Resize and populate every frame-in-flight buffer, not just
		// currentFrame_'s -- see the identical comment in
		// updateInstanceDataOptimized() for why leaving a sibling frame's
		// buffer undersized here leads to an out-of-bounds write later.
		for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
		{
			ensureInstanceBufferSizeForFrame(frameIndex, instBytes);
			void* ptr = instanceBufferMappedPerFrame_[frameIndex];
			if (ptr)
			{
				std::memcpy(ptr, instances.data(), (size_t)instBytes);
			}
		}
	}
}
