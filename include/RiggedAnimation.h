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

#include <memory>
#include <vector>

namespace lightGraphics
{
	class RiggedObject;

	// Advances every object's animation by deltaTime -- RiggedObject::updateAnimation() on
	// each -- spread over a small pool of worker threads (created on first use, at most 8,
	// and none on a single-core machine). The result is exactly that of calling
	// updateAnimation() on each in turn: every call writes only its own object and reads
	// its model, which nothing modifies while animating.
	//
	// Worth it for crowds: with 128 Worker.fbx characters, evaluating the animations is
	// the largest per-frame cost left on the CPU. Small sets (fewer than 8 objects) are
	// simply updated in turn on the calling thread.
	//
	// Call it from one thread, and do not touch these objects (or modify a model they
	// share) from elsewhere until it returns. Null entries are skipped. If an update
	// throws, the first exception is rethrown here once every object has been processed.
	void updateAnimations(const std::vector<std::shared_ptr<RiggedObject>>& objects, float deltaTime);
}
