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

// updateAnimations() (worker threads) must give bit-identical poses to calling
// RiggedObject::updateAnimation() on each object in turn.

#include "RiggedAnimation.h"
#include "RiggedObject.h"

#include <glm/glm.hpp>

#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

int main()
{
#ifdef LVG_SOURCE_DIR
	const std::filesystem::path modelPath = std::filesystem::path(LVG_SOURCE_DIR) / "assets" / "Worker.fbx";
#else
	const std::filesystem::path modelPath = std::filesystem::path("assets") / "Worker.fbx";
#endif
	auto first = std::make_shared<lightGraphics::RiggedObject>(
	    glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), "w", 1.0f, modelPath.string());
	auto model = first->getModel();
	if (!model || first->getAnimationCount() == 0)
	{
		std::cerr << "rigged_animation_parallel: could not load " << modelPath << std::endl;
		return 1;
	}

	// Two identical crowds: same clip (several, to vary channel layouts), staggered times.
	constexpr int kCount = 64;
	std::vector<std::shared_ptr<lightGraphics::RiggedObject>> serial;
	std::vector<std::shared_ptr<lightGraphics::RiggedObject>> parallel;
	for (int i = 0; i < kCount; ++i)
	{
		const int clip = i % first->getAnimationCount();
		for (auto* crowd : {&serial, &parallel})
		{
			auto object = std::make_shared<lightGraphics::RiggedObject>(
			    glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), "w", 1.0f, model);
			object->playAnimation(clip, true);
			object->setAnimationTime(0.13f * static_cast<float>(i));
			crowd->push_back(object);
		}
	}

	// Uneven steps, as real frame times are.
	for (int step = 0; step < 30; ++step)
	{
		const float dt = 0.004f + 0.003f * static_cast<float>(step % 7);
		for (const auto& object : serial)
		{
			object->updateAnimation(dt);
		}
		lightGraphics::updateAnimations(parallel, dt);

		for (int i = 0; i < kCount; ++i)
		{
			const auto& a = serial[i]->getBoneTransforms();
			const auto& b = parallel[i]->getBoneTransforms();
			if (a.size() != b.size() ||
			    (!a.empty() && std::memcmp(a.data(), b.data(), a.size() * sizeof(glm::mat4)) != 0))
			{
				std::cerr << "rigged_animation_parallel: object " << i << " differs at step " << step << std::endl;
				return 1;
			}
		}
	}

	std::cout << "rigged_animation_parallel: " << kCount << " objects, 30 steps, poses identical" << std::endl;
	return 0;
}
