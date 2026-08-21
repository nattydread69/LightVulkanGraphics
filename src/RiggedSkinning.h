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

#pragma once

#include "FBXLoader.h"

#include <glm/glm.hpp>

#include <vector>

namespace lightGraphics::detail
{
	enum class RiggedSkinningBindCorrectionMode : int
	{
		OffsetOnly,
		MeshBind,
		MeshBindWithoutScale
	};

	inline glm::mat4 removeAffineScale(const glm::mat4& transform)
	{
		glm::mat4 result = transform;
		float scaleSum = 0.0f;
		int scaleCount = 0;
		for (int column = 0; column < 3; ++column)
		{
			const glm::vec3 axis = glm::vec3(result[column]);
			const float length = glm::length(axis);
			if (length > 1.0e-6f)
			{
				result[column] = glm::vec4(axis / length, 0.0f);
				scaleSum += length;
				++scaleCount;
			}
		}
		if (scaleCount > 0)
		{
			const float uniformScale = scaleSum / static_cast<float>(scaleCount);
			if (uniformScale > 1.0e-6f)
			{
				result[3] = glm::vec4(glm::vec3(result[3]) / uniformScale, result[3].w);
			}
		}
		return result;
	}

	inline glm::mat4 buildRiggedFinalBoneMatrix(const RiggedModel& model,
	                                            const RiggedMesh& mesh,
	                                            const Bone& meshBone,
	                                            const std::vector<glm::mat4>& boneTransforms,
	                                            RiggedSkinningBindCorrectionMode correctionMode =
	                                                RiggedSkinningBindCorrectionMode::OffsetOnly)
	{
		// meshBone.cachedGlobalBoneIndex is resolved once at load time (see
		// FBXLoader::loadModel) so this per-frame skinning hot path doesn't repeat
		// a string-keyed boneMapping lookup for every bone, every mesh, every
		// instance, every frame. Fall back to the map lookup if it's unset (e.g. a
		// RiggedMesh assembled by something other than FBXLoader::loadModel).
		int globalBoneIndex = meshBone.cachedGlobalBoneIndex;
		if (globalBoneIndex < 0)
		{
			auto it = model.boneMapping.find(meshBone.name);
			if (it != model.boneMapping.end())
			{
				globalBoneIndex = it->second;
			}
		}
		if (globalBoneIndex < 0 ||
		    globalBoneIndex >= static_cast<int>(boneTransforms.size()) ||
		    globalBoneIndex >= static_cast<int>(model.bones.size()))
		{
			return glm::mat4(1.0f);
		}

		const glm::mat4 globalBone = boneTransforms[globalBoneIndex];
		const Bone& globalBoneBind = model.bones[globalBoneIndex];

		// Prefer the skin cluster's own bind pose (meshBone.offsetMatrix) whenever
		// this bone actually has skin-cluster data: it is the FBX file's
		// authoritative "mesh space to bone space" bind transform, straight from
		// the exporter, and doesn't depend on reconstructing a bind pose by walking
		// node-local transforms — which can disagree with the true bind pose (see
		// the diagnostic in FBXLoader::loadModel). Bones with no skin-cluster data
		// (e.g. IK/helper joints nothing is weighted to) fall through to the
		// node-hierarchy-derived transform below, which is the only bind-pose
		// source available for them.
		if (globalBoneBind.hasSkinBindTransform &&
		    boneTransforms.size() == model.bones.size())
		{
			glm::mat4 finalMatrix =
			    model.globalInverseTransform *
			    globalBone *
			    meshBone.offsetMatrix;

			if (correctionMode == RiggedSkinningBindCorrectionMode::MeshBind)
			{
				finalMatrix *= mesh.globalBindTransform;
			}
			else if (correctionMode == RiggedSkinningBindCorrectionMode::MeshBindWithoutScale)
			{
				finalMatrix *= removeAffineScale(mesh.globalBindTransform);
			}

			return finalMatrix;
		}

		return model.globalInverseTransform *
		       globalBone *
		       glm::inverse(globalBoneBind.globalBindTransform) *
		       mesh.globalBindTransform;
	}
} // namespace lightGraphics::detail
