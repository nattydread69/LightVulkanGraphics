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
#include "LightVulkanGraphicsLogging.h"
#include "RiggedObject.h"
#include "RiggedSkinning.h"
#include "SceneGraph.h"

#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace lightGraphics
{
	using detail::Buffer;
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

		std::string makeObjectIndexMessage(const char* operation, size_t index, size_t size)
		{
			std::ostringstream message;
			message << operation << " index " << index << " is out of range for "
			        << size << " objects";
			return message.str();
		}

		bool isFiniteVec2(const glm::vec2& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y);
		}

		bool isFiniteVec3(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
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

		std::string normalizeEnvironmentToken(std::string value)
		{
			std::transform(value.begin(),
			               value.end(),
			               value.begin(),
			               [](unsigned char c)
			               {
				               if (c == '_' || c == ' ')
				               {
					               return '-';
				               }
				               return static_cast<char>(std::tolower(c));
			               });
			return value;
		}

		std::optional<detail::RiggedSkinningBindCorrectionMode> riggedSkinningModeFromEnvironment()
		{
			const auto value = getEnvironmentVariable("LIGHT_VULKAN_GRAPHICS_RIGGED_SKINNING_MODE");
			if (!value || value->empty())
			{
				return std::nullopt;
			}

			const std::string mode = normalizeEnvironmentToken(*value);
			if (mode == "offset" || mode == "offset-only" || mode == "gpu")
			{
				return detail::RiggedSkinningBindCorrectionMode::OffsetOnly;
			}
			if (mode == "mesh-bind" || mode == "meshbind" || mode == "cpu")
			{
				return detail::RiggedSkinningBindCorrectionMode::MeshBind;
			}
			if (mode == "mesh-bind-without-scale" ||
			    mode == "mesh-bind-no-scale" ||
			    mode == "meshbind-without-scale" ||
			    mode == "meshbind-no-scale")
			{
				return detail::RiggedSkinningBindCorrectionMode::MeshBindWithoutScale;
			}

			return std::nullopt;
		}

		// LIGHT_VULKAN_GRAPHICS_SKINNING_DEVICE=cpu forces CPU vertex skinning (for
		// comparison or as a workaround); anything else, or unset, allows the GPU path.
		// Distinct from LIGHT_VULKAN_GRAPHICS_RIGGED_SKINNING_MODE above, which picks
		// the bind-pose correction and whose "gpu"/"cpu" spellings are historical aliases.
		bool cpuSkinningRequestedByEnvironment()
		{
			const auto value = getEnvironmentVariable("LIGHT_VULKAN_GRAPHICS_SKINNING_DEVICE");
			return value && normalizeEnvironmentToken(*value) == "cpu";
		}

		// rigged_skin.comp's push constant block; keep the two in sync.
		struct SkinningPush
		{
			uint32_t vertexCount = 0;
			uint32_t boneBase = 0;
			uint32_t boneCount = 0;
		};
		constexpr uint32_t kSkinningWorkgroupSize = 64; // rigged_skin.comp's local_size_x

		// The skeleton bone buildRiggedFinalBoneMatrix() resolves a mesh bone to (its
		// cached index, else a name lookup; -1 when neither finds one).
		int resolveRiggedGlobalBoneIndex(const RiggedModel& model, const Bone& meshBone)
		{
			if (meshBone.cachedGlobalBoneIndex >= 0)
			{
				return meshBone.cachedGlobalBoneIndex;
			}
			const auto it = model.boneMapping.find(meshBone.name);
			return it != model.boneMapping.end() ? it->second : -1;
		}

		// Bitwise equality: two matrices that compare equal here give bit-identical
		// results from the same arithmetic.
		bool sameBits(const glm::mat4& a, const glm::mat4& b)
		{
			return std::memcmp(&a, &b, sizeof(glm::mat4)) == 0;
		}

		// rigged_skin.comp reads RiggedVertex as 16 tightly packed 32-bit words and
		// writes detail::Vertex as 8 floats.
		static_assert(sizeof(RiggedVertex) == 16 * sizeof(uint32_t), "rigged_skin.comp expects a 64-byte RiggedVertex");
		static_assert(offsetof(RiggedVertex, boneWeights) == 8 * sizeof(float), "rigged_skin.comp: boneWeights at word 8");
		static_assert(offsetof(RiggedVertex, boneIndices) == 12 * sizeof(float), "rigged_skin.comp: boneIndices at word 12");
		static_assert(sizeof(Vertex) == 8 * sizeof(float), "rigged_skin.comp expects a 32-byte detail::Vertex");

		detail::RiggedSkinningBindCorrectionMode defaultRiggedSkinningModeForDevice(VkPhysicalDeviceType deviceType)
		{
			return deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU
			    ? detail::RiggedSkinningBindCorrectionMode::MeshBind
			    : detail::RiggedSkinningBindCorrectionMode::OffsetOnly;
		}

		const char* riggedSkinningModeName(detail::RiggedSkinningBindCorrectionMode mode)
		{
			switch (mode)
			{
				case detail::RiggedSkinningBindCorrectionMode::OffsetOnly:
					return "offset-only";
				case detail::RiggedSkinningBindCorrectionMode::MeshBind:
					return "mesh-bind";
				case detail::RiggedSkinningBindCorrectionMode::MeshBindWithoutScale:
					return "mesh-bind-without-scale";
				default:
					return "unknown";
			}
		}

		struct RiggedSkinningPoseStats
		{
			glm::vec3 min{std::numeric_limits<float>::max()};
			glm::vec3 max{std::numeric_limits<float>::lowest()};
			float maxExtent = 0.0f;
			float maxEdgeRatio = 0.0f;
			size_t nonFiniteVertices = 0;
			bool valid = false;

			void addPoint(const glm::vec3& point)
			{
				min = glm::min(min, point);
				max = glm::max(max, point);
				valid = true;
			}

			void finalize()
			{
				if (!valid)
				{
					maxExtent = 0.0f;
					return;
				}

				const glm::vec3 size = max - min;
				maxExtent = std::max({size.x, size.y, size.z});
			}
		};

		glm::vec3 skinRiggedPosition(const RiggedVertex& vertex,
		                             const std::vector<glm::mat4>& finalBoneMatrices)
		{
			glm::vec4 blended(0.0f);
			bool usedWeights = false;
			for (int k = 0; k < 4; ++k)
			{
				const int boneIndex = vertex.boneIndices[k];
				const float weight = vertex.boneWeights[k];
				if (boneIndex >= 0 &&
				    boneIndex < static_cast<int>(finalBoneMatrices.size()) &&
				    weight > 0.0f)
				{
					blended +=
					    (finalBoneMatrices[boneIndex] * glm::vec4(vertex.position, 1.0f)) *
					    weight;
					usedWeights = true;
				}
			}

			return usedWeights ? glm::vec3(blended) : vertex.position;
		}

		RiggedSkinningPoseStats evaluateRiggedSkinningPose(
		    const RiggedModel& model,
		    const std::vector<glm::mat4>& boneTransforms,
		    detail::RiggedSkinningBindCorrectionMode mode)
		{
			RiggedSkinningPoseStats stats;
			for (const auto& mesh : model.meshes)
			{
				if (mesh.vertices.empty())
				{
					continue;
				}

				std::vector<glm::mat4> finalBoneMatrices(mesh.bones.size(), glm::mat4(1.0f));
				if (!mesh.bones.empty() && !boneTransforms.empty())
				{
					for (size_t i = 0; i < mesh.bones.size(); ++i)
					{
						finalBoneMatrices[i] =
						    detail::buildRiggedFinalBoneMatrix(
						        model,
						        mesh,
						        mesh.bones[i],
						        boneTransforms,
						        mode);
					}
				}

				std::vector<glm::vec3> skinned(mesh.vertices.size(), glm::vec3(0.0f));
				for (size_t vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex)
				{
					const glm::vec3 position =
					    skinRiggedPosition(mesh.vertices[vertexIndex], finalBoneMatrices);
					skinned[vertexIndex] = position;
					if (!isFiniteVec3(position))
					{
						++stats.nonFiniteVertices;
						continue;
					}

					stats.addPoint(position);
				}

				for (size_t index = 0; index + 2 < mesh.indices.size(); index += 3)
				{
					const uint32_t tri[3] = {
					    mesh.indices[index + 0],
					    mesh.indices[index + 1],
					    mesh.indices[index + 2]
					};
					if (tri[0] >= mesh.vertices.size() ||
					    tri[1] >= mesh.vertices.size() ||
					    tri[2] >= mesh.vertices.size())
					{
						continue;
					}

					for (int edge = 0; edge < 3; ++edge)
					{
						const uint32_t a = tri[edge];
						const uint32_t b = tri[(edge + 1) % 3];
						const float sourceLength =
						    glm::length(mesh.vertices[a].position - mesh.vertices[b].position);
						const float posedLength = glm::length(skinned[a] - skinned[b]);
						if (sourceLength > 1.0e-6f && std::isfinite(posedLength))
						{
							stats.maxEdgeRatio =
							    std::max(stats.maxEdgeRatio, posedLength / sourceLength);
						}
					}
				}
			}

			stats.finalize();
			return stats;
		}

		RiggedSkinningPoseStats evaluateRiggedSourcePose(const RiggedModel& model)
		{
			RiggedSkinningPoseStats stats;
			for (const auto& mesh : model.meshes)
			{
				for (const auto& vertex : mesh.vertices)
				{
					if (!isFiniteVec3(vertex.position))
					{
						++stats.nonFiniteVertices;
						continue;
					}

					stats.addPoint(vertex.position);
				}
			}

			stats.finalize();
			return stats;
		}

		std::string riggedPoseStatsSummary(const RiggedSkinningPoseStats& stats)
		{
			std::ostringstream message;
			if (!stats.valid)
			{
				message << "invalid";
				return message.str();
			}

			message << "extent=" << stats.maxExtent
			        << ", edgeRatio=" << stats.maxEdgeRatio
			        << ", nonFinite=" << stats.nonFiniteVertices;
			return message.str();
		}

		detail::RiggedSkinningBindCorrectionMode chooseRiggedSkinningMode(
		    const RiggedModel& model,
		    const std::vector<glm::mat4>& boneTransforms,
		    detail::RiggedSkinningBindCorrectionMode defaultMode)
		{
			const std::array<detail::RiggedSkinningBindCorrectionMode, 3> modes = {
			    detail::RiggedSkinningBindCorrectionMode::OffsetOnly,
			    detail::RiggedSkinningBindCorrectionMode::MeshBind,
			    detail::RiggedSkinningBindCorrectionMode::MeshBindWithoutScale
			};

			const RiggedSkinningPoseStats sourceStats = evaluateRiggedSourcePose(model);
			const float minimumExtent = sourceStats.valid
			    ? std::max(sourceStats.maxExtent * 0.05f, 1.0e-4f)
			    : 1.0e-4f;
			std::vector<detail::RiggedSkinningModeScore> scores;
			std::size_t defaultIndex = 0;
			for (size_t i = 0; i < modes.size(); ++i)
			{
				const RiggedSkinningPoseStats stats = evaluateRiggedSkinningPose(model, boneTransforms, modes[i]);
				scores.push_back({stats.valid, stats.nonFiniteVertices, stats.maxExtent, stats.maxEdgeRatio});
				if (modes[i] == defaultMode)
				{
					defaultIndex = i;
				}
			}
			return modes[detail::selectRiggedSkinningModeIndex(scores, minimumExtent, defaultIndex)];
		}
	}

