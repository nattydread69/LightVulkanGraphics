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

#include "FBXLoader.h"
#include "RiggedObject.h"
#include "../src/RiggedSkinning.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	struct Bounds
	{
		glm::vec3 min{std::numeric_limits<float>::max()};
		glm::vec3 max{std::numeric_limits<float>::lowest()};
		bool valid = false;

		void add(const glm::vec3& point)
		{
			min = glm::min(min, point);
			max = glm::max(max, point);
			valid = true;
		}
	};

	bool isFinite(const glm::vec3& value)
	{
		return std::isfinite(value.x) &&
		       std::isfinite(value.y) &&
		       std::isfinite(value.z);
	}

	void require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	glm::vec3 skinVertex(const lightGraphics::RiggedVertex& vertex,
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
				blended += (finalBoneMatrices[boneIndex] *
				            glm::vec4(vertex.position, 1.0f)) * weight;
				usedWeights = true;
			}
		}

		return usedWeights ? glm::vec3(blended) : vertex.position;
	}

	struct PoseStats
	{
		float maxDisplacement = 0.0f;
		float maxEdgeRatio = 0.0f;
		size_t nonFiniteVertices = 0;
		Bounds bounds;
	};

	PoseStats evaluatePose(const lightGraphics::RiggedModel& model,
	                       const lightGraphics::RiggedMesh& mesh,
	                       const std::vector<glm::mat4>& boneTransforms,
	                       lightGraphics::detail::RiggedSkinningBindCorrectionMode correctionMode =
	                           lightGraphics::detail::RiggedSkinningBindCorrectionMode::OffsetOnly)
	{
		PoseStats stats;
		std::vector<glm::vec3> skinned(mesh.vertices.size(), glm::vec3(0.0f));
		std::vector<glm::mat4> finalBoneMatrices(mesh.bones.size(), glm::mat4(1.0f));

		for (size_t i = 0; i < mesh.bones.size(); ++i)
		{
			finalBoneMatrices[i] =
			    lightGraphics::detail::buildRiggedFinalBoneMatrix(
			        model,
			        mesh,
			        mesh.bones[i],
			        boneTransforms,
			        correctionMode);
		}

		for (size_t i = 0; i < mesh.vertices.size(); ++i)
		{
			const glm::vec3 position = skinVertex(mesh.vertices[i], finalBoneMatrices);
			skinned[i] = position;

			if (!isFinite(position))
			{
				++stats.nonFiniteVertices;
				continue;
			}

			stats.bounds.add(position);
			stats.maxDisplacement =
			    std::max(stats.maxDisplacement,
			             glm::length(position - mesh.vertices[i].position));
		}

		for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
		{
			const uint32_t indices[3] = {
			    mesh.indices[i + 0],
			    mesh.indices[i + 1],
			    mesh.indices[i + 2]
			};

			if (indices[0] >= mesh.vertices.size() ||
			    indices[1] >= mesh.vertices.size() ||
			    indices[2] >= mesh.vertices.size())
			{
				continue;
			}

			for (int edge = 0; edge < 3; ++edge)
			{
				const uint32_t a = indices[edge];
				const uint32_t b = indices[(edge + 1) % 3];
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

		return stats;
	}

	Bounds combinedPoseBounds(const lightGraphics::RiggedModel& model,
	                          const std::vector<glm::mat4>& boneTransforms,
	                          size_t& nonFiniteVertices,
	                          lightGraphics::detail::RiggedSkinningBindCorrectionMode correctionMode =
	                              lightGraphics::detail::RiggedSkinningBindCorrectionMode::OffsetOnly)
	{
		Bounds bounds;
		for (const auto& mesh : model.meshes)
		{
			const PoseStats stats = evaluatePose(model, mesh, boneTransforms, correctionMode);
			nonFiniteVertices += stats.nonFiniteVertices;
			if (stats.bounds.valid)
			{
				bounds.add(stats.bounds.min);
				bounds.add(stats.bounds.max);
			}
		}
		return bounds;
	}

	float maxExtent(const Bounds& bounds)
	{
		const glm::vec3 size = bounds.max - bounds.min;
		return std::max({size.x, size.y, size.z});
	}

	std::string boundsSummary(const Bounds& bounds)
	{
		std::ostringstream message;
		message << "min=(" << bounds.min.x << ", " << bounds.min.y << ", " << bounds.min.z
		        << "), max=(" << bounds.max.x << ", " << bounds.max.y << ", " << bounds.max.z
		        << "), extent=" << maxExtent(bounds);
		return message.str();
	}

	float horizontalExtentX(const Bounds& bounds)
	{
		return bounds.max.x - bounds.min.x;
	}

	void testWorkerBindPoseSkinning()
	{
#ifdef LVG_SOURCE_DIR
		const std::filesystem::path modelPath =
		    std::filesystem::path(LVG_SOURCE_DIR) / "assets" / "Worker.fbx";
#else
		const std::filesystem::path modelPath =
		    std::filesystem::path("assets") / "Worker.fbx";
#endif

		lightGraphics::FBXLoader loader;
		auto model = loader.loadModel(modelPath.string());
		require(model != nullptr, "failed to load Worker.fbx: " + loader.getLastError());
		require(!model->meshes.empty(), "Worker.fbx should contain meshes");

		lightGraphics::RiggedObject object(
		    glm::vec3(0.0f),
		    glm::vec3(1.0f),
		    glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
		    "WorkerBindPoseProbe",
		    1.0f,
		    model);

		const auto& boneTransforms = object.getBoneTransforms();
		require(boneTransforms.size() == model->bones.size(),
		        "bind pose should have one transform per model bone");

		size_t nonFiniteVertices = 0;
		const Bounds combinedBounds =
		    combinedPoseBounds(*model, boneTransforms, nonFiniteVertices);
		require(nonFiniteVertices == 0,
		        "Worker bind pose produced non-finite vertices");
		require(combinedBounds.valid,
		        "Worker bind pose produced no skinned vertices");
		require(maxExtent(combinedBounds) < 2.5f,
		        "Worker bind pose produced implausibly large bounds: " +
		        boundsSummary(combinedBounds));
		require(horizontalExtentX(combinedBounds) < 0.9f,
		        "Worker bind pose stayed in the source T-pose arm span");

		for (size_t meshIndex = 0; meshIndex < model->meshes.size(); ++meshIndex)
		{
			const auto& mesh = model->meshes[meshIndex];
			const PoseStats stats = evaluatePose(*model, mesh, boneTransforms);

			std::ostringstream label;
			label << "mesh " << meshIndex << " (" << mesh.materialName << ")";
			require(stats.nonFiniteVertices == 0,
			        label.str() + " produced non-finite bind-pose vertices");
			require(stats.bounds.valid,
			        label.str() + " produced no bind-pose vertices");
			require(stats.maxEdgeRatio < 3.5f,
			        label.str() + " stretched bind-pose triangle edges");
		}
	}

	void testWorkerAnimationBounds()
	{
#ifdef LVG_SOURCE_DIR
		const std::filesystem::path modelPath =
		    std::filesystem::path(LVG_SOURCE_DIR) / "assets" / "Worker.fbx";
#else
		const std::filesystem::path modelPath =
		    std::filesystem::path("assets") / "Worker.fbx";
#endif

		lightGraphics::FBXLoader loader;
		auto model = loader.loadModel(modelPath.string());
		require(model != nullptr, "failed to load Worker.fbx: " + loader.getLastError());
		require(!model->animations.empty(), "Worker.fbx should contain animations");

		constexpr float kMaxReasonableWorkerExtent = 4.0f;
		for (int animationIndex = 0;
		     animationIndex < static_cast<int>(model->animations.size());
		     ++animationIndex)
		{
			const auto& animation = model->animations[animationIndex];
			const float durationSeconds =
			    animation.ticksPerSecond > 0.0f
			        ? animation.duration / animation.ticksPerSecond
			        : animation.duration;

			std::vector<float> samples{0.0f};
			if (durationSeconds > 0.0f)
			{
				samples.push_back(durationSeconds * 0.5f);
				samples.push_back(std::max(0.0f, durationSeconds - (1.0f / 24.0f)));
			}

			for (float sampleTime : samples)
			{
				lightGraphics::RiggedObject object(
				    glm::vec3(0.0f),
				    glm::vec3(1.0f),
				    glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
				    "WorkerAnimationProbe",
				    1.0f,
				    model);

				object.playAnimation(animationIndex, false);
				object.updateAnimation(sampleTime);

				size_t nonFiniteVertices = 0;
				const Bounds bounds =
				    combinedPoseBounds(*model, object.getBoneTransforms(), nonFiniteVertices);

				std::ostringstream label;
				label << "animation " << animationIndex << " (" << animation.name
				      << ") at " << sampleTime << "s";
				require(nonFiniteVertices == 0,
				        label.str() + " produced non-finite vertices");
				require(bounds.valid,
				        label.str() + " produced no skinned vertices");
				require(maxExtent(bounds) < kMaxReasonableWorkerExtent,
				        label.str() + " produced implausibly large bounds");
			}
		}
	}

	void testWorkerWaveArmPose()
	{
#ifdef LVG_SOURCE_DIR
		const std::filesystem::path modelPath =
		    std::filesystem::path(LVG_SOURCE_DIR) / "assets" / "Worker.fbx";
#else
		const std::filesystem::path modelPath =
		    std::filesystem::path("assets") / "Worker.fbx";
#endif

		lightGraphics::FBXLoader loader;
		auto model = loader.loadModel(modelPath.string());
		require(model != nullptr, "failed to load Worker.fbx: " + loader.getLastError());

		const auto waveIt =
		    std::find_if(model->animations.begin(),
		                 model->animations.end(),
		                 [](const lightGraphics::Animation& animation)
		                 {
			                 return animation.name == "CharacterArmature|Wave";
		                 });
		require(waveIt != model->animations.end(),
		        "Worker.fbx should contain the Wave animation");

		lightGraphics::RiggedObject object(
		    glm::vec3(0.0f),
		    glm::vec3(1.0f),
		    glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
		    "WorkerWavePoseProbe",
		    1.0f,
		    model);

		const int animationIndex =
		    static_cast<int>(std::distance(model->animations.begin(), waveIt));
		object.playAnimation(animationIndex, false);

		const auto& firstFrameTransforms = object.getBoneTransforms();
		size_t firstFrameNonFiniteVertices = 0;
		const Bounds firstFrameBounds =
		    combinedPoseBounds(*model, firstFrameTransforms, firstFrameNonFiniteVertices);
		require(firstFrameNonFiniteVertices == 0,
		        "Worker Wave first frame produced non-finite vertices");
		require(firstFrameBounds.valid,
		        "Worker Wave first frame produced no skinned vertices");
		require(horizontalExtentX(firstFrameBounds) < 0.9f,
		        "Worker Wave first frame stayed in the source T-pose arm span");

		for (size_t meshIndex = 0; meshIndex < model->meshes.size(); ++meshIndex)
		{
			const auto& mesh = model->meshes[meshIndex];
			const PoseStats stats =
			    evaluatePose(*model, mesh, firstFrameTransforms);

			std::ostringstream label;
			label << "Worker Wave first frame mesh " << meshIndex
			      << " (" << mesh.materialName << ")";
			require(stats.nonFiniteVertices == 0,
			        label.str() + " produced non-finite vertices");
			require(stats.maxEdgeRatio < 3.5f,
			        label.str() + " distorted triangle edges");
		}

		object.updateAnimation(1.0f);

		size_t nonFiniteVertices = 0;
		const Bounds bounds =
		    combinedPoseBounds(*model, object.getBoneTransforms(), nonFiniteVertices);
		require(nonFiniteVertices == 0,
		        "Worker Wave produced non-finite vertices");
		require(bounds.valid,
		        "Worker Wave produced no skinned vertices");
		require(horizontalExtentX(bounds) < 0.9f,
		        "Worker Wave reintroduced the source T-pose arm span");
		require(bounds.max.z > firstFrameBounds.max.z + 0.05f,
		        "Worker Wave arm pose did not lift above the first frame");
	}
} // namespace

int main()
{
	try
	{
		testWorkerBindPoseSkinning();
		testWorkerAnimationBounds();
		testWorkerWaveArmPose();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Worker skinning sanity test failed: " << error.what() << '\n';
		return 1;
	}

	std::cout << "Worker skinning sanity test passed\n";
	return 0;
}
