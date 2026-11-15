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

// A small synchronous compute API (3.4): storage buffers, kernels and one blocking submission.
//
// Everything is recorded into the application's upload batch (see VkApp::beginSingleTimeCommands) and submitted with
// flushUploads(true), so compute is ordered with pending uploads and needs no queue of its own. State lives in a side table keyed
// by the VkApp, so VkApp's data layout does not grow.

#include "VkApp.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace lightGraphics
{

namespace
{

constexpr std::uint32_t kMaxPushBytes = 128;

struct GpuBufferState
{
	std::uint32_t generation = 1;
	bool alive = false;
	detail::Buffer buffer;
	std::size_t size = 0;
};

struct KernelState
{
	std::uint32_t generation = 1;
	bool alive = false;
	ComputeKernelDescription description;
	VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
};

struct AppCompute
{
	std::vector<GpuBufferState> buffers;
	std::vector<std::uint32_t> freeBuffers;
	std::vector<KernelState> kernels;
	std::vector<std::uint32_t> freeKernels;
	// One pool, reset after every runCompute (descriptor sets live for one submission).
	VkDescriptorPool pool = VK_NULL_HANDLE;
};

std::mutex& computeMutex()
{
	static std::mutex m;
	return m;
}

std::unordered_map<const void*, AppCompute>& computeTables()
{
	static std::unordered_map<const void*, AppCompute> t;
	return t;
}

void check(VkResult result, const char* operation)
{
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error(std::string(operation) + " (VkResult " + std::to_string(static_cast<int>(result)) + ")");
	}
}

GpuBufferState& resolveBuffer(AppCompute& table, GpuBufferHandle handle)
{
	if (handle.index >= table.buffers.size() || !table.buffers[handle.index].alive || table.buffers[handle.index].generation != handle.generation)
	{
		throw std::invalid_argument("GpuBuffer handle is invalid or stale");
	}
	return table.buffers[handle.index];
}

KernelState& resolveKernel(AppCompute& table, ComputeKernelHandle handle)
{
	if (handle.index >= table.kernels.size() || !table.kernels[handle.index].alive || table.kernels[handle.index].generation != handle.generation)
	{
		throw std::invalid_argument("ComputeKernel handle is invalid or stale");
	}
	return table.kernels[handle.index];
}

std::vector<char> readSpirv(const std::filesystem::path& path)
{
	std::ifstream in(path, std::ios::binary | std::ios::ate);
	if (!in)
	{
		throw std::runtime_error("Cannot open compute shader " + path.string());
	}
	const auto size = static_cast<std::size_t>(in.tellg());
	if (size == 0 || size % 4 != 0)
	{
		throw std::runtime_error("Not a SPIR-V file: " + path.string());
	}
	std::vector<char> code(size);
	in.seekg(0);
	in.read(code.data(), static_cast<std::streamsize>(size));
	return code;
}

}

GpuBufferHandle VkApp::createGpuBuffer(std::size_t byteSize)
{
	if (byteSize == 0)
	{
		throw std::invalid_argument("A GPU buffer cannot be empty");
	}
	GpuBufferState state;
	state.size = byteSize;
	createBuffer(byteSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, state.buffer);
	std::lock_guard<std::mutex> lock(computeMutex());
	AppCompute& table = computeTables()[this];
	std::uint32_t index = 0;
	if (table.freeBuffers.empty())
	{
		index = static_cast<std::uint32_t>(table.buffers.size());
		table.buffers.emplace_back();
	}
	else
	{
		index = table.freeBuffers.back();
		table.freeBuffers.pop_back();
	}
	const std::uint32_t generation = table.buffers[index].generation;
	table.buffers[index] = state;
	table.buffers[index].generation = generation;
	table.buffers[index].alive = true;
	return {index, generation};
}