#define LVG_VK_CHECK(expr) checkVkResult((expr), #expr, __FILE__, __LINE__)
#define VK_CHECK(expr) LVG_VK_CHECK(expr)

	bool VkApp::ensureSkinningPipeline()
	{
		if (skinPipeline_ != VK_NULL_HANDLE)
		{
			return true;
		}
		if (skinPipelineAttempted_ || device_ == VK_NULL_HANDLE)
		{
			return false;
		}
		skinPipelineAttempted_ = true;

		auto useCpu = [this](const std::string& reason)
		{
			const std::string message = "[RiggedMesh] Skinning vertices on the CPU (" + reason + ")";
			if (logCallback_ || debugOutput)
			{
				logMessage(LogLevel::Info, message);
			}
			else
			{
				consoleInfoStream() << message << std::endl;
			}
			return false;
		};

		if (cpuSkinningRequestedByEnvironment())
		{
			return useCpu("LIGHT_VULKAN_GRAPHICS_SKINNING_DEVICE=cpu");
		}

		uint32_t familyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, nullptr);
		std::vector<VkQueueFamilyProperties> families(familyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, families.data());
		if (qFamGfx >= familyCount || (families[qFamGfx].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)
		{
			return useCpu("graphics queue has no compute support");
		}

		std::vector<char> code;
		try
		{
			code = readFile(findShaderPath("rigged_skin.comp.spv"));
		}
		catch (const std::exception& e)
		{
			logMessage(LogLevel::Warning, std::string("[RiggedMesh] ") + e.what());
			return useCpu("rigged_skin.comp.spv not found");
		}

		std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
		for (uint32_t i = 0; i < bindings.size(); ++i)
		{
			bindings[i].binding = i;
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		}
		VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();
		VK_CHECK(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &skinSetLayout_));

		VkPushConstantRange pushRange{};
		pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushRange.offset = 0;
		pushRange.size = sizeof(SkinningPush);
		VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &skinSetLayout_;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushRange;
		VK_CHECK(vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &skinPipelineLayout_));

		VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
		moduleInfo.codeSize = code.size();
		moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
		VkShaderModule module = VK_NULL_HANDLE;
		VK_CHECK(vkCreateShaderModule(device_, &moduleInfo, nullptr, &module));

		VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
		pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		pipelineInfo.stage.module = module;
		pipelineInfo.stage.pName = "main";
		pipelineInfo.layout = skinPipelineLayout_;
		const VkResult result =
		    vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &skinPipeline_);
		vkDestroyShaderModule(device_, module, nullptr);
		if (result != VK_SUCCESS)
		{
			skinPipeline_ = VK_NULL_HANDLE;
			destroySkinningPipeline();
			skinPipelineAttempted_ = true;
			return useCpu("compute pipeline creation failed");
		}

		const std::string message = "[RiggedMesh] Skinning vertices on the GPU (rigged_skin.comp)";
		if (logCallback_ || debugOutput)
		{
			logMessage(LogLevel::Info, message);
		}
		else
		{
			consoleInfoStream() << message << std::endl;
		}
		return true;
	}

	void VkApp::destroySkinningPipeline()
	{
		if (device_ != VK_NULL_HANDLE)
		{
			if (skinPipeline_ != VK_NULL_HANDLE)
			{
				vkDestroyPipeline(device_, skinPipeline_, nullptr);
			}
			if (skinPipelineLayout_ != VK_NULL_HANDLE)
			{
				vkDestroyPipelineLayout(device_, skinPipelineLayout_, nullptr);
			}
			if (skinSetLayout_ != VK_NULL_HANDLE)
			{
				vkDestroyDescriptorSetLayout(device_, skinSetLayout_, nullptr);
			}
		}
		skinPipeline_ = VK_NULL_HANDLE;
		skinPipelineLayout_ = VK_NULL_HANDLE;
		skinSetLayout_ = VK_NULL_HANDLE;
		skinPipelineAttempted_ = false;
	}

	bool VkApp::validateRiggedMesh(const RiggedMesh& mesh) const
	{
		const std::string meshLabel = mesh.materialName.empty() ? std::string("<unnamed>") : mesh.materialName;
		size_t unnormalizedWeightVertices = 0;
		for (size_t index = 0; index < mesh.indices.size(); ++index)
		{
			if (mesh.indices[index] >= mesh.vertices.size())
			{
				logMessage(LogLevel::Error,
				           "[RiggedMesh] '" + meshLabel + "' has out-of-range index " +
				           std::to_string(mesh.indices[index]) + " at element " + std::to_string(index));
				return false;
			}
		}

		for (size_t vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex)
		{
			const RiggedVertex& vertex = mesh.vertices[vertexIndex];
			if (!isFiniteVec3(vertex.position) || !isFiniteVec3(vertex.normal) || !isFiniteVec2(vertex.texCoords))
			{
				logMessage(LogLevel::Error,
				           "[RiggedMesh] '" + meshLabel + "' has non-finite source vertex data at vertex " +
				           std::to_string(vertexIndex));
				return false;
			}

			float weightSum = 0.0f;
			for (int k = 0; k < 4; ++k)
			{
				const int boneIndex = vertex.boneIndices[k];
				const float weight = vertex.boneWeights[k];
				if (!std::isfinite(weight) || weight < 0.0f)
				{
					logMessage(LogLevel::Error,
					           "[RiggedMesh] '" + meshLabel + "' has invalid bone weight at vertex " +
					           std::to_string(vertexIndex));
					return false;
				}

				if (boneIndex < -1 || boneIndex >= static_cast<int>(mesh.bones.size()))
				{
					logMessage(LogLevel::Error,
					           "[RiggedMesh] '" + meshLabel + "' has invalid bone index " +
					           std::to_string(boneIndex) + " at vertex " + std::to_string(vertexIndex));
					return false;
				}

				weightSum += weight;
			}

			if (weightSum > 0.0f && std::abs(weightSum - 1.0f) > 1.0e-3f)
			{
				++unnormalizedWeightVertices;
			}
		}

		if (unnormalizedWeightVertices > 0)
		{
			logMessage(LogLevel::Warning,
			           "[RiggedMesh] '" + meshLabel + "' has " +
			           std::to_string(unnormalizedWeightVertices) +
			           " vertices whose bone weights are not normalized");
		}

		if (debugOutput)
		{
			std::ostringstream message;
			message << "[RiggedMesh] '" << meshLabel << "' validated: vertices=" << mesh.vertices.size()
			        << ", indices=" << mesh.indices.size() << ", bones=" << mesh.bones.size();
			logMessage(LogLevel::Debug, message.str());
		}

		return true;
	}

	void VkApp::handleRiggedAnimationInput()
	{
		if (!window_ || riggedInstances_.empty())
		{
			return;
		}

		// Polled through pollKey() rather than glfwGetKey() so these shortcuts
		// go quiet while the GUI owns the keyboard: N/P/O are ordinary letters,
		// and typing a filename into a Save As dialog was stepping through the
		// animation list behind it. See docs/gui/04, "Application shortcuts".

		bool nextDown = (pollKey(GLFW_KEY_N) == GLFW_PRESS) ||
						(pollKey(GLFW_KEY_RIGHT) == GLFW_PRESS);
		if (nextDown && !riggedNextKeyDown_)
		{
			for (auto& instance : riggedInstances_)
			{
				auto riggedObject = instance.object;
				if (!riggedObject)
				{
					continue;
				}

				int animationCount = riggedObject->getAnimationCount();
				if (animationCount <= 0)
				{
					continue;
				}

				const int currentIndex = riggedObject->getCurrentAnimationIndex();
				instance.activeAnimationIndex = currentIndex;
				instance.animationLoop = riggedObject->getAnimationLooping();

				int nextIndex = currentIndex;
				if (nextIndex < 0 || nextIndex >= animationCount)
				{
					nextIndex = 0;
				}
				else
				{
					nextIndex = (nextIndex + 1) % animationCount;
				}

				riggedObject->playAnimation(nextIndex, instance.animationLoop);
				instance.activeAnimationIndex = nextIndex;
			}
		}
		riggedNextKeyDown_ = nextDown;

		bool prevDown = (pollKey(GLFW_KEY_P) == GLFW_PRESS) ||
						(pollKey(GLFW_KEY_LEFT) == GLFW_PRESS);
		if (prevDown && !riggedPrevKeyDown_)
		{
			for (auto& instance : riggedInstances_)
			{
				auto riggedObject = instance.object;
				if (!riggedObject)
				{
					continue;
				}

				int animationCount = riggedObject->getAnimationCount();
				if (animationCount <= 0)
				{
					continue;
				}

				const int currentIndex = riggedObject->getCurrentAnimationIndex();
				instance.activeAnimationIndex = currentIndex;
				instance.animationLoop = riggedObject->getAnimationLooping();

				int prevIndex = currentIndex;
				if (prevIndex < 0 || prevIndex >= animationCount)
				{
					prevIndex = animationCount - 1;
				}
				else
				{
					prevIndex = (prevIndex - 1 + animationCount) % animationCount;
				}

				riggedObject->playAnimation(prevIndex, instance.animationLoop);
				instance.activeAnimationIndex = prevIndex;
			}
		}
		riggedPrevKeyDown_ = prevDown;

		bool stopDown = (pollKey(GLFW_KEY_O) == GLFW_PRESS);
		if (stopDown && !riggedStopKeyDown_)
		{
			for (auto& instance : riggedInstances_)
			{
				auto riggedObject = instance.object;
				if (!riggedObject)
				{
					continue;
				}

				riggedObject->stopAnimation();
				instance.activeAnimationIndex = -1;
			}
		}
		riggedStopKeyDown_ = stopDown;
	}

	void VkApp::createRiggedInstanceBuffers(RiggedInstanceRenderData& instance)
	{
		const bool gpu = instance.gpuSkinning;
		constexpr VkMemoryPropertyFlags kHostMemory =
		    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

		// Every buffer this instance needs, sorted into device-local and host-visible
		// memory. A Worker (12 meshes) used to make ~78 allocations here, which made
		// loading a crowd slow and could exceed the driver's allocation-count limit.
		std::vector<BufferRequest> deviceLocal;
		std::vector<BufferRequest> hostVisible;
		for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
		{
			deviceLocal.push_back({&instance.instanceBuffers[frameIndex], sizeof(Instance),
			                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr});
			hostVisible.push_back({&instance.instanceUploadBuffers[frameIndex], sizeof(Instance),
			                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &instance.instanceUploadMapped[frameIndex]});
			if (gpu)
			{
				hostVisible.push_back({&instance.boneBuffers[frameIndex],
				                       sizeof(glm::mat4) * std::max<size_t>(instance.uniqueBones.size(), 1u),
				                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &instance.boneMapped[frameIndex]});
			}
		}
		for (auto& meshData : instance.meshes)
		{
			const VkDeviceSize vertexBytes = sizeof(Vertex) * meshData.vertexCount;
			for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
			{
				deviceLocal.push_back({&meshData.vertexBuffers[frameIndex], vertexBytes,
				                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
				                           (gpu ? static_cast<VkBufferUsageFlags>(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) : 0u),
				                       nullptr});
				if (!gpu)
				{
					hostVisible.push_back({&meshData.vertexUploadBuffers[frameIndex], vertexBytes,
					                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &meshData.vertexUploadMapped[frameIndex]});
				}
			}
			deviceLocal.push_back({&meshData.indexBuffer, sizeof(uint32_t) * meshData.indexCount,
			                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr});
			if (gpu)
			{
				deviceLocal.push_back({&meshData.skinSourceBuffer, sizeof(RiggedVertex) * meshData.vertexCount,
				                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr});
			}
		}
		instance.deviceMemory = createBuffersInOneAllocation(deviceLocal, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		instance.hostMemory = createBuffersInOneAllocation(hostVisible, kHostMemory);

		// Initial data -- every mesh's indices, and on the GPU path its skin source --
		// through one staging buffer, copied by the upload batch.
		struct Copy
		{
			VkBuffer destination;
			VkDeviceSize sourceOffset;
			VkDeviceSize size;
		};
		std::vector<Copy> copies;
		VkDeviceSize stagingBytes = 0;
		for (auto& meshData : instance.meshes)
		{
			copies.push_back({meshData.indexBuffer.buffer, stagingBytes, sizeof(uint32_t) * meshData.indexCount});
			stagingBytes += copies.back().size;
			if (gpu)
			{
				copies.push_back({meshData.skinSourceBuffer.buffer, stagingBytes,
				                  sizeof(RiggedVertex) * meshData.vertexCount});
				stagingBytes += copies.back().size;
			}
		}
		if (stagingBytes == 0)
		{
			return;
		}

		detail::Buffer staging;
		createBuffer(stagingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, kHostMemory, staging);
		void* mapped = nullptr;
		VK_CHECK(vkMapMemory(device_, staging.memory, 0, stagingBytes, 0, &mapped));
		auto* const out = static_cast<char*>(mapped);
		size_t copyIndex = 0;
		std::vector<RiggedVertex> remapScratch;
		for (auto& meshData : instance.meshes)
		{
			const RiggedMesh& mesh = *meshData.mesh;
			std::memcpy(out + copies[copyIndex++].sourceOffset, mesh.indices.data(),
			            sizeof(uint32_t) * mesh.indices.size());
			if (gpu)
			{
				// The shader indexes the instance's unique bone table directly, so the
				// uploaded copy's bone indices are remapped through uniqueBoneIndex. -1
				// ("no bone") stays -1; validateRiggedMesh() already rejected any other
				// out-of-range index. Remapped in ordinary memory, then copied: the
				// staging memory is typically write-combined, where reading back (as a
				// remap in place would) is very slow.
				remapScratch.assign(mesh.vertices.begin(), mesh.vertices.end());
				for (RiggedVertex& vertex : remapScratch)
				{
					for (int k = 0; k < 4; ++k)
					{
						const int boneIndex = vertex.boneIndices[k];
						if (boneIndex >= 0 && boneIndex < static_cast<int>(meshData.uniqueBoneIndex.size()))
						{
							vertex.boneIndices[k] =
							    static_cast<int>(meshData.uniqueBoneIndex[static_cast<size_t>(boneIndex)]);
						}
					}
				}
				std::memcpy(out + copies[copyIndex++].sourceOffset, remapScratch.data(),
				            sizeof(RiggedVertex) * remapScratch.size());
			}
		}
		vkUnmapMemory(device_, staging.memory);

		VkCommandBuffer cmd = beginSingleTimeCommands();
		for (const Copy& copy : copies)
		{
			const VkBufferCopy region{copy.sourceOffset, 0, copy.size};
			vkCmdCopyBuffer(cmd, staging.buffer, copy.destination, 1, &region);
		}
		VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cmd,
		                     VK_PIPELINE_STAGE_TRANSFER_BIT,
		                     VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		                     0, 1, &barrier, 0, nullptr, 0, nullptr);
		endSingleTimeCommands(cmd);
		// Destroyed once the upload batch has completed (destroyBuffer() defers while
		// uploads are pending).
		destroyBuffer(device_, staging);
	}

	size_t VkApp::addRiggedObject(const std::shared_ptr<RiggedObject>& riggedObject)
	{
		assertOwnerThread("addRiggedObject");
		if (!riggedObject)
		{
			throw std::runtime_error("addRiggedObject: rigged object pointer is null");
		}

			auto model = riggedObject->getModel();
			if (!model)
			{
				throw std::runtime_error("addRiggedObject: rigged object has no loaded model");
			}

			RiggedInstanceRenderData instanceData;
			instanceData.object = riggedObject;
			// Pin the model this registration actually built `meshes` against (see
			// RiggedInstanceRenderData::model's comment) -- riggedObject->getModel()
			// is deliberately not re-fetched later for this purpose.
			instanceData.model = model;
			instanceData.activeAnimationIndex = riggedObject->getCurrentAnimationIndex();
			instanceData.animationLoop = riggedObject->getAnimationLooping();

			instanceData.uprightCorrection = model->axisCorrection;

			{
				const auto overrideMode = riggedSkinningModeFromEnvironment();
				const auto defaultMode = defaultRiggedSkinningModeForDevice(physicalDeviceType_);

				// Chosen once per model: the first registration judges the model's bind pose
				// (skinning it three times, plus once more for the log line), and every later
				// instance of the same RiggedModel reuses that choice, so all of a model's
				// instances skin the same way and a crowd does not repeat the work.
				for (auto it = skinningModeByModel_.begin(); it != skinningModeByModel_.end();)
				{
					it = it->second.model.expired() ? skinningModeByModel_.erase(it) : std::next(it);
				}
				const auto cached = skinningModeByModel_.find(model.get());
				if (!overrideMode && cached != skinningModeByModel_.end())
				{
					instanceData.skinningCorrectionMode = cached->second.mode;
				}
				else
				{
				const auto mode = overrideMode.value_or(
				    chooseRiggedSkinningMode(*model,
				                              riggedObject->getBoneTransforms(),
				                              defaultMode));
				instanceData.skinningCorrectionMode = mode;
				if (!overrideMode)
				{
					skinningModeByModel_[model.get()] = {model, mode};
				}

				const RiggedSkinningPoseStats selectedStats =
				    evaluateRiggedSkinningPose(*model,
				                                riggedObject->getBoneTransforms(),
				                                mode);
				std::ostringstream message;
				message << "[RiggedMesh] Skinning correction mode: "
				        << riggedSkinningModeName(mode);
				if (overrideMode)
				{
					message << " (environment override";
				}
				else
				{
					message << " (auto, default "
					        << riggedSkinningModeName(defaultMode);
				}
				message << ", " << riggedPoseStatsSummary(selectedStats) << ')';
				if (logCallback_ || debugOutput)
				{
					logMessage(LogLevel::Info, message.str());
				}
				else
				{
					consoleInfoStream() << message.str() << std::endl;
				}
				} // not cached
			}

		// GPU resource creation is skipped without a device so scene content can
		// be built up (mesh validation, skinning-mode selection, handle
		// bookkeeping) headlessly, e.g. for testing. updateRiggedInstances()
		// already tolerates VK_NULL_HANDLE buffers/textures below. The mesh loop below
		// prepares each mesh's CPU-side data and textures; every GPU buffer is then
		// created at once by createRiggedInstanceBuffers().
		const bool gpuSkinning = device_ != VK_NULL_HANDLE && ensureSkinningPipeline();
		instanceData.gpuSkinning = gpuSkinning;
		// Unique final bone matrices, keyed by skeleton bone (see
		// RiggedInstanceRenderData::uniqueBones); built up mesh by mesh below.
		std::unordered_map<int, std::vector<uint32_t>> uniqueBonesBySkeletonBone;

		for (const auto& mesh : model->meshes)
		{
			if (mesh.vertices.empty() || mesh.indices.empty())
			{
				continue;
			}
			if (!validateRiggedMesh(mesh))
			{
				logMessage(LogLevel::Warning,
				           "[RiggedMesh] Skipping mesh because imported data failed validation");
				continue;
			}

			RiggedMeshRenderData meshData;
			meshData.mesh = &mesh;
			meshData.indexCount = static_cast<uint32_t>(mesh.indices.size());
			meshData.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
			if (!gpuSkinning)
			{
				meshData.skinnedVertices.resize(mesh.vertices.size());
			}

			meshData.uniqueBoneIndex.resize(mesh.bones.size());
			for (size_t boneIndex = 0; boneIndex < mesh.bones.size(); ++boneIndex)
			{
				const Bone& meshBone = mesh.bones[boneIndex];
				std::vector<uint32_t>& candidates =
				    uniqueBonesBySkeletonBone[resolveRiggedGlobalBoneIndex(*model, meshBone)];
				uint32_t found = UINT32_MAX;
				for (uint32_t candidate : candidates)
				{
					const auto& unique = instanceData.uniqueBones[candidate];
					if (sameBits(unique.bone->offsetMatrix, meshBone.offsetMatrix) &&
					    sameBits(unique.mesh->globalBindTransform, mesh.globalBindTransform))
					{
						found = candidate;
						break;
					}
				}
				if (found == UINT32_MAX)
				{
					found = static_cast<uint32_t>(instanceData.uniqueBones.size());
					instanceData.uniqueBones.push_back({&mesh, &meshBone});
					candidates.push_back(found);
				}
				meshData.uniqueBoneIndex[boneIndex] = found;
			}

			if (device_ != VK_NULL_HANDLE)
			{
				if (!mesh.diffuseTexturePath.empty())
				{
					if (debugOutput)
					{
						logMessage(LogLevel::Debug, "[RiggedMesh] Loading texture: " + mesh.diffuseTexturePath);
					}
					meshData.texture = getOrCreateTexture(mesh.diffuseTexturePath);
				}

				if (!meshData.texture && mesh.embeddedTexture)
				{
					const std::string cacheKey = !mesh.embeddedTextureKey.empty()
						? mesh.embeddedTextureKey
						: mesh.diffuseTexturePath;

					if (!cacheKey.empty())
					{
						auto cacheIt = textureCache_.find(cacheKey);
						if (cacheIt != textureCache_.end())
						{
							meshData.texture = cacheIt->second;
						}
						else
						{
							meshData.texture = createTextureFromEmbedded(*mesh.embeddedTexture, cacheKey);
							if (meshData.texture)
							{
								textureCache_[cacheKey] = meshData.texture;
							}
						}
					}
					else
					{
						meshData.texture = createTextureFromEmbedded(*mesh.embeddedTexture, "embedded_texture");
					}
				}

				if (!meshData.texture)
				{
					meshData.texture = getOrCreateSolidColorTexture(mesh.diffuseColor);
					if (debugOutput)
					{
						std::ostringstream message;
						message << "[RiggedMesh] Using material color fallback for '" << mesh.materialName
						        << "' (" << mesh.diffuseColor.r << ", " << mesh.diffuseColor.g << ", "
						        << mesh.diffuseColor.b << ", " << mesh.diffuseColor.a << ")";
						logMessage(LogLevel::Debug, message.str());
					}
				}

				if (!meshData.texture)
				{
					if (debugOutput)
					{
						logMessage(LogLevel::Debug, "[RiggedMesh] Texture and material color fallback unavailable; using default texture");
					}
					meshData.texture = defaultTexture_;
				}
			}

			instanceData.meshes.push_back(std::move(meshData));
		}

		if (device_ != VK_NULL_HANDLE)
		{
			createRiggedInstanceBuffers(instanceData);
		}

		if (gpuSkinning && !instanceData.meshes.empty())
		{
			const uint32_t setCount = static_cast<uint32_t>(instanceData.meshes.size()) * MAX_FRAMES_IN_FLIGHT;
			VkDescriptorPoolSize poolSize{};
			poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			poolSize.descriptorCount = setCount * 3u;
			VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
			poolInfo.maxSets = setCount;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes = &poolSize;
			VK_CHECK(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &instanceData.skinDescriptorPool));

			for (auto& meshData : instanceData.meshes)
			{
				std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts;
				layouts.fill(skinSetLayout_);
				VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
				allocInfo.descriptorPool = instanceData.skinDescriptorPool;
				allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
				allocInfo.pSetLayouts = layouts.data();
				VK_CHECK(vkAllocateDescriptorSets(device_, &allocInfo, meshData.skinDescriptorSets));

				for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
				{
					const std::array<VkDescriptorBufferInfo, 3> bufferInfos{{
					    {meshData.skinSourceBuffer.buffer, 0, VK_WHOLE_SIZE},
					    {instanceData.boneBuffers[frameIndex].buffer, 0, VK_WHOLE_SIZE},
					    {meshData.vertexBuffers[frameIndex].buffer, 0, VK_WHOLE_SIZE},
					}};
					std::array<VkWriteDescriptorSet, 3> writes{};
					for (uint32_t binding = 0; binding < writes.size(); ++binding)
					{
						writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
						writes[binding].dstSet = meshData.skinDescriptorSets[frameIndex];
						writes[binding].dstBinding = binding;
						writes[binding].descriptorCount = 1;
						writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
						writes[binding].pBufferInfo = &bufferInfos[binding];
					}
					vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
				}
			}
		}

		riggedInstances_.push_back(std::move(instanceData));

		const std::uint32_t newIndex = static_cast<std::uint32_t>(riggedInstances_.size() - 1);
		const std::uint32_t slot = allocateHandleSlot(riggedObjectSlots_, freeRiggedObjectSlots_, newIndex);
		riggedObjectSlotForIndex_.push_back(slot);

		// Populate buffers with the current pose
		updateRiggedInstances();

		// No command-buffer rebuild here: drawFrame() re-records the frame's command
		// buffer every frame, so the new instance is drawn from the next frame on. This
		// used to call createCommandBuffers(), freeing command buffers that frames in
		// flight could still be executing -- harmless only while every upload above idled
		// the queue first.

		return riggedInstances_.size() - 1;
	}

	RiggedObjectHandle VkApp::addRiggedObjectHandle(const std::shared_ptr<RiggedObject>& riggedObject)
	{
		const size_t index = addRiggedObject(riggedObject);
		const std::uint32_t slot = riggedObjectSlotForIndex_[index];
		return RiggedObjectHandle{slot, riggedObjectSlots_[slot].generation};
	}

	void VkApp::removeRiggedObject(size_t index)
	{
		assertOwnerThread("removeRiggedObject");
		if (index >= riggedInstances_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("removeRiggedObject", index, riggedInstances_.size()));
		}

		releaseHandleSlot(riggedObjectSlots_, freeRiggedObjectSlots_, riggedObjectSlotForIndex_[index]);
		riggedObjectSlotForIndex_.erase(riggedObjectSlotForIndex_.begin() + static_cast<std::ptrdiff_t>(index));
		reindexHandleSlotsFrom(riggedObjectSlots_, riggedObjectSlotForIndex_, index);

		// Frames in flight (and pending uploads) may still read this instance's buffers
		// and descriptor sets: destroy them once those have completed, rather than idling
		// the whole device here as this used to (a visible hitch per removal). The
		// render data, including the model it points into, moves into the retire entry.
		auto removed = std::make_shared<RiggedInstanceRenderData>(std::move(riggedInstances_[index]));
		riggedInstances_.erase(riggedInstances_.begin() + static_cast<std::ptrdiff_t>(index));
		retire([this, removed]() { destroyRiggedInstance(*removed); });
		sceneGraph_->onRiggedObjectRemoved(index);
	}

	void VkApp::removeRiggedObject(RiggedObjectHandle handle)
	{
		removeRiggedObject(resolveRiggedObjectHandle(handle));
	}

	bool VkApp::isRiggedObjectHandleValid(RiggedObjectHandle handle) const noexcept
	{
		return isHandleSlotValid(riggedObjectSlots_, handle.index, handle.generation);
	}

	size_t VkApp::resolveRiggedObjectHandle(RiggedObjectHandle handle) const
	{
		return resolveHandleSlot(riggedObjectSlots_, handle.index, handle.generation, "Rigged object");
	}

	RiggedObjectHandle VkApp::riggedObjectHandleAt(size_t index) const
	{
		if (index >= riggedObjectSlotForIndex_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("riggedObjectHandleAt", index, riggedObjectSlotForIndex_.size()));
		}
		const std::uint32_t slot = riggedObjectSlotForIndex_[index];
		return RiggedObjectHandle{slot, riggedObjectSlots_[slot].generation};
	}

	void VkApp::setRiggedObjectTransformMatrixOverride(size_t index, const glm::mat4& transform)
	{
		if (index >= riggedInstances_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("setRiggedObjectTransformMatrixOverride", index, riggedInstances_.size()));
		}
		riggedInstances_[index].transformMatrixOverride = transform;
	}

	void VkApp::clearRiggedObjectTransformMatrixOverride(size_t index)
	{
		if (index >= riggedInstances_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("clearRiggedObjectTransformMatrixOverride", index, riggedInstances_.size()));
		}
		riggedInstances_[index].transformMatrixOverride.reset();
	}

	void VkApp::updateRiggedInstances()
	{
		if (riggedInstances_.empty())
		{
			return;
		}

		const uint32_t frameIndex = static_cast<uint32_t>(currentFrame_);
		if (sceneFinalized_ && device_ != VK_NULL_HANDLE && frameIndex < inFlight_.size())
		{
			VK_CHECK(vkWaitForFences(device_, 1, &inFlight_[frameIndex], VK_TRUE, UINT64_MAX));
		}

		// CPU side only: everything below writes into [frameIndex]'s persistently
		// mapped staging/bone memory (safe once its fence has signalled, above). The
		// GPU side -- the staging copies or the skinning dispatches -- is recorded into
		// the frame's own command buffer by recordRiggedUploads(), so this no longer
		// submits (and waits for) a separate upload every frame.
		for (auto& instance : riggedInstances_)
		{
			auto riggedObject = instance.object;
			if (!riggedObject)
			{
				continue;
			}

			// Use the model pinned at registration time, not riggedObject->getModel() --
			// meshData.mesh below points into that pinned model's own mesh vector, and
			// re-fetching here would desync model/mesh (and use-after-free the mesh
			// pointers) if the object's model was swapped after registration. See
			// RiggedInstanceRenderData::model's comment.
			const std::shared_ptr<RiggedModel>& model = instance.model;
			if (!model)
			{
				continue;
			}

			const auto& boneTransforms = riggedObject->getBoneTransforms();
			const auto skinningCorrectionMode = instance.skinningCorrectionMode;
			// If this instance's pose hasn't changed since last frame (e.g. a held
			// static pose), every mesh's skinnedVertices from last frame is still
			// correct — skip the O(vertices) skin-blend loop below entirely and just
			// re-upload the unchanged data (upload itself always runs: with multiple
			// frames in flight, a currently-targeted GPU buffer may still need this
			// frame's copy even though the CPU-side data itself didn't change).
			const bool poseUnchanged =
			    instance.hasLastBoneTransforms &&
			    instance.lastBoneTransforms == boneTransforms;

			// The instance's distinct final bone matrices, built once per pose change for
			// all meshes (see RiggedInstanceRenderData::uniqueBones), each from its
			// representative mesh bone exactly as buildRiggedFinalBoneMatrix() always has.
			bool anyMeshNeedsSkinning = !poseUnchanged;
			for (const auto& meshData : instance.meshes)
			{
				anyMeshNeedsSkinning = anyMeshNeedsSkinning || !meshData.hasValidSkinnedVertices;
			}
			if (anyMeshNeedsSkinning)
			{
				instance.uniqueBoneMatrices.resize(instance.uniqueBones.size());
				for (size_t u = 0; u < instance.uniqueBones.size(); ++u)
				{
					const auto& unique = instance.uniqueBones[u];
					instance.uniqueBoneMatrices[u] = detail::buildRiggedFinalBoneMatrix(
					    *model, *unique.mesh, *unique.bone, boneTransforms, skinningCorrectionMode);
				}
			}

			bool bonesNeedUpload = false;
			for (auto& meshData : instance.meshes)
			{
				const RiggedMesh* mesh = meshData.mesh;
				if (!mesh || mesh->vertices.empty())
				{
					continue;
				}

				if (!instance.gpuSkinning && meshData.skinnedVertices.size() != mesh->vertices.size())
				{
					meshData.skinnedVertices.resize(mesh->vertices.size());
					meshData.hasValidSkinnedVertices = false;
				}

				const bool canSkipSkinning = poseUnchanged && meshData.hasValidSkinnedVertices;
				if (!canSkipSkinning)
				{
				bool hasSkinning = !mesh->bones.empty() && !boneTransforms.empty();
				meshData.skinBoneCount = hasSkinning ? static_cast<uint32_t>(instance.uniqueBoneMatrices.size()) : 0u;

				// GPU skinning: the unique matrices are all the CPU produces;
				// rigged_skin.comp reads them through the remapped vertex bone indices and
				// does the per-vertex blend below on the GPU.
				if (!instance.gpuSkinning)
				{
				// This mesh's own bone table, gathered from the unique matrices.
				std::vector<glm::mat4>& finalBoneMatrices = meshData.finalBoneMatrices;
				finalBoneMatrices.clear();
				if (hasSkinning)
				{
					finalBoneMatrices.resize(mesh->bones.size(), glm::mat4(1.0f));
					for (size_t i = 0; i < mesh->bones.size(); ++i)
					{
						finalBoneMatrices[i] = instance.uniqueBoneMatrices[meshData.uniqueBoneIndex[i]];
					}
				}

				glm::vec2 uvMin(std::numeric_limits<float>::max());
				glm::vec2 uvMax(std::numeric_limits<float>::lowest());
				bool loggedInvalidSkinnedVertex = false;

				for (size_t v = 0; v < mesh->vertices.size(); ++v)
				{
					const RiggedVertex& src = mesh->vertices[v];
					glm::vec4 blendedPos(0.0f);
					glm::vec4 blendedNrm(0.0f);
					bool usedWeights = false;

					if (hasSkinning)
					{
						for (int k = 0; k < 4; ++k)
						{
							int boneIndex = src.boneIndices[k];
							float weight = src.boneWeights[k];
							if (boneIndex >= 0 &&
							    boneIndex < static_cast<int>(finalBoneMatrices.size()) &&
							    weight > 0.0f)
							{
								const glm::mat4& mat = finalBoneMatrices[boneIndex];
								blendedPos += (mat * glm::vec4(src.position, 1.0f)) * weight;
								blendedNrm += (mat * glm::vec4(src.normal, 0.0f)) * weight;
								usedWeights = true;
							}
						}
					}

					if (!usedWeights)
					{
						blendedPos = glm::vec4(src.position, 1.0f);
						blendedNrm = glm::vec4(src.normal, 0.0f);
					}

					glm::vec3 finalPos = glm::vec3(blendedPos);
					glm::vec3 finalNrm = glm::vec3(blendedNrm);
					if (!isFiniteVec3(finalPos) || !isFiniteVec3(finalNrm))
					{
						if (!loggedInvalidSkinnedVertex)
						{
							const std::string meshLabel =
							    mesh->materialName.empty() ? std::string("<unnamed>") : mesh->materialName;
							logMessage(LogLevel::Warning,
							           "[RiggedMesh] '" + meshLabel +
							           "' produced non-finite skinned vertices; using source vertices for safety");
							loggedInvalidSkinnedVertex = true;
						}

						finalPos = src.position;
						finalNrm = src.normal;
					}
					if (glm::length(finalNrm) > 0.0f)
					{
						finalNrm = glm::normalize(finalNrm);
					}

					meshData.skinnedVertices[v].pos = finalPos;
					meshData.skinnedVertices[v].nrm = finalNrm;

					// Assimp already flips V for us (aiProcess_FlipUVs), so keep coordinates as-is.
					glm::vec2 uv(src.texCoords.x, src.texCoords.y);
					meshData.skinnedVertices[v].uv = uv;
					uvMin = glm::min(uvMin, uv);
					uvMax = glm::max(uvMax, uv);
				}

				if (debugOutput)
				{
					std::ostringstream message;
					message << "[RiggedMesh] '" << mesh->materialName << "' UV range: ["
					        << uvMin.x << ", " << uvMin.y << "] -> ["
					        << uvMax.x << ", " << uvMax.y << "]";
					logMessage(LogLevel::Debug, message.str());
				}
				} // !instance.gpuSkinning

				meshData.hasValidSkinnedVertices = true;
				// The pose just changed: every frame-in-flight buffer's existing
				// copy of it is now stale, not just currentFrame_'s.
				for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
				{
					meshData.vertexBufferNeedsSync[i] = true;
				}
				} // !canSkipSkinning

				if (!meshData.vertexBufferNeedsSync[frameIndex])
				{
					continue;
				}
				if (instance.gpuSkinning)
				{
					bonesNeedUpload = true;
				}
				else if (void* frameVertexMapped = meshData.vertexUploadMapped[frameIndex];
				         frameVertexMapped != nullptr && !meshData.skinnedVertices.empty())
				{
					std::memcpy(frameVertexMapped,
					            meshData.skinnedVertices.data(),
					            sizeof(Vertex) * meshData.skinnedVertices.size());
				}
			}

			// GPU skinning: only the instance's unique bone matrices travel, into
			// [frameIndex]'s bone buffer, which every mesh's dispatch reads.
			if (bonesNeedUpload && instance.boneMapped[frameIndex] != nullptr &&
			    !instance.uniqueBoneMatrices.empty() &&
			    instance.uniqueBoneMatrices.size() * sizeof(glm::mat4) <= instance.boneBuffers[frameIndex].size)
			{
				std::memcpy(instance.boneMapped[frameIndex],
				            instance.uniqueBoneMatrices.data(),
				            sizeof(glm::mat4) * instance.uniqueBoneMatrices.size());
			}

			instance.lastBoneTransforms = boneTransforms;
			instance.hasLastBoneTransforms = true;

			Instance riggedInstance{};
			if (instance.transformMatrixOverride)
			{
				riggedInstance.model = *instance.transformMatrixOverride * instance.uprightCorrection;
			}
			else
			{
				glm::mat4 translation = glm::translate(glm::mat4(1.0f), riggedObject->getPosition());
				glm::mat4 rotation = glm::mat4_cast(riggedObject->getRotation());
				glm::mat4 scale = glm::scale(glm::mat4(1.0f), riggedObject->getSize());
				riggedInstance.model = translation * rotation * instance.uprightCorrection * scale;
			}
			riggedInstance.color = glm::vec3(riggedObject->getColour());
			riggedInstance.shapeType = static_cast<float>(lightGraphics::ShapeType::HUMAN);

			if (void* frameInstanceMapped = instance.instanceUploadMapped[frameIndex])
			{
				std::memcpy(frameInstanceMapped, &riggedInstance, sizeof(Instance));
			}
		}
	}

	void VkApp::recordRiggedUploads(VkCommandBuffer cmd)
	{
		const uint32_t frameIndex = static_cast<uint32_t>(currentFrame_);
		if (riggedInstances_.empty() || frameIndex >= MAX_FRAMES_IN_FLIGHT)
		{
			return;
		}

		VkPipelineStageFlags srcStages = 0;
		VkAccessFlags srcAccess = 0;
		bool skinPipelineBound = false;
		for (const auto& instance : riggedInstances_)
		{
			// The instance's model matrix changes with its transform, so it is
			// copied every frame (one Instance, 80 bytes).
			const detail::Buffer& instanceUpload = instance.instanceUploadBuffers[frameIndex];
			const detail::Buffer& instanceBuffer = instance.instanceBuffers[frameIndex];
			if (instanceUpload.buffer != VK_NULL_HANDLE && instanceBuffer.buffer != VK_NULL_HANDLE)
			{
				VkBufferCopy region{0, 0, sizeof(Instance)};
				vkCmdCopyBuffer(cmd, instanceUpload.buffer, instanceBuffer.buffer, 1, &region);
				srcStages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
				srcAccess |= VK_ACCESS_TRANSFER_WRITE_BIT;
			}

			for (const auto& meshData : instance.meshes)
			{
				const detail::Buffer& vertexBuffer = meshData.vertexBuffers[frameIndex];
				if (!meshData.vertexBufferNeedsSync[frameIndex] || vertexBuffer.buffer == VK_NULL_HANDLE)
				{
					continue;
				}

				if (instance.gpuSkinning)
				{
					const VkDescriptorSet set = meshData.skinDescriptorSets[frameIndex];
					if (skinPipeline_ == VK_NULL_HANDLE || set == VK_NULL_HANDLE || meshData.vertexCount == 0)
					{
						continue;
					}
					if (!skinPipelineBound)
					{
						vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skinPipeline_);
						skinPipelineBound = true;
					}
					SkinningPush push;
					push.vertexCount = meshData.vertexCount;
					push.boneBase = 0; // every mesh indexes the instance's unique bone table
					push.boneCount = meshData.skinBoneCount;
					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skinPipelineLayout_,
					                        0, 1, &set, 0, nullptr);
					vkCmdPushConstants(cmd, skinPipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
					                   0, sizeof(push), &push);
					vkCmdDispatch(cmd, (meshData.vertexCount + kSkinningWorkgroupSize - 1) / kSkinningWorkgroupSize, 1, 1);
					srcStages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
					srcAccess |= VK_ACCESS_SHADER_WRITE_BIT;
				}
				else
				{
					const detail::Buffer& upload = meshData.vertexUploadBuffers[frameIndex];
					if (upload.buffer == VK_NULL_HANDLE || meshData.skinnedVertices.empty())
					{
						continue;
					}
					VkBufferCopy region{0, 0, sizeof(Vertex) * meshData.skinnedVertices.size()};
					vkCmdCopyBuffer(cmd, upload.buffer, vertexBuffer.buffer, 1, &region);
					srcStages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
					srcAccess |= VK_ACCESS_TRANSFER_WRITE_BIT;
				}
			}
		}

		if (srcStages == 0)
		{
			return;
		}

		// One barrier for everything above: written vertex/instance data becomes
		// visible to the vertex input of the shadow and main passes that follow.
		VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
		barrier.srcAccessMask = srcAccess;
		barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
		vkCmdPipelineBarrier(cmd,
		                     srcStages,
		                     VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		                     0,
		                     1,
		                     &barrier,
		                     0,
		                     nullptr,
		                     0,
		                     nullptr);
	}

	void VkApp::markRiggedFrameSubmitted(uint32_t frameIndex)
	{
		if (frameIndex >= MAX_FRAMES_IN_FLIGHT)
		{
			return;
		}
		for (auto& instance : riggedInstances_)
		{
			for (auto& meshData : instance.meshes)
			{
				meshData.vertexBufferNeedsSync[frameIndex] = false;
			}
		}
	}

	void VkApp::destroyRiggedInstance(RiggedInstanceRenderData& instance)
	{
		for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
		{
			if (instance.instanceUploadMapped[frameIndex] != nullptr &&
			    instance.instanceUploadBuffers[frameIndex].memory != VK_NULL_HANDLE)
			{
				vkUnmapMemory(device_, instance.instanceUploadBuffers[frameIndex].memory);
				instance.instanceUploadMapped[frameIndex] = nullptr;
			}
			destroyBuffer(device_, instance.instanceUploadBuffers[frameIndex]);
			destroyBuffer(device_, instance.instanceBuffers[frameIndex]);
			if (instance.boneMapped[frameIndex] != nullptr &&
			    instance.boneBuffers[frameIndex].memory != VK_NULL_HANDLE)
			{
				vkUnmapMemory(device_, instance.boneBuffers[frameIndex].memory);
				instance.boneMapped[frameIndex] = nullptr;
			}
			destroyBuffer(device_, instance.boneBuffers[frameIndex]);
		}
		if (instance.skinDescriptorPool != VK_NULL_HANDLE)
		{
			// Frees every mesh's skinDescriptorSets with it.
			vkDestroyDescriptorPool(device_, instance.skinDescriptorPool, nullptr);
			instance.skinDescriptorPool = VK_NULL_HANDLE;
		}
		for (auto& mesh : instance.meshes)
		{
			for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
			{
				if (mesh.vertexUploadMapped[frameIndex] != nullptr &&
				    mesh.vertexUploadBuffers[frameIndex].memory != VK_NULL_HANDLE)
				{
					vkUnmapMemory(device_, mesh.vertexUploadBuffers[frameIndex].memory);
					mesh.vertexUploadMapped[frameIndex] = nullptr;
				}
				destroyBuffer(device_, mesh.vertexUploadBuffers[frameIndex]);
				destroyBuffer(device_, mesh.vertexBuffers[frameIndex]);
			}
			destroyBuffer(device_, mesh.indexBuffer);
			destroyBuffer(device_, mesh.skinSourceBuffer);
			std::fill(std::begin(mesh.skinDescriptorSets), std::end(mesh.skinDescriptorSets), VK_NULL_HANDLE);
			mesh.skinnedVertices.clear();
			mesh.texture.reset();
		}
		instance.meshes.clear();
		// The shared allocations the buffers above were bound into (see
		// createRiggedInstanceBuffers()); after those buffers if their destruction was deferred.
		auto freeMemory = [device = device_, deviceMemory = instance.deviceMemory, hostMemory = instance.hostMemory]()
		{
			if (deviceMemory != VK_NULL_HANDLE)
			{
				vkFreeMemory(device, deviceMemory, nullptr);
			}
			if (hostMemory != VK_NULL_HANDLE)
			{
				vkFreeMemory(device, hostMemory, nullptr);
			}
		};
		if (uploadsMayReferenceResources())
		{
			retire(freeMemory);
		}
		else
		{
			freeMemory();
		}
		instance.deviceMemory = VK_NULL_HANDLE;
		instance.hostMemory = VK_NULL_HANDLE;
		instance.object.reset();
		instance.transformMatrixOverride.reset();
	}

	void VkApp::destroyRiggedInstances()
	{
		for (auto& instance : riggedInstances_)
		{
			destroyRiggedInstance(instance);
		}
		riggedInstances_.clear();
		for (std::uint32_t slot : riggedObjectSlotForIndex_)
		{
			releaseHandleSlot(riggedObjectSlots_, freeRiggedObjectSlots_, slot);
		}
		riggedObjectSlotForIndex_.clear();
	}
}
