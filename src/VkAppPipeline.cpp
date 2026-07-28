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
#include "LightVulkanGraphicsLogging.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <dlfcn.h>
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <dlfcn.h>
#include <unistd.h>
#endif

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
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

		void shaderSearchModuleAnchor()
		{
		}

		std::optional<std::filesystem::path> weaklyCanonicalPath(const std::filesystem::path& path)
		{
			std::error_code error;
			const auto canonicalPath = std::filesystem::weakly_canonical(path, error);
			if (error)
			{
				return path.lexically_normal();
			}
			return canonicalPath;
		}

		std::optional<std::string> getEnvironmentVariable(const char* name)
		{
#if defined(_WIN32)
			char* value = nullptr;
			size_t length = 0;
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

#if defined(_WIN32)
		std::optional<std::filesystem::path> getWindowsModulePath(HMODULE module)
		{
			if (module == nullptr)
			{
				return std::nullopt;
			}

			std::wstring buffer(512, L'\0');
			for (;;)
			{
				const DWORD length = GetModuleFileNameW(module, buffer.data(),
				                                        static_cast<DWORD>(buffer.size()));
				if (length == 0)
				{
					return std::nullopt;
				}

				if (length < buffer.size())
				{
					buffer.resize(length);
					return std::filesystem::path(buffer);
				}

				buffer.resize(buffer.size() * 2);
			}
		}
#endif

		std::optional<std::filesystem::path> getExecutablePath()
		{
#if defined(_WIN32)
			return getWindowsModulePath(nullptr);
#elif defined(__APPLE__)
			uint32_t size = 0;
			_NSGetExecutablePath(nullptr, &size);
			if (size == 0)
			{
				return std::nullopt;
			}

			std::string buffer(size, '\0');
			if (_NSGetExecutablePath(buffer.data(), &size) != 0)
			{
				return std::nullopt;
			}

			return weaklyCanonicalPath(std::filesystem::path(buffer.c_str()));
#elif defined(__linux__)
			std::array<char, 4096> buffer{};
			const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
			if (length <= 0)
			{
				return std::nullopt;
			}

			buffer[static_cast<size_t>(length)] = '\0';
			return weaklyCanonicalPath(std::filesystem::path(buffer.data()));
#else
			return std::nullopt;
#endif
		}

		std::optional<std::filesystem::path> getLibraryPath()
		{
#if defined(_WIN32)
			HMODULE module = nullptr;
			const BOOL loaded = GetModuleHandleExA(
			    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			    reinterpret_cast<LPCSTR>(reinterpret_cast<void*>(&shaderSearchModuleAnchor)),
			    &module);
			if (!loaded)
			{
				return std::nullopt;
			}
			return getWindowsModulePath(module);
#elif defined(__APPLE__) || defined(__linux__)
			Dl_info info{};
			if (dladdr(reinterpret_cast<void*>(&shaderSearchModuleAnchor), &info) == 0 ||
			    info.dli_fname == nullptr || info.dli_fname[0] == '\0')
			{
				return std::nullopt;
			}
			return weaklyCanonicalPath(std::filesystem::path(info.dli_fname));
#else
			return std::nullopt;
#endif
		}

		void appendShaderSearchRoots(std::vector<std::filesystem::path>& roots,
		                             const std::filesystem::path& baseDir)
		{
			if (baseDir.empty())
			{
				return;
			}

			roots.push_back(baseDir / "spv");
			roots.push_back(baseDir / "../spv");
			roots.push_back(baseDir / "../../spv");
			roots.push_back(baseDir / "../share/LightVulkanGraphics/spv");
			roots.push_back(baseDir / "../../share/LightVulkanGraphics/spv");
#if defined(__APPLE__)
			roots.push_back(baseDir / "../Resources/LightVulkanGraphics/spv");
			roots.push_back(baseDir / "../../Resources/LightVulkanGraphics/spv");
#endif
		}
	}

