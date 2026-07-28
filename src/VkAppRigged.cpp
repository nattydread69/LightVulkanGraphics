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
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
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

		bool isPlausibleRiggedPose(const RiggedSkinningPoseStats& stats,
		                           float minimumExtent)
		{
			return stats.valid &&
			       stats.nonFiniteVertices == 0 &&
			       stats.maxExtent >= minimumExtent &&
			       stats.maxEdgeRatio < 8.0f;
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

			std::array<RiggedSkinningPoseStats, modes.size()> stats{};
			const RiggedSkinningPoseStats sourceStats = evaluateRiggedSourcePose(model);
			const float minimumExtent = sourceStats.valid
			    ? std::max(sourceStats.maxExtent * 0.05f, 1.0e-4f)
			    : 1.0e-4f;
			float smallestPlausibleExtent = std::numeric_limits<float>::max();
			std::optional<size_t> bestIndex;
			std::optional<size_t> defaultIndex;
			for (size_t i = 0; i < modes.size(); ++i)
			{
				stats[i] = evaluateRiggedSkinningPose(model, boneTransforms, modes[i]);
				if (modes[i] == defaultMode)
				{
					defaultIndex = i;
				}

				if (!isPlausibleRiggedPose(stats[i], minimumExtent))
				{
					continue;
				}

				if (!bestIndex || stats[i].maxExtent < smallestPlausibleExtent)
				{
					bestIndex = i;
					smallestPlausibleExtent = stats[i].maxExtent;
				}
			}

			if (defaultIndex &&
			    isPlausibleRiggedPose(stats[*defaultIndex], minimumExtent) &&
			    (!bestIndex ||
			     stats[*defaultIndex].maxExtent <=
			         std::max(smallestPlausibleExtent * 1.25f,
			                  smallestPlausibleExtent + 0.25f)))
			{
				return defaultMode;
			}

			return bestIndex ? modes[*bestIndex] : defaultMode;
		}
	}

