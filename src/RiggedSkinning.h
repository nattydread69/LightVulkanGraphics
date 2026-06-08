// SPDX-License-Identifier: LGPL-3.0-or-later

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
		auto it = model.boneMapping.find(meshBone.name);
		if (it == model.boneMapping.end() ||
		    it->second < 0 ||
		    it->second >= static_cast<int>(boneTransforms.size()) ||
		    it->second >= static_cast<int>(model.bones.size()))
		{
			return glm::mat4(1.0f);
		}

		const glm::mat4 globalBone = boneTransforms[it->second];
		const Bone& globalBoneBind = model.bones[it->second];

		if (model.usesSkinningBindCorrection &&
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
