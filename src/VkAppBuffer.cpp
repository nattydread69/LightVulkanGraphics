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
#include "VkAppExtras.h"

#include <algorithm>
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
		if (uploadCmd_ == VK_NULL_HANDLE)
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
				vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
				throw std::runtime_error("failed to begin command buffer");
			}
			uploadCmd_ = cmd;
		}
		return uploadCmd_;
	}

	void VkApp::endSingleTimeCommands(VkCommandBuffer cmd)
	{
		if (cmd != uploadCmd_)
		{
			throw std::logic_error("endSingleTimeCommands: not the open upload batch");
		}
		// Recording stays open: drawFrame() submits the batch ahead of the next frame.
		// A large batch (e.g. a crowd being loaded with no frames drawn yet) is submitted
		// early so its staging memory can be freed as it completes.
		constexpr std::size_t kMaxCommandsPerUploadBatch = 256;
		if (++uploadCommandCount_ >= kMaxCommandsPerUploadBatch)
		{
			flushUploads(false);
		}
	}

	void VkApp::flushUploads(bool waitForCompletion)
	{
		if (uploadCmd_ != VK_NULL_HANDLE)
		{
			VkCommandBuffer cmd = uploadCmd_;
			uploadCmd_ = VK_NULL_HANDLE;
			uploadCommandCount_ = 0;
			if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
			{
				vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
				throw std::runtime_error("failed to record the upload batch");
			}

			VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
			VkFence fence = VK_NULL_HANDLE;
			if (vkCreateFence(device_, &fenceInfo, nullptr, &fence) != VK_SUCCESS)
			{
				vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
				throw std::runtime_error("failed to create the upload batch fence");
			}

			VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &cmd;
			if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, fence) != VK_SUCCESS)
			{
				vkDestroyFence(device_, fence, nullptr);
				vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
				throw std::runtime_error("failed to submit the upload batch");
			}
			uploadsInFlight_.push_back({cmd, fence, ++submitSerial_});
		}

		if (waitForCompletion && !uploadsInFlight_.empty())
		{
			const VkFence last = uploadsInFlight_.back().fence;
			if (vkWaitForFences(device_, 1, &last, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to wait for the upload batch");
			}
		}
		collectRetired();
	}

	bool VkApp::uploadsMayReferenceResources() const
	{
		return uploadCmd_ != VK_NULL_HANDLE || !uploadsInFlight_.empty();
	}

	void VkApp::retire(std::function<void()> destroy)
	{
		if (!destroy)
		{
			return;
		}
		if (device_ == VK_NULL_HANDLE)
		{
			destroy();
			return;
		}
		// Anything recorded or submitted so far may use the resource; the next
		// submission's completion (its fence covers all earlier ones) proves it is not.
		retired_.push_back({submitSerial_ + 1, std::move(destroy)});
	}

	void VkApp::collectRetired()
	{
		// Finished upload batches advance the completed serial and are freed.
		for (auto it = uploadsInFlight_.begin(); it != uploadsInFlight_.end();)
		{
			if (vkGetFenceStatus(device_, it->fence) == VK_SUCCESS)
			{
				completedSerial_ = std::max(completedSerial_, it->serial);
				vkDestroyFence(device_, it->fence, nullptr);
				vkFreeCommandBuffers(device_, commandPool_, 1, &it->cmd);
				it = uploadsInFlight_.erase(it);
			}
			else
			{
				++it;
			}
		}

		// retired_ is in non-decreasing serial order. Take the safe entries out before
		// running them: a destroy function may itself retire something.
		std::vector<std::function<void()>> ready;
		while (!retired_.empty() && retired_.front().safeAfterSerial <= completedSerial_)
		{
			ready.push_back(std::move(retired_.front().destroy));
			retired_.pop_front();
		}
		for (auto& destroy : ready)
		{
			destroy();
		}
	}

	void VkApp::drainRetired()
	{
		// Caller has made the device idle: every submission is complete.
		if (uploadCmd_ != VK_NULL_HANDLE)
		{
			vkEndCommandBuffer(uploadCmd_);
			vkFreeCommandBuffers(device_, commandPool_, 1, &uploadCmd_);
			uploadCmd_ = VK_NULL_HANDLE;
			uploadCommandCount_ = 0;
		}
		for (InFlightUpload& upload : uploadsInFlight_)
		{
			vkDestroyFence(device_, upload.fence, nullptr);
			vkFreeCommandBuffers(device_, commandPool_, 1, &upload.cmd);
		}
		uploadsInFlight_.clear();
		completedSerial_ = submitSerial_;
		// Destroy functions run here may retire more (e.g. a retired instance's buffers,
		// while uploads were still pending when it was retired); with nothing pending now,
		// those run straight away, so this ends.
		while (!retired_.empty())
		{
			std::function<void()> destroy = std::move(retired_.front().destroy);
			retired_.pop_front();
			destroy();
		}
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
		// While uploads are recorded or in flight, the buffer may be one of their sources
		// (a staging buffer destroyed right after copyBuffer(), as every upload does) or
		// destinations: destroy it once they have completed.
		if ((buf.buffer != VK_NULL_HANDLE || buf.memory != VK_NULL_HANDLE) &&
		    device == device_ && uploadsMayReferenceResources())
		{
			const VkBuffer buffer = buf.buffer;
			const VkDeviceMemory memory = buf.memory;
			retire([device, buffer, memory]()
			{
				if (buffer != VK_NULL_HANDLE)
				{
					vkDestroyBuffer(device, buffer, nullptr);
				}
				if (memory != VK_NULL_HANDLE)
				{
					vkFreeMemory(device, memory, nullptr);
				}
			});
			buf = Buffer{};
			return;
		}
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
		// Free any old buffers first (safe if empty). They may still be pending on the GPU
		// if this is called in a running scene, so wait for every frame in flight first.
		if (!commandBuffers_.empty())
		{
			if (device_ != VK_NULL_HANDLE && !inFlight_.empty())
			{
				VK_CHECK(vkWaitForFences(device_, static_cast<uint32_t>(inFlight_.size()), inFlight_.data(),
					VK_TRUE, UINT64_MAX));
			}
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

		// drawFrame() always calls updateInstanceDataOptimized() (and
		// updateRiggedInstances()) immediately before recordCommandBuffer(), so
		// every per-frame-in-flight instance buffer is guaranteed sized/mapped
		// for the scene's current object count before anything reads from it.
		// This function's own recordCommandBuffer() calls below bypass that --
		// callers include addRiggedObject()/addRiggedObjectHandle() re-recording
		// live (post-sceneFinalized_) via their own "if (sceneFinalized_)
		// { createCommandBuffers(); }" -- so without repeating that same sync
		// here, a frame-in-flight slot that's never been sized/mapped yet falls
		// through writeInstancesToBuffer() to vkMapMemory() on a null memory
		// handle: undefined behaviour that, under a permissive software
		// implementation, doesn't error loudly but silently corrupts whatever
		// GPU memory the driver hands back instead.
		updateInstanceDataOptimized();
		updateRiggedInstances();

		// Record each command buffer
		for (uint32_t i = 0; i < static_cast<uint32_t>(commandBuffers_.size()); i++)
		{
			recordCommandBuffer(commandBuffers_[i], i);
		}
	}


	namespace
	{
		// Instance buffers used to be recreated at the exact byte size needed on every
		// single growth step (see ensureInstanceBufferSizeForFrame()/updateInstanceData()
		// below). For a scene built one object at a time -- a tatami floor grid or a
		// ragdoll debug-shape pool, both added via repeated addObject() calls -- that
		// meant one vkDestroyBuffer/vkFreeMemory/vkCreateBuffer/vkAllocateMemory cycle
		// per object, and hundreds of those compounds into seconds of driver overhead
		// for what should be a near-instant scene rebuild. Geometric growth (at least
		// doubling the previous capacity) turns that into O(log n) reallocations instead
		// of O(n), the same technique std::vector uses for push_back.
		VkDeviceSize growInstanceBufferCapacity(VkDeviceSize currentCapacity, VkDeviceSize requiredSize)
		{
			VkDeviceSize const grown = currentCapacity > 0 ? currentCapacity * 2 : requiredSize;
			return std::max(grown, requiredSize);
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

	VkDeviceMemory VkApp::createBuffersInOneAllocation(const std::vector<BufferRequest>& requests,
	                                                   VkMemoryPropertyFlags properties)
	{
		if (requests.empty())
		{
			return VK_NULL_HANDLE;
		}

		auto fallback = [&]()
		{
			for (const BufferRequest& request : requests)
			{
				createBuffer(request.size, request.usage, properties, *request.buffer);
				if (request.mapped != nullptr)
				{
					VK_CHECK(vkMapMemory(device_, request.buffer->memory, 0, request.size, 0, request.mapped));
				}
			}
			return static_cast<VkDeviceMemory>(VK_NULL_HANDLE);
		};

		std::vector<VkDeviceSize> offsets(requests.size(), 0);
		VkDeviceSize total = 0;
		uint32_t typeBits = ~0u;
		for (size_t i = 0; i < requests.size(); ++i)
		{
			const BufferRequest& request = requests[i];
			VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
			info.size = request.size;
			info.usage = request.usage;
			info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VK_CHECK(vkCreateBuffer(device_, &info, nullptr, &request.buffer->buffer));
			request.buffer->memory = VK_NULL_HANDLE;
			request.buffer->size = request.size;

			VkMemoryRequirements requirements{};
			vkGetBufferMemoryRequirements(device_, request.buffer->buffer, &requirements);
			typeBits &= requirements.memoryTypeBits;
			const VkDeviceSize alignment = std::max<VkDeviceSize>(requirements.alignment, 1);
			offsets[i] = (total + alignment - 1) / alignment * alignment;
			total = offsets[i] + requirements.size;
		}

		uint32_t memoryType = 0;
		bool haveType = typeBits != 0;
		if (haveType)
		{
			try
			{
				memoryType = findMemoryType(typeBits, properties);
			}
			catch (const std::runtime_error&)
			{
				haveType = false;
			}
		}
		if (!haveType)
		{
			for (const BufferRequest& request : requests)
			{
				vkDestroyBuffer(device_, request.buffer->buffer, nullptr);
				*request.buffer = detail::Buffer{};
			}
			return fallback();
		}

		VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		allocInfo.allocationSize = total;
		allocInfo.memoryTypeIndex = memoryType;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS)
		{
			for (const BufferRequest& request : requests)
			{
				vkDestroyBuffer(device_, request.buffer->buffer, nullptr);
				*request.buffer = detail::Buffer{};
			}
			throw std::runtime_error("createBuffersInOneAllocation: vkAllocateMemory failed");
		}

		for (size_t i = 0; i < requests.size(); ++i)
		{
			VK_CHECK(vkBindBufferMemory(device_, requests[i].buffer->buffer, memory, offsets[i]));
		}

		if ((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
		{
			void* base = nullptr;
			VK_CHECK(vkMapMemory(device_, memory, 0, VK_WHOLE_SIZE, 0, &base));
			for (size_t i = 0; i < requests.size(); ++i)
			{
				if (requests[i].mapped != nullptr)
				{
					*requests[i].mapped = static_cast<char*>(base) + offsets[i];
				}
			}
		}
		return memory;
	}

	void VkApp::ensureInstanceBufferSizeForFrame(uint32_t frameIndex, VkDeviceSize requiredSize)
	{
		if (instanceBufferSizes_[frameIndex] < requiredSize)
		{
			VkDeviceSize const newCapacity =
				growInstanceBufferCapacity(instanceBufferSizes_[frameIndex], requiredSize);

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
			createBuffer(newCapacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
						instanceBufs_[frameIndex]);

			// Map the buffer persistently
			VK_CHECK(vkMapMemory(device_, instanceBufs_[frameIndex].memory, 0, newCapacity, 0, &instanceBufferMappedPerFrame_[frameIndex]));
			instanceBufferSizes_[frameIndex] = newCapacity;
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
			for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
			{
				instanceBufferNeedsSync_[i] = false;
			}
			return;
		}

		// currentFrame_'s buffer can still be stale (instanceBufferNeedsSync_[currentFrame_])
		// even when instanceDataDirty_ is already false: instanceDataDirty_ only tracks
		// whether instanceDataCache_ itself is up to date, not whether every
		// individual frame-in-flight GPU buffer has actually received a copy of
		// it yet -- see instanceBufferNeedsSync_'s own comment for why those are
		// tracked separately.
		// Until the first update after finalizeScene() every entry is rebuilt: before it,
		// the object setters do not mark objects dirty, so the dirty bits alone could
		// leave a cache entry stale -- and recordCommandBuffer() now draws from the cache.
		bool const fullRebuild = !sceneFinalized_ || !instanceCacheBuiltSinceFinalize_;
		if (!fullRebuild && !instanceDataDirty_ && !instanceBufferNeedsSync_[currentFrame_])
		{
			return;
		}

		// A removal shifts dirtyObjects_/instanceDataCache_ in lockstep with
		// _objects_ (see removeObject()), so the cache stays index-correct for
		// every surviving object without any of them needing to be marked
		// dirty -- but that also means the anyDirty loop below would see
		// nothing dirty and skip the GPU memcpy entirely, leaving the buffer
		// holding the pre-removal instance count/layout. Catch that case by its
		// only visible symptom here: the instance count no longer matches what
		// the GPU buffers were last synced to.
		bool const countChanged = instanceCount_ != static_cast<uint32_t>(_objects_.size());

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

		// Only update dirty objects (all of them on a full rebuild). AppExtras is looked up
		// once here, not per object: each lookup takes a global mutex.
		const detail::AppExtras& extras = detail::appExtras(this);
		bool anyDirty = false;
		for (size_t i = 0; i < _objects_.size(); ++i)
		{
			if (fullRebuild || dirtyObjects_[i])
			{
				instanceDataCache_[i] = makeInstanceForObject(i, extras.flagsFor(i));

				dirtyObjects_[i] = false;
				anyDirty = true;
			}
		}
		instanceCacheBuiltSinceFinalize_ = sceneFinalized_;

		if (anyDirty || countChanged)
		{
			// instanceDataCache_ just changed, so every frame-in-flight buffer's
			// existing copy of it is now stale, not just currentFrame_'s -- mark
			// all of them. Only currentFrame_'s buffer actually gets written
			// below, though (see instanceBufferNeedsSync_'s own declaration
			// comment for why): a sibling frame's flag stays set until that
			// sibling is itself currentFrame_ on some later drawFrame() call,
			// at which point drawFrame()'s own fence wait has actually verified
			// its buffer is idle.
			for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
			{
				instanceBufferNeedsSync_[i] = true;
			}
		}
		instanceDataDirty_ = false;

		if (!instanceBufferNeedsSync_[currentFrame_])
		{
			return;
		}

		// Every frame-in-flight buffer must be kept appropriately *sized* here,
		// not just currentFrame_'s: recordCommandBuffer is driven by the
		// swapchain's imageIndex/currentFrame_ on whichever frame comes next,
		// which may not be this one, and an under-sized sibling buffer causes
		// an out-of-bounds write (and a segfault) the next time that frame is
		// drawn. Twice the object count: recordCommandBuffer() writes the main pass's
		// instances to [0, N) and the shadow casters' to [N, 2N) (see
		// instanceBufferSizes_'s comment).
		VkDeviceSize instBytes = sizeof(Instance) * _objects_.size() * 2u;

		// drawFrame() only waits on currentFrame_'s own fence before calling
		// this function -- but when growth is needed below, resizing touches
		// the instance buffer for EVERY frame-in-flight index, including
		// sibling frames whose fences were never waited on here. A sibling
		// frame's command buffer, submitted by a previous drawFrame() call and
		// still executing on the GPU, can still hold a live
		// vkCmdBindVertexBuffers reference to that buffer -- destroying it out
		// from under the GPU is a genuine use-after-free. Only the growth path
		// needs the extra wait; the common case (memcpy into an
		// already-adequately-sized buffer) is already covered by
		// currentFrame_'s own fence wait in drawFrame().
		bool needsResize = false;
		for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
		{
			if (instanceBufferSizes_[frameIndex] < instBytes)
			{
				needsResize = true;
				break;
			}
		}
		if (needsResize && device_ != VK_NULL_HANDLE)
		{
			for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
			{
				if (inFlight_[frameIndex] != VK_NULL_HANDLE)
				{
					vkWaitForFences(device_, 1, &inFlight_[frameIndex], VK_TRUE, UINT64_MAX);
				}
			}
		}

		for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
		{
			ensureInstanceBufferSizeForFrame(frameIndex, instBytes);
		}

		// No data is copied here: every draw reads the per-pass ranges
		// recordCommandBuffer() writes from instanceDataCache_ into currentFrame_'s
		// buffer (the only one whose fence drawFrame() has waited on). Copying the whole
		// cache here in object order too, as this used to, was overwritten before the
		// GPU ever read it.
		instanceBufferNeedsSync_[currentFrame_] = false;
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

		// If the buffer is too small, recreate it -- with headroom (see
		// growInstanceBufferCapacity()) so building a scene via many addObject() calls
		// (a floor grid, a debug-shape pool) doesn't pay a full buffer
		// destroy/reallocate for every single one of them.
		if (instanceBuf.size < instBytes)
		{
			VkDeviceSize const newCapacity = growInstanceBufferCapacity(instanceBuf.size, instBytes);
			if (instanceBuf.buffer != VK_NULL_HANDLE)
			{
				vkDestroyBuffer(device_, instanceBuf.buffer, nullptr);
				vkFreeMemory(device_, instanceBuf.memory, nullptr);
			}
			createBuffer(newCapacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
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