void VkApp::destroyGpuBuffer(GpuBufferHandle handle)
{
	detail::Buffer buffer;
	{
		std::lock_guard<std::mutex> lock(computeMutex());
		AppCompute& table = computeTables()[this];
		GpuBufferState& s = resolveBuffer(table, handle);
		buffer = s.buffer;
		s.buffer = {};
		s.alive = false;
		++s.generation;
		table.freeBuffers.push_back(handle.index);
	}
	destroyBuffer(device_, buffer);
}

void VkApp::uploadGpuBuffer(GpuBufferHandle handle, const void* data, std::size_t byteSize, std::size_t byteOffset)
{
	VkBuffer target = VK_NULL_HANDLE;
	{
		std::lock_guard<std::mutex> lock(computeMutex());
		const GpuBufferState& s = resolveBuffer(computeTables()[this], handle);
		if (data == nullptr || byteOffset + byteSize > s.size)
		{
			throw std::invalid_argument("GPU buffer upload is out of range");
		}
		target = s.buffer.buffer;
	}
	if (byteSize == 0)
	{
		return;
	}
	detail::Buffer staging;
	createBuffer(byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging);
	void* mapped = nullptr;
	check(vkMapMemory(device_, staging.memory, 0, byteSize, 0, &mapped), "Failed to map GPU buffer staging memory");
	std::memcpy(mapped, data, byteSize);
	vkUnmapMemory(device_, staging.memory);
	VkCommandBuffer cmd = beginSingleTimeCommands();
	VkBufferCopy copy {0, byteOffset, byteSize};
	vkCmdCopyBuffer(cmd, staging.buffer, target, 1, &copy);
	VkMemoryBarrier barrier {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0,
		nullptr, 0, nullptr);
	endSingleTimeCommands(cmd);
	destroyBuffer(device_, staging);
}

void VkApp::downloadGpuBuffer(GpuBufferHandle handle, void* data, std::size_t byteSize, std::size_t byteOffset)
{
	VkBuffer source = VK_NULL_HANDLE;
	{
		std::lock_guard<std::mutex> lock(computeMutex());
		const GpuBufferState& s = resolveBuffer(computeTables()[this], handle);
		if (data == nullptr || byteOffset + byteSize > s.size)
		{
			throw std::invalid_argument("GPU buffer download is out of range");
		}
		source = s.buffer.buffer;
	}
	if (byteSize == 0)
	{
		return;
	}
	detail::Buffer staging;
	createBuffer(byteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging);
	VkCommandBuffer cmd = beginSingleTimeCommands();
	VkMemoryBarrier barrier {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0,
		nullptr, 0, nullptr);
	VkBufferCopy copy {byteOffset, 0, byteSize};
	vkCmdCopyBuffer(cmd, source, staging.buffer, 1, &copy);
	endSingleTimeCommands(cmd);
	flushUploads(true);
	void* mapped = nullptr;
	check(vkMapMemory(device_, staging.memory, 0, byteSize, 0, &mapped), "Failed to map GPU buffer readback memory");
	std::memcpy(data, mapped, byteSize);
	vkUnmapMemory(device_, staging.memory);
	destroyBuffer(device_, staging);
}

