// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "FBXLoader.h"

#include <glm/glm.hpp>

#include <vector>

namespace lightGraphics::detail
{
	inline glm::mat4 buildRiggedFinalBoneMatrix(const RiggedModel& model,
	                                            const RiggedMesh& mesh,
	                                            const Bone& meshBone,
	                                            const std::vector<glm::mat4>& boneTransforms)
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
			// Convert baked mesh vertices back through the mesh node before
			// applying the FBX bone offset, then return to model space.
			return model.globalInverseTransform *
			       globalBone *
			       meshBone.offsetMatrix *
			       mesh.globalBindTransform;
		}

		return model.globalInverseTransform *
		       globalBone *
		       glm::inverse(globalBoneBind.globalBindTransform) *
		       mesh.globalBindTransform;
	}
} // namespace lightGraphics::detail