#define LVG_VK_CHECK(expr) checkVkResult((expr), #expr, __FILE__, __LINE__)
#define VK_CHECK(expr) LVG_VK_CHECK(expr)

	void VkApp::createDescriptorSetLayout()
	{
		VkDescriptorSetLayoutBinding uboB{};
		uboB.binding = 0;
		uboB.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboB.descriptorCount = 1;
		uboB.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutBinding lightingB{};
		lightingB.binding = 1;
		lightingB.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		lightingB.descriptorCount = 1;
		lightingB.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutBinding shadowB{};
		shadowB.binding = 2;
		shadowB.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		shadowB.descriptorCount = 1;
		shadowB.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		std::array<VkDescriptorSetLayoutBinding, 3> bindings{uboB, lightingB, shadowB};
		VkDescriptorSetLayoutCreateInfo dsli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		dsli.bindingCount = static_cast<uint32_t>(bindings.size());
		dsli.pBindings = bindings.data();
		VK_CHECK(vkCreateDescriptorSetLayout(device_, &dsli, nullptr, &descriptorSetLayout_));

		VkDescriptorSetLayoutBinding samplerBinding{};
		samplerBinding.binding = 0;
		samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerBinding.descriptorCount = 1;
		samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo samplerLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		samplerLayoutInfo.bindingCount = 1;
		samplerLayoutInfo.pBindings = &samplerBinding;
		VK_CHECK(vkCreateDescriptorSetLayout(device_, &samplerLayoutInfo, nullptr, &textureSetLayout_));

		std::array<VkDescriptorSetLayoutBinding, 2> volumeBindings{};
		for (std::uint32_t index = 0; index < volumeBindings.size(); ++index)
		{
			volumeBindings[index].binding = index;
			volumeBindings[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			volumeBindings[index].descriptorCount = 1;
			volumeBindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		}
		VkDescriptorSetLayoutCreateInfo volumeLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		volumeLayoutInfo.bindingCount = static_cast<std::uint32_t>(volumeBindings.size());
		volumeLayoutInfo.pBindings = volumeBindings.data();
		VK_CHECK(vkCreateDescriptorSetLayout(device_, &volumeLayoutInfo, nullptr, &volumeSetLayout_));
	}

	void VkApp::createGraphicsPipeline()
	{
		VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
		if (descriptorSetLayout_ != VK_NULL_HANDLE)
		{
			plci.setLayoutCount = 1;
			plci.pSetLayouts = &descriptorSetLayout_;
		}
		VK_CHECK(vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_));

		// Create all pipeline variants
		createShadowPipeline();
		createFlexibleShapePipeline();
		createWireframePipeline();
		createUnlitPipeline();
		createLinePipeline();

		// Keep the original sphere pipeline for backward compatibility
		createOriginalSpherePipeline();

		// Create rigged rendering pipeline (needs sampler descriptor set)
		createRiggedPipeline();

		createVolumePipeline();
		rebuildCustomPipelines();
	}

	void VkApp::createShadowPipeline()
	{
		if (shadowPipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, shadowPipeline_, nullptr);
			shadowPipeline_ = VK_NULL_HANDLE;
		}
		if (shadowPipelineLayout_ != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device_, shadowPipelineLayout_, nullptr);
			shadowPipelineLayout_ = VK_NULL_HANDLE;
		}
		if (shadowRenderPass_ == VK_NULL_HANDLE)
		{
			return;
		}

		auto vsCode = readFile(findShaderPath("shadow_depth.vert.spv"));
		VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
		smi.codeSize = vsCode.size();
		smi.pCode = reinterpret_cast<const uint32_t*>(vsCode.data());
		VkShaderModule vs{};
		VK_CHECK(vkCreateShaderModule(device_, &smi, nullptr, &vs));

		VkPushConstantRange pushRange{};
		pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushRange.offset = 0;
		pushRange.size = sizeof(detail::ShadowPushConstants);

		VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
		plci.pushConstantRangeCount = 1;
		plci.pPushConstantRanges = &pushRange;
		VK_CHECK(vkCreatePipelineLayout(device_, &plci, nullptr, &shadowPipelineLayout_));

		VkVertexInputBindingDescription binds[2]{};
		binds[0].binding = 0;
		binds[0].stride = sizeof(Vertex);
		binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		binds[1].binding = 1;
		binds[1].stride = sizeof(Instance);
		binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		std::array<VkVertexInputAttributeDescription, 5> attrs{};
		attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
		attrs[1] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4) * 0};
		attrs[2] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4) * 1};
		attrs[3] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4) * 2};
		attrs[4] = {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4) * 3};

		VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		vi.vertexBindingDescriptionCount = 2;
		vi.pVertexBindingDescriptions = binds;
		vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
		vi.pVertexAttributeDescriptions = attrs.data();

		VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkViewport viewport{};
		viewport.width = static_cast<float>(shadowMapSize_);
		viewport.height = static_cast<float>(shadowMapSize_);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor{{0, 0}, {shadowMapSize_, shadowMapSize_}};
		VkPipelineViewportStateCreateInfo vpci{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		vpci.viewportCount = 1;
		vpci.pViewports = &viewport;
		vpci.scissorCount = 1;
		vpci.pScissors = &scissor;

		VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_NONE;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.depthBiasEnable = VK_TRUE;
		rs.depthBiasConstantFactor = 1.25f;
		rs.depthBiasSlopeFactor = 1.75f;
		rs.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

		VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		cb.attachmentCount = 0;

		VkPipelineShaderStageCreateInfo sVS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sVS.stage = VK_SHADER_STAGE_VERTEX_BIT;
		sVS.module = vs;
		sVS.pName = "main";

		VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		gp.stageCount = 1;
		gp.pStages = &sVS;
		gp.pVertexInputState = &vi;
		gp.pInputAssemblyState = &ia;
		gp.pViewportState = &vpci;
		gp.pRasterizationState = &rs;
		gp.pMultisampleState = &ms;
		gp.pDepthStencilState = &ds;
		gp.pColorBlendState = &cb;
		gp.layout = shadowPipelineLayout_;
		gp.renderPass = shadowRenderPass_;
		gp.subpass = 0;
		VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &shadowPipeline_));
		vkDestroyShaderModule(device_, vs, nullptr);
	}

	void VkApp::createOriginalSpherePipeline()
	{
		// --- Shaders
		auto vsCode = readFile(findShaderPath("instanced_sphere.vert.spv"));
		auto fsCode = readFile(findShaderPath("instanced_sphere.frag.spv"));
		auto mkModule = [&](const std::vector<char>& code)->VkShaderModule{
			VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			smi.codeSize = code.size();
			smi.pCode = reinterpret_cast<const uint32_t*>(code.data());
			VkShaderModule m{}; VK_CHECK(vkCreateShaderModule(device_, &smi, nullptr, &m)); return m;
		};
		VkShaderModule vs = mkModule(vsCode), fs = mkModule(fsCode);

		// --- Vertex input: binding 0 = sphere verts, binding 1 = instance data
		VkVertexInputBindingDescription binds[2]{};
		binds[0].binding = 0; binds[0].stride = sizeof(Vertex);   binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		binds[1].binding = 1; binds[1].stride = sizeof(Instance); binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		std::array<VkVertexInputAttributeDescription, 7> attrs{};
		// pos (0) + nrm(1)
		attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
		attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, nrm)};
		// model matrix columns (2..5) from binding 1
		attrs[2] = {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*0};
		attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*1};
		attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*2};
		attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*3};
		// color (6)
		attrs[6] = {6, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, color)};

		VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds;
		vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();

		// Input assembly
		VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		// Viewport/scissor (fixed; no resize handling for brevity)
		VkViewport vp{0,0,(float)swapChainExtent_.width,(float)swapChainExtent_.height, 0.0f, 1.0f};
		VkRect2D sc{{0,0}, swapChainExtent_};
		VkPipelineViewportStateCreateInfo vpci{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		vpci.viewportCount=1; vpci.pViewports=&vp; vpci.scissorCount=1; vpci.pScissors=&sc;

		// Rasterization
		VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_BACK_BIT;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		// Multisample
		VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Colour blend
		VkPipelineColorBlendAttachmentState cba{};
		cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		cb.attachmentCount=1; cb.pAttachments=&cba;

		// Depth testing
		VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS;

		// Shaders stages
		VkPipelineShaderStageCreateInfo sVS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sVS.stage = VK_SHADER_STAGE_VERTEX_BIT; sVS.module=vs; sVS.pName="main";
		VkPipelineShaderStageCreateInfo sFS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sFS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; sFS.module=fs; sFS.pName="main";
		VkPipelineShaderStageCreateInfo stages[2]={sVS,sFS};

		// Pipeline
		VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		gp.stageCount=2; gp.pStages=stages;
		gp.pVertexInputState=&vi;
		gp.pInputAssemblyState=&ia;
		gp.pViewportState=&vpci;
		gp.pRasterizationState=&rs;
		gp.pMultisampleState=&ms;
		gp.pColorBlendState=&cb;
		gp.pDepthStencilState = &ds;
		gp.layout=pipelineLayout_;
		gp.renderPass=renderPass_; gp.subpass=0;
		VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &graphicsPipeline_));
		vkDestroyShaderModule(device_, vs, nullptr);
		vkDestroyShaderModule(device_, fs, nullptr);
	}

	void VkApp::createFlexibleShapePipeline()
	{
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

		// --- Shaders
		auto vsCode = readFile(findShaderPath("flexible_shape.vert.spv"));
		auto fsCode = readFile(findShaderPath("flexible_shape.frag.spv"));
		auto mkModule = [&](const std::vector<char>& code)->VkShaderModule{
			VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			smi.codeSize = code.size();
			smi.pCode = reinterpret_cast<const uint32_t*>(code.data());
			VkShaderModule m{}; VK_CHECK(vkCreateShaderModule(device_, &smi, nullptr, &m)); return m;
		};
		VkShaderModule vs = mkModule(vsCode), fs = mkModule(fsCode);

		// --- Vertex input: binding 0 = verts, binding 1 = instance data (with shape type)
		VkVertexInputBindingDescription binds[2]{};
		binds[0].binding = 0; binds[0].stride = sizeof(Vertex);   binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		binds[1].binding = 1; binds[1].stride = sizeof(Instance); binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		std::array<VkVertexInputAttributeDescription, 9> attrs{};
		// pos (0) + nrm(1) + uv(2)
		attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
		attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, nrm)};
		attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};
		// model matrix columns (3..6) from binding 1
		attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*0};
		attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*1};
		attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*2};
		attrs[6] = {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*3};
		// color (7) + shapeType (8)
		attrs[7] = {7, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, color)};
		attrs[8] = {8, 1, VK_FORMAT_R32_SFLOAT, offsetof(Instance, shapeType)};

		VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds;
		vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();

		// Input assembly
		VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		// Viewport/scissor
		VkViewport vp{0,0,(float)swapChainExtent_.width,(float)swapChainExtent_.height, 0.0f, 1.0f};
		VkRect2D sc{{0,0}, swapChainExtent_};
		VkPipelineViewportStateCreateInfo vpci{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		vpci.viewportCount=1; vpci.pViewports=&vp; vpci.scissorCount=1; vpci.pScissors=&sc;

		// Rasterization
		VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_BACK_BIT;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		// Multisample
		VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Colour blend
		VkPipelineColorBlendAttachmentState cba{};
		cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		cb.attachmentCount=1; cb.pAttachments=&cba;

		// Depth testing
		VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS;

		// Shaders stages
		VkPipelineShaderStageCreateInfo sVS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sVS.stage = VK_SHADER_STAGE_VERTEX_BIT; sVS.module=vs; sVS.pName="main";
		VkPipelineShaderStageCreateInfo sFS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sFS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; sFS.module=fs; sFS.pName="main";
		VkPipelineShaderStageCreateInfo stages[2]={sVS,sFS};

		// Pipeline
		VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		gp.stageCount=2; gp.pStages=stages;
		gp.pVertexInputState=&vi;
		gp.pInputAssemblyState=&ia;
		gp.pViewportState=&vpci;
		gp.pRasterizationState=&rs;
		gp.pMultisampleState=&ms;
		gp.pColorBlendState=&cb;
		gp.pDepthStencilState = &ds;
		gp.layout=pipelineLayout_;
		gp.renderPass=renderPass_; gp.subpass=0;
		VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &flexibleShapePipeline_));

		// Overlay debug drawing gets a fresh depth buffer later in the frame:
		// it renders through scene meshes but still self-occludes correctly.
		VkPipelineDepthStencilStateCreateInfo dsOverlay = ds;
		dsOverlay.depthTestEnable = VK_TRUE;
		dsOverlay.depthWriteEnable = VK_TRUE;
		dsOverlay.depthCompareOp = VK_COMPARE_OP_LESS;
		gp.pDepthStencilState = &dsOverlay;
		VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &flexibleShapeOverlayPipeline_));

		vkDestroyShaderModule(device_, vs, nullptr);
		vkDestroyShaderModule(device_, fs, nullptr);
	}

	void VkApp::createRiggedPipeline()
	{
		if (riggedPipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, riggedPipeline_, nullptr);
			riggedPipeline_ = VK_NULL_HANDLE;
		}
		if (riggedPipelineLayout_ != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device_, riggedPipelineLayout_, nullptr);
			riggedPipelineLayout_ = VK_NULL_HANDLE;
		}

		if (descriptorSetLayout_ == VK_NULL_HANDLE || textureSetLayout_ == VK_NULL_HANDLE)
		{
			return;
		}

		auto vsCode = readFile(findShaderPath("rigged_mesh.vert.spv"));
		auto fsCode = readFile(findShaderPath("rigged_mesh.frag.spv"));
		auto mkModule = [&](const std::vector<char>& code)->VkShaderModule{
			VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			smi.codeSize = code.size();
			smi.pCode = reinterpret_cast<const uint32_t*>(code.data());
			VkShaderModule m{}; VK_CHECK(vkCreateShaderModule(device_, &smi, nullptr, &m)); return m;
		};
		VkShaderModule vs = mkModule(vsCode), fs = mkModule(fsCode);

		VkDescriptorSetLayout setLayouts[2] = { descriptorSetLayout_, textureSetLayout_ };
		VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
		plci.setLayoutCount = 2;
		plci.pSetLayouts = setLayouts;
		VK_CHECK(vkCreatePipelineLayout(device_, &plci, nullptr, &riggedPipelineLayout_));

		VkVertexInputBindingDescription binds[2]{};
		binds[0].binding = 0; binds[0].stride = sizeof(Vertex);   binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		binds[1].binding = 1; binds[1].stride = sizeof(Instance); binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		std::array<VkVertexInputAttributeDescription, 8> attrs{};
		attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
		attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, nrm)};
		attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};
		attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*0};
		attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*1};
		attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*2};
		attrs[6] = {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*3};
		attrs[7] = {7, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, color)};

		VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		vi.vertexBindingDescriptionCount = 2;
		vi.pVertexBindingDescriptions = binds;
		vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
		vi.pVertexAttributeDescriptions = attrs.data();

		VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkViewport vp{0,0,(float)swapChainExtent_.width,(float)swapChainExtent_.height, 0.0f, 1.0f};
		VkRect2D sc{{0,0}, swapChainExtent_};
		VkPipelineViewportStateCreateInfo vpci{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		vpci.viewportCount = 1; vpci.pViewports = &vp;
		vpci.scissorCount = 1; vpci.pScissors = &sc;

		VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_NONE;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS;

		VkPipelineColorBlendAttachmentState cba{};
		cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		cb.attachmentCount = 1;
		cb.pAttachments = &cba;

		VkPipelineShaderStageCreateInfo sVS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sVS.stage = VK_SHADER_STAGE_VERTEX_BIT; sVS.module = vs; sVS.pName = "main";
		VkPipelineShaderStageCreateInfo sFS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sFS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; sFS.module = fs; sFS.pName = "main";
		VkPipelineShaderStageCreateInfo stages[2]{sVS, sFS};

		VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		gp.stageCount = 2; gp.pStages = stages;
		gp.pVertexInputState = &vi;
		gp.pInputAssemblyState = &ia;
		gp.pViewportState = &vpci;
		gp.pRasterizationState = &rs;
		gp.pMultisampleState = &ms;
		gp.pDepthStencilState = &ds;
		gp.pColorBlendState = &cb;
		gp.layout = riggedPipelineLayout_;
		gp.renderPass = renderPass_;

		if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &riggedPipeline_) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create rigged graphics pipeline");
		}

		vkDestroyShaderModule(device_, vs, nullptr);
		vkDestroyShaderModule(device_, fs, nullptr);
	}

	void VkApp::createWireframePipeline()
	{
		if (!supportsNonSolidFill_)
		{
			wireframePipeline_ = VK_NULL_HANDLE;
			return;
		}

		// --- Shaders
		auto vsCode = readFile(findShaderPath("wireframe.vert.spv"));
		auto fsCode = readFile(findShaderPath("wireframe.frag.spv"));
		auto mkModule = [&](const std::vector<char>& code)->VkShaderModule{
			VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			smi.codeSize = code.size();
			smi.pCode = reinterpret_cast<const uint32_t*>(code.data());
			VkShaderModule m{}; VK_CHECK(vkCreateShaderModule(device_, &smi, nullptr, &m)); return m;
		};
		VkShaderModule vs = mkModule(vsCode), fs = mkModule(fsCode);

		// --- Vertex input: binding 0 = verts, binding 1 = instance data
		VkVertexInputBindingDescription binds[2]{};
		binds[0].binding = 0; binds[0].stride = sizeof(Vertex);   binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		binds[1].binding = 1; binds[1].stride = sizeof(Instance); binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		std::array<VkVertexInputAttributeDescription, 7> attrs{};
		// pos (0) + nrm(1)
		attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
		attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, nrm)};
		// model matrix columns (2..5) from binding 1
		attrs[2] = {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*0};
		attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*1};
		attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*2};
		attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*3};
		// color (6)
		attrs[6] = {6, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, color)};

		VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds;
		vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();

		// Input assembly - use triangle list for wireframe (edges will be drawn as lines)
		VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		// Viewport/scissor
		VkViewport vp{0,0,(float)swapChainExtent_.width,(float)swapChainExtent_.height, 0.0f, 1.0f};
		VkRect2D sc{{0,0}, swapChainExtent_};
		VkPipelineViewportStateCreateInfo vpci{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		vpci.viewportCount=1; vpci.pViewports=&vp; vpci.scissorCount=1; vpci.pScissors=&sc;

		// Rasterization - wireframe mode
		VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rs.polygonMode = VK_POLYGON_MODE_LINE;
		rs.cullMode = VK_CULL_MODE_NONE;  // Don't cull for wireframe
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = supportsWideLines_ ? 2.0f : 1.0f;

		// Multisample
		VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Colour blend
		VkPipelineColorBlendAttachmentState cba{};
		cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		cb.attachmentCount=1; cb.pAttachments=&cba;

		// Depth testing
		VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS;

		// Shaders stages
		VkPipelineShaderStageCreateInfo sVS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sVS.stage = VK_SHADER_STAGE_VERTEX_BIT; sVS.module=vs; sVS.pName="main";
		VkPipelineShaderStageCreateInfo sFS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sFS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; sFS.module=fs; sFS.pName="main";
		VkPipelineShaderStageCreateInfo stages[2]={sVS,sFS};

		// Pipeline
		VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		gp.stageCount=2; gp.pStages=stages;
		gp.pVertexInputState=&vi;
		gp.pInputAssemblyState=&ia;
		gp.pViewportState=&vpci;
		gp.pRasterizationState=&rs;
		gp.pMultisampleState=&ms;
		gp.pColorBlendState=&cb;
		gp.pDepthStencilState = &ds;
		gp.layout=pipelineLayout_;
		gp.renderPass=renderPass_; gp.subpass=0;
		VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &wireframePipeline_));
		vkDestroyShaderModule(device_, vs, nullptr);
		vkDestroyShaderModule(device_, fs, nullptr);
	}

	void VkApp::createUnlitPipeline()
	{
		// --- Shaders
		auto vsCode = readFile(findShaderPath("unlit.vert.spv"));
		auto fsCode = readFile(findShaderPath("unlit.frag.spv"));
		auto mkModule = [&](const std::vector<char>& code)->VkShaderModule{
			VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			smi.codeSize = code.size();
			smi.pCode = reinterpret_cast<const uint32_t*>(code.data());
			VkShaderModule m{}; VK_CHECK(vkCreateShaderModule(device_, &smi, nullptr, &m)); return m;
		};
		VkShaderModule vs = mkModule(vsCode), fs = mkModule(fsCode);

		// --- Vertex input: binding 0 = verts, binding 1 = instance data
		VkVertexInputBindingDescription binds[2]{};
		binds[0].binding = 0; binds[0].stride = sizeof(Vertex);   binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		binds[1].binding = 1; binds[1].stride = sizeof(Instance); binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		std::array<VkVertexInputAttributeDescription, 7> attrs{};
		// pos (0) + nrm(1)
		attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
		attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, nrm)};
		// model matrix columns (2..5) from binding 1
		attrs[2] = {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*0};
		attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*1};
		attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*2};
		attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*3};
		// color (6)
		attrs[6] = {6, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, color)};

		VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds;
		vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();

		// Input assembly
		VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		// Viewport/scissor
		VkViewport vp{0,0,(float)swapChainExtent_.width,(float)swapChainExtent_.height, 0.0f, 1.0f};
		VkRect2D sc{{0,0}, swapChainExtent_};
		VkPipelineViewportStateCreateInfo vpci{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		vpci.viewportCount=1; vpci.pViewports=&vp; vpci.scissorCount=1; vpci.pScissors=&sc;

		// Rasterization
		VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_BACK_BIT;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		// Multisample
		VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Colour blend
		VkPipelineColorBlendAttachmentState cba{};
		cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		cb.attachmentCount=1; cb.pAttachments=&cba;

		// Depth testing
		VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS;

		// Shaders stages
		VkPipelineShaderStageCreateInfo sVS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sVS.stage = VK_SHADER_STAGE_VERTEX_BIT; sVS.module=vs; sVS.pName="main";
		VkPipelineShaderStageCreateInfo sFS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sFS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; sFS.module=fs; sFS.pName="main";
		VkPipelineShaderStageCreateInfo stages[2]={sVS,sFS};

		// Pipeline
		VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		gp.stageCount=2; gp.pStages=stages;
		gp.pVertexInputState=&vi;
		gp.pInputAssemblyState=&ia;
		gp.pViewportState=&vpci;
		gp.pRasterizationState=&rs;
		gp.pMultisampleState=&ms;
		gp.pColorBlendState=&cb;
		gp.pDepthStencilState = &ds;
		gp.layout=pipelineLayout_;
		gp.renderPass=renderPass_; gp.subpass=0;
		VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &unlitPipeline_));
		vkDestroyShaderModule(device_, vs, nullptr);
		vkDestroyShaderModule(device_, fs, nullptr);
	}

	void VkApp::createLinePipeline()
	{
		// --- Shaders
		auto vsCode = readFile(findShaderPath("unlit.vert.spv"));  // Use unlit shader for lines
		auto fsCode = readFile(findShaderPath("unlit.frag.spv"));
		auto mkModule = [&](const std::vector<char>& code)->VkShaderModule{
			VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			smi.codeSize = code.size();
			smi.pCode = reinterpret_cast<const uint32_t*>(code.data());
			VkShaderModule m{}; VK_CHECK(vkCreateShaderModule(device_, &smi, nullptr, &m)); return m;
		};
		VkShaderModule vs = mkModule(vsCode), fs = mkModule(fsCode);

		// --- Vertex input: binding 0 = verts, binding 1 = instance data
		VkVertexInputBindingDescription binds[2]{};
		binds[0].binding = 0; binds[0].stride = sizeof(Vertex);   binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		binds[1].binding = 1; binds[1].stride = sizeof(Instance); binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		std::array<VkVertexInputAttributeDescription, 7> attrs{};
		// pos (0) + nrm(1)
		attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
		attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, nrm)};
		// model matrix columns (2..5) from binding 1
		attrs[2] = {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*0};
		attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*1};
		attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*2};
		attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*3};
		// color (6)
		attrs[6] = {6, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, color)};

		VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds;
		vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();

		// Input assembly - use line list for proper line rendering
		VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

		// Viewport/scissor
		VkViewport vp{0,0,(float)swapChainExtent_.width,(float)swapChainExtent_.height, 0.0f, 1.0f};
		VkRect2D sc{{0,0}, swapChainExtent_};
		VkPipelineViewportStateCreateInfo vpci{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		vpci.viewportCount=1; vpci.pViewports=&vp; vpci.scissorCount=1; vpci.pScissors=&sc;

		// Rasterization - line mode
		VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rs.polygonMode = VK_POLYGON_MODE_LINE;
		rs.cullMode = VK_CULL_MODE_NONE;  // Don't cull for lines
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = supportsWideLines_ ? 5.0f : 1.0f;

		// Multisample
		VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Colour blend
		VkPipelineColorBlendAttachmentState cba{};
		cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		cb.attachmentCount=1; cb.pAttachments=&cba;

		// Depth testing
		VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS;

		// Dynamic state
		VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dsi{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
		dsi.dynamicStateCount = 2; dsi.pDynamicStates = dynStates;

		// Shader stages
		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vs; stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fs; stages[1].pName = "main";

		VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		gp.stageCount = 2; gp.pStages = stages;
		gp.pVertexInputState = &vi;
		gp.pInputAssemblyState = &ia;
		gp.pViewportState = &vpci;
		gp.pRasterizationState = &rs;
		gp.pMultisampleState=&ms;
		gp.pColorBlendState=&cb;
		gp.pDepthStencilState = &ds;
		gp.pDynamicState = &dsi;
		gp.layout=pipelineLayout_;
		gp.renderPass=renderPass_; gp.subpass=0;
		VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &linePipeline_));
		vkDestroyShaderModule(device_, vs, nullptr);
		vkDestroyShaderModule(device_, fs, nullptr);
	}

	// Old signature preserved for compatibility with existing call sites
	void VkApp::setRenderMode(RenderMode mode)
	{
		currentRenderMode_ = mode;
	}

	VkPipeline VkApp::getCurrentPipeline()
	{
		switch (currentRenderMode_)
		{
			case RenderMode::FLEXIBLE_SHAPES:
				return (flexibleShapePipeline_ != VK_NULL_HANDLE) ? flexibleShapePipeline_ : graphicsPipeline_;
			case RenderMode::WIREFRAME:
				return (wireframePipeline_ != VK_NULL_HANDLE) ? wireframePipeline_ : flexibleShapePipeline_;
			case RenderMode::UNLIT:
				return (unlitPipeline_ != VK_NULL_HANDLE) ? unlitPipeline_ : flexibleShapePipeline_;
			case RenderMode::LINE:
				return (linePipeline_ != VK_NULL_HANDLE) ? linePipeline_ : unlitPipeline_;
			case RenderMode::ORIGINAL_SPHERES:
			default:
				return graphicsPipeline_;
		}
	}

	void VkApp::setShaderPath(const std::string& path)
	{
		customShaderPath_ = path;
	}

	std::vector<char> VkApp::readFile(const std::string& path)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
			throw std::runtime_error("Failed to open file: " + path);
		return std::vector<char>(std::istreambuf_iterator<char>(f), {});
	}

	std::string VkApp::findShaderPath(const std::string& shaderName)
	{
		std::vector<std::filesystem::path> searchRoots;
		searchRoots.reserve(16);

		auto addRoot = [&](const std::filesystem::path& root)
		{
			if (!root.empty())
			{
				searchRoots.push_back(root);
			}
		};

		if (!customShaderPath_.empty())
		{
			addRoot(customShaderPath_);
		}

		if (const auto envPath = getEnvironmentVariable("LIGHT_VULKAN_GRAPHICS_SHADER_PATH"))
		{
			addRoot(*envPath);
		}

#ifdef LVG_BUILD_SHADER_DIR
		addRoot(LVG_BUILD_SHADER_DIR);
#endif

		if (const auto libraryPath = getLibraryPath())
		{
			appendShaderSearchRoots(searchRoots, libraryPath->parent_path());
		}

		if (const auto executablePath = getExecutablePath())
		{
			appendShaderSearchRoots(searchRoots, executablePath->parent_path());
		}

		std::error_code currentPathError;
		const auto currentPath = std::filesystem::current_path(currentPathError);
		if (!currentPathError)
		{
			appendShaderSearchRoots(searchRoots, currentPath);
		}

#ifdef LVG_DEFAULT_SHADER_INSTALL_DIR
		addRoot(LVG_DEFAULT_SHADER_INSTALL_DIR);
#endif

		std::vector<std::string> searchedPaths;
		searchedPaths.reserve(searchRoots.size());
		std::unordered_set<std::string> visitedPaths;

		for (const auto& root : searchRoots)
		{
			const auto candidatePath = weaklyCanonicalPath(root / shaderName).value_or((root / shaderName).lexically_normal());
			const std::string canonicalKey = candidatePath.generic_string();
			if (!visitedPaths.insert(canonicalKey).second)
			{
				continue;
			}

			searchedPaths.push_back(candidatePath.string());
			if (std::ifstream(candidatePath, std::ios::binary).good())
			{
				if (debugOutput)
				{
					logMessage(LogLevel::Debug, "Found shader: " + candidatePath.string());
				}
				if (shaderName.find("rigged_mesh.") == 0)
				{
					const std::string message =
					    "Using rigged shader '" + shaderName + "': " + candidatePath.string();
					if (logCallback_ || debugOutput)
					{
						logMessage(LogLevel::Info, message);
					}
					else
					{
						consoleInfoStream() << message << std::endl;
					}
				}
				return candidatePath.string();
			}
		}

		std::ostringstream message;
		message << "Failed to locate shader '" << shaderName << "'. Searched:";
		for (const auto& path : searchedPaths)
		{
			message << "\n  - " << path;
		}

		throw std::runtime_error(message.str());
	}
}