#define LVG_VK_CHECK(expr) checkVkResult((expr), #expr, __FILE__, __LINE__)
#define VK_CHECK(expr) LVG_VK_CHECK(expr)

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

		bool nextDown = (glfwGetKey(window_, GLFW_KEY_N) == GLFW_PRESS) ||
						(glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS);
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

		bool prevDown = (glfwGetKey(window_, GLFW_KEY_P) == GLFW_PRESS) ||
						(glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS);
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

		bool stopDown = (glfwGetKey(window_, GLFW_KEY_O) == GLFW_PRESS);
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

	size_t VkApp::addRiggedObject(const std::shared_ptr<RiggedObject>& riggedObject)
	{
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
			instanceData.activeAnimationIndex = riggedObject->getCurrentAnimationIndex();
			instanceData.animationLoop = riggedObject->getAnimationLooping();

			instanceData.uprightCorrection = model->axisCorrection;

			{
				const auto overrideMode = riggedSkinningModeFromEnvironment();
				const auto defaultMode = defaultRiggedSkinningModeForDevice(physicalDeviceType_);
				const auto mode = overrideMode.value_or(
				    chooseRiggedSkinningMode(*model,
				                              riggedObject->getBoneTransforms(),
				                              defaultMode));
				instanceData.skinningCorrectionMode = mode;

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
			}

		// GPU resource creation is skipped without a device so scene content can
		// be built up (mesh validation, skinning-mode selection, handle
		// bookkeeping) headlessly, e.g. for testing. updateRiggedInstances()
		// already tolerates VK_NULL_HANDLE buffers/textures below.
		if (device_ != VK_NULL_HANDLE)
		{
			for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
			{
				createBuffer(sizeof(Instance),
				             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				             instanceData.instanceBuffers[frameIndex]);
				createBuffer(sizeof(Instance),
				             VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				             instanceData.instanceUploadBuffers[frameIndex]);
				VK_CHECK(vkMapMemory(device_,
				                     instanceData.instanceUploadBuffers[frameIndex].memory,
				                     0,
				                     sizeof(Instance),
				                     0,
				                     &instanceData.instanceUploadMapped[frameIndex]));
			}
		}

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
			meshData.skinnedVertices.resize(mesh.vertices.size());

			VkDeviceSize vbSize = sizeof(Vertex) * mesh.vertices.size();
			VkDeviceSize ibSize = sizeof(uint32_t) * mesh.indices.size();

			if (device_ != VK_NULL_HANDLE)
			{
				for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
				{
					createBuffer(vbSize,
					             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					             meshData.vertexBuffers[frameIndex]);
					createBuffer(vbSize,
					             VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					             meshData.vertexUploadBuffers[frameIndex]);
					VK_CHECK(vkMapMemory(device_,
					                     meshData.vertexUploadBuffers[frameIndex].memory,
					                     0,
					                     vbSize,
					                     0,
					                     &meshData.vertexUploadMapped[frameIndex]));
				}
				createBuffer(ibSize,
				             VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				             meshData.indexBuffer);

				detail::Buffer indexUploadBuffer;
				createBuffer(ibSize,
				             VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				             indexUploadBuffer);
				void* mapped = nullptr;
				VK_CHECK(vkMapMemory(device_, indexUploadBuffer.memory, 0, ibSize, 0, &mapped));
				std::memcpy(mapped, mesh.indices.data(), static_cast<size_t>(ibSize));
				vkUnmapMemory(device_, indexUploadBuffer.memory);
				copyBuffer(indexUploadBuffer.buffer, meshData.indexBuffer.buffer, ibSize);
				destroyBuffer(device_, indexUploadBuffer);

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

		riggedInstances_.push_back(std::move(instanceData));

		const std::uint32_t newIndex = static_cast<std::uint32_t>(riggedInstances_.size() - 1);
		const std::uint32_t slot = allocateHandleSlot(riggedObjectSlots_, freeRiggedObjectSlots_, newIndex);
		riggedObjectSlotForIndex_.push_back(slot);

		// Populate buffers with the current pose
		updateRiggedInstances();

		if (sceneFinalized_)
		{
			createCommandBuffers();
		}

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
		if (index >= riggedInstances_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("removeRiggedObject", index, riggedInstances_.size()));
		}

		if (sceneFinalized_ && device_ != VK_NULL_HANDLE)
		{
			VK_CHECK(vkDeviceWaitIdle(device_));
		}

		releaseHandleSlot(riggedObjectSlots_, freeRiggedObjectSlots_, riggedObjectSlotForIndex_[index]);
		riggedObjectSlotForIndex_.erase(riggedObjectSlotForIndex_.begin() + static_cast<std::ptrdiff_t>(index));
		reindexHandleSlotsFrom(riggedObjectSlots_, riggedObjectSlotForIndex_, index);

		destroyRiggedInstance(riggedInstances_[index]);
		riggedInstances_.erase(riggedInstances_.begin() + static_cast<std::ptrdiff_t>(index));
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

		VkCommandBuffer uploadCmd = VK_NULL_HANDLE;
		auto recordUploadCopy = [&](const detail::Buffer& src, const detail::Buffer& dst, VkDeviceSize size)
		{
			if (src.buffer == VK_NULL_HANDLE || dst.buffer == VK_NULL_HANDLE || size == 0)
			{
				return;
			}

			if (uploadCmd == VK_NULL_HANDLE)
			{
				uploadCmd = beginSingleTimeCommands();
			}

			VkBufferCopy copyRegion{};
			copyRegion.srcOffset = 0;
			copyRegion.dstOffset = 0;
			copyRegion.size = size;
			vkCmdCopyBuffer(uploadCmd, src.buffer, dst.buffer, 1, &copyRegion);

			VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.buffer = dst.buffer;
			barrier.offset = 0;
			barrier.size = size;
			vkCmdPipelineBarrier(uploadCmd,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
			                     0,
			                     0,
			                     nullptr,
			                     1,
			                     &barrier,
			                     0,
			                     nullptr);
		};

		for (auto& instance : riggedInstances_)
		{
			auto riggedObject = instance.object;
			if (!riggedObject)
			{
				continue;
			}

			auto model = riggedObject->getModel();
			if (!model)
			{
				continue;
			}

			const auto& boneTransforms = riggedObject->getBoneTransforms();
			const auto skinningCorrectionMode = instance.skinningCorrectionMode;
			for (auto& meshData : instance.meshes)
			{
				const RiggedMesh* mesh = meshData.mesh;
				if (!mesh || mesh->vertices.empty())
				{
					continue;
				}

				if (meshData.skinnedVertices.size() != mesh->vertices.size())
				{
					meshData.skinnedVertices.resize(mesh->vertices.size());
				}

				bool hasSkinning = !mesh->bones.empty() && !boneTransforms.empty();
				std::vector<glm::mat4> finalBoneMatrices;
				if (hasSkinning)
				{
					finalBoneMatrices.resize(mesh->bones.size(), glm::mat4(1.0f));
					for (size_t i = 0; i < mesh->bones.size(); ++i)
					{
						finalBoneMatrices[i] =
						    detail::buildRiggedFinalBoneMatrix(
						        *model,
						        *mesh,
						        mesh->bones[i],
						        boneTransforms,
						        skinningCorrectionMode);
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

				detail::Buffer& frameVertexBuffer = meshData.vertexBuffers[frameIndex];
				detail::Buffer& frameVertexUploadBuffer = meshData.vertexUploadBuffers[frameIndex];
				void* frameVertexMapped = meshData.vertexUploadMapped[frameIndex];
				if (!meshData.skinnedVertices.empty() &&
				    frameVertexBuffer.buffer != VK_NULL_HANDLE &&
				    frameVertexUploadBuffer.memory != VK_NULL_HANDLE)
				{
					VkDeviceSize vbSize = sizeof(Vertex) * meshData.skinnedVertices.size();
					if (frameVertexMapped != nullptr)
					{
						std::memcpy(frameVertexMapped, meshData.skinnedVertices.data(), static_cast<size_t>(vbSize));
					}
					else
					{
						void* mapped = nullptr;
						VK_CHECK(vkMapMemory(device_, frameVertexUploadBuffer.memory, 0, vbSize, 0, &mapped));
						std::memcpy(mapped, meshData.skinnedVertices.data(), static_cast<size_t>(vbSize));
						vkUnmapMemory(device_, frameVertexUploadBuffer.memory);
					}
					recordUploadCopy(frameVertexUploadBuffer, frameVertexBuffer, vbSize);
				}
			}

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

			detail::Buffer& frameInstanceBuffer = instance.instanceBuffers[frameIndex];
			detail::Buffer& frameInstanceUploadBuffer = instance.instanceUploadBuffers[frameIndex];
			void* frameInstanceMapped = instance.instanceUploadMapped[frameIndex];
			if (frameInstanceBuffer.buffer != VK_NULL_HANDLE &&
			    frameInstanceUploadBuffer.memory != VK_NULL_HANDLE)
			{
				if (frameInstanceMapped != nullptr)
				{
					std::memcpy(frameInstanceMapped, &riggedInstance, sizeof(Instance));
				}
				else
				{
					void* mapped = nullptr;
					VK_CHECK(vkMapMemory(device_, frameInstanceUploadBuffer.memory, 0, sizeof(Instance), 0, &mapped));
					std::memcpy(mapped, &riggedInstance, sizeof(Instance));
					vkUnmapMemory(device_, frameInstanceUploadBuffer.memory);
				}
				recordUploadCopy(frameInstanceUploadBuffer, frameInstanceBuffer, sizeof(Instance));
			}
		}

		if (uploadCmd != VK_NULL_HANDLE)
		{
			endSingleTimeCommands(uploadCmd);
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
			mesh.skinnedVertices.clear();
			mesh.texture.reset();
		}
		instance.meshes.clear();
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