ComputeKernelHandle VkApp::createComputeKernel(const ComputeKernelDescription& description)
{
	if (description.storageBufferCount == 0 || description.storageBufferCount > 16 || description.pushConstantBytes > kMaxPushBytes ||
		description.pushConstantBytes % 4 != 0)
	{
		throw std::invalid_argument("Compute kernel needs 1..16 storage buffers and at most 128 push-constant bytes (a multiple of 4)");
	}
	const std::vector<char> code = readSpirv(description.spirv);
	KernelState state;
	state.description = description;
	VkShaderModule module = VK_NULL_HANDLE;
	try
	{
		std::vector<VkDescriptorSetLayoutBinding> bindings(description.storageBufferCount);
		for (std::uint32_t i = 0; i < description.storageBufferCount; ++i)
		{
			bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
		}
		VkDescriptorSetLayoutCreateInfo setInfo {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		setInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
		setInfo.pBindings = bindings.data();
		check(vkCreateDescriptorSetLayout(device_, &setInfo, nullptr, &state.setLayout), "Failed to create the compute set layout");
		VkPushConstantRange range {VK_SHADER_STAGE_COMPUTE_BIT, 0, description.pushConstantBytes};
		VkPipelineLayoutCreateInfo layoutInfo {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &state.setLayout;
		layoutInfo.pushConstantRangeCount = description.pushConstantBytes > 0 ? 1 : 0;
		layoutInfo.pPushConstantRanges = &range;
		check(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &state.layout), "Failed to create the compute pipeline layout");
		VkShaderModuleCreateInfo moduleInfo {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
		moduleInfo.codeSize = code.size();
		moduleInfo.pCode = reinterpret_cast<const std::uint32_t*>(code.data());
		check(vkCreateShaderModule(device_, &moduleInfo, nullptr, &module), "Failed to create the compute shader module");
		VkComputePipelineCreateInfo pipelineInfo {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
		pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr};
		pipelineInfo.layout = state.layout;
		check(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &state.pipeline), "Failed to create the compute pipeline");
	}
	catch (...)
	{
		if (module != VK_NULL_HANDLE) vkDestroyShaderModule(device_, module, nullptr);
		if (state.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device_, state.pipeline, nullptr);
		if (state.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, state.layout, nullptr);
		if (state.setLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, state.setLayout, nullptr);
		throw;
	}
	vkDestroyShaderModule(device_, module, nullptr);
	std::lock_guard<std::mutex> lock(computeMutex());
	AppCompute& table = computeTables()[this];
	std::uint32_t index = 0;
	if (table.freeKernels.empty())
	{
		index = static_cast<std::uint32_t>(table.kernels.size());
		table.kernels.emplace_back();
	}
	else
	{
		index = table.freeKernels.back();
		table.freeKernels.pop_back();
	}
	const std::uint32_t generation = table.kernels[index].generation;
	table.kernels[index] = state;
	table.kernels[index].generation = generation;
	table.kernels[index].alive = true;
	return {index, generation};
}

void VkApp::destroyComputeKernel(ComputeKernelHandle handle)
{
	KernelState state;
	{
		std::lock_guard<std::mutex> lock(computeMutex());
		AppCompute& table = computeTables()[this];
		KernelState& s = resolveKernel(table, handle);
		state = s;
		s = KernelState {s.generation + 1, false, {}, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
		table.freeKernels.push_back(handle.index);
	}
	retire([device = device_, state]()
	{
		vkDestroyPipeline(device, state.pipeline, nullptr);
		vkDestroyPipelineLayout(device, state.layout, nullptr);
		vkDestroyDescriptorSetLayout(device, state.setLayout, nullptr);
	});
}

namespace
{

class Recorder final : public ComputeRecorder
{
public:
	Recorder(VkDevice device, VkCommandBuffer cmd, VkDescriptorPool pool, AppCompute& table) : device_ {device}, cmd_ {cmd}, pool_ {pool}, table_ {table} {}

	void dispatch(ComputeKernelHandle kernel, std::vector<GpuBufferHandle> const& buffers, void const* pushConstants, std::size_t pushBytes,
		std::uint32_t gx, std::uint32_t gy, std::uint32_t gz) override
	{
		KernelState const& k = resolveKernel(table_, kernel);
		if (buffers.size() != k.description.storageBufferCount)
		{
			throw std::invalid_argument("dispatch: wrong number of storage buffers for the kernel");
		}
		if (pushBytes != k.description.pushConstantBytes)
		{
			throw std::invalid_argument("dispatch: push-constant size does not match the kernel");
		}
		if (needBarrier_)
		{
			VkMemoryBarrier barrier {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
		}
		needBarrier_ = true;
		VkDescriptorSetAllocateInfo allocation {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
		allocation.descriptorPool = pool_;
		allocation.descriptorSetCount = 1;
		allocation.pSetLayouts = &k.setLayout;
		VkDescriptorSet set = VK_NULL_HANDLE;
		check(vkAllocateDescriptorSets(device_, &allocation, &set), "Failed to allocate a compute descriptor set (too many dispatches in one runCompute?)");
		std::vector<VkDescriptorBufferInfo> infos(buffers.size());
		std::vector<VkWriteDescriptorSet> writes(buffers.size());
		for (std::size_t i = 0; i < buffers.size(); ++i)
		{
			GpuBufferState const& b = resolveBuffer(table_, buffers[i]);
			infos[i] = {b.buffer.buffer, 0, VK_WHOLE_SIZE};
			writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
			writes[i].dstSet = set;
			writes[i].dstBinding = static_cast<std::uint32_t>(i);
			writes[i].descriptorCount = 1;
			writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			writes[i].pBufferInfo = &infos[i];
		}
		vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
		vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, k.pipeline);
		vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, k.layout, 0, 1, &set, 0, nullptr);
		if (pushBytes > 0)
		{
			vkCmdPushConstants(cmd_, k.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<std::uint32_t>(pushBytes), pushConstants);
		}
		vkCmdDispatch(cmd_, gx, gy, gz);
	}

	void copy(GpuBufferHandle source, GpuBufferHandle destination, std::size_t bytes) override
	{
		GpuBufferState const& s = resolveBuffer(table_, source);
		GpuBufferState const& d = resolveBuffer(table_, destination);
		if (bytes > s.size || bytes > d.size)
		{
			throw std::invalid_argument("copy: out of range");
		}
		if (needBarrier_)
		{
			VkMemoryBarrier barrier {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier,
				0, nullptr, 0, nullptr);
		}
		needBarrier_ = true;
		VkBufferCopy region {0, 0, bytes};
		vkCmdCopyBuffer(cmd_, s.buffer.buffer, d.buffer.buffer, 1, &region);
	}

private:
	VkDevice device_;
	VkCommandBuffer cmd_;
	VkDescriptorPool pool_;
	AppCompute& table_;
	bool needBarrier_ = false;
};

}

void VkApp::runCompute(const std::function<void(ComputeRecorder&)>& record)
{
	std::lock_guard<std::mutex> lock(computeMutex());
	AppCompute& table = computeTables()[this];
	if (table.pool == VK_NULL_HANDLE)
	{
		VkDescriptorPoolSize size {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16 * 4096};
		VkDescriptorPoolCreateInfo info {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
		info.maxSets = 4096;
		info.poolSizeCount = 1;
		info.pPoolSizes = &size;
		check(vkCreateDescriptorPool(device_, &info, nullptr, &table.pool), "Failed to create the compute descriptor pool");
	}
	// Anything uploaded earlier must be finished before descriptor sets are recycled.
	flushUploads(true);
	check(vkResetDescriptorPool(device_, table.pool, 0), "Failed to reset the compute descriptor pool");
	VkCommandBuffer cmd = beginSingleTimeCommands();
	Recorder recorder(device_, cmd, table.pool, table);
	try
	{
		record(recorder);
	}
	catch (...)
	{
		// What was recorded so far is still submitted (it is valid); the caller sees the exception.
		endSingleTimeCommands(cmd);
		flushUploads(true);
		throw;
	}
	endSingleTimeCommands(cmd);
	flushUploads(true);
}

void VkApp::destroyComputeResources()
{
	AppCompute table;
	{
		std::lock_guard<std::mutex> lock(computeMutex());
		auto it = computeTables().find(this);
		if (it == computeTables().end())
		{
			return;
		}
		table = std::move(it->second);
		computeTables().erase(it);
	}
	for (KernelState& k : table.kernels)
	{
		if (!k.alive) continue;
		vkDestroyPipeline(device_, k.pipeline, nullptr);
		vkDestroyPipelineLayout(device_, k.layout, nullptr);
		vkDestroyDescriptorSetLayout(device_, k.setLayout, nullptr);
	}
	for (GpuBufferState& b : table.buffers)
	{
		if (!b.alive) continue;
		destroyBuffer(device_, b.buffer);
	}
	if (table.pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, table.pool, nullptr);
}

}
