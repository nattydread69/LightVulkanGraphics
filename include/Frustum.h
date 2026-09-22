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

#ifndef LIGHT_VULKAN_GRAPHICS_FRUSTUM_H
#define LIGHT_VULKAN_GRAPHICS_FRUSTUM_H

#include <glm/glm.hpp>
#include <array>

namespace lightGraphics
{
	// A camera's view frustum as six half-space planes, each stored as (a, b, c, d) with
	// unit-length (a, b, c) such that a point P is on the INSIDE of that plane iff
	// dot((a,b,c), P) + d >= 0. All six planes point inward, so a point/sphere inside the
	// frustum satisfies that for every plane simultaneously.
	//
	// Built with the standard Gribb/Hartmann method (extracting planes directly from the
	// combined view-projection matrix's rows) -- see fromViewProj()'s comment for why this
	// project's specific clip-space convention (OpenGL-style z in [-1,1], despite the
	// Vulkan Y-flip alongside it) matters for the near/far planes specifically, and how
	// that was verified.
	class ViewFrustum
	{
	public:
		// `viewProj` must be projection * view (the same combined matrix used to place
		// vertices in clip space for rendering) -- planes are extracted in WORLD space, so
		// this takes no separate per-object transform.
		static ViewFrustum fromViewProj(const glm::mat4& viewProj)
		{
			// GLM stores matrices column-major and applies them as clip = M * point, so
			// "row i" of the matrix as the textbook derivation means (M[0][i], M[1][i],
			// M[2][i], M[3][i]) here, not glm's own row_type.
			const glm::vec4 row0(viewProj[0][0], viewProj[1][0], viewProj[2][0], viewProj[3][0]);
			const glm::vec4 row1(viewProj[0][1], viewProj[1][1], viewProj[2][1], viewProj[3][1]);
			const glm::vec4 row2(viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]);
			const glm::vec4 row3(viewProj[0][3], viewProj[1][3], viewProj[2][3], viewProj[3][3]);

			ViewFrustum f;
			// Left/right/bottom/top: the classic Gribb/Hartmann combinations. These four
			// depend only on the X/Y/W rows, so they are correct regardless of whether the
			// projection targets a [-1,1] (OpenGL) or [0,1] (Vulkan) clip-space Z range --
			// only near/far (below) depend on that.
			f.m_planes[0] = normalizePlane(row3 + row0); // left
			f.m_planes[1] = normalizePlane(row3 - row0); // right
			f.m_planes[2] = normalizePlane(row3 + row1); // bottom (or top -- see below)
			f.m_planes[3] = normalizePlane(row3 - row1); // top (or bottom)
			// Near/far: this project's Camera::proj() (Camera.h) is glm::perspective's
			// default OpenGL-style clip-space Z range ([-1,1], via P[2][2]/P[3][2]),
			// despite the Vulkan Y-flip alongside it -- GLM_FORCE_DEPTH_ZERO_TO_ONE is not
			// defined anywhere in this project. Verified directly against the actual
			// matrix Camera::proj() produces: dot(row3+row2, p) is exactly zero at
			// p = -zNear (the near plane), not dot(row2, p) alone (the Vulkan [0,1]
			// formula, which is zero at a different, zNear-and-zFar-dependent point and
			// would silently cull the wrong things). See tests/unit/test_core_types.cpp,
			// testViewFrustumCulling(), for the pinned-down near/far boundary cases this
			// depends on.
			f.m_planes[4] = normalizePlane(row3 + row2); // near
			f.m_planes[5] = normalizePlane(row3 - row2); // far
			return f;
		}

		// True if the sphere is at least partially inside the frustum (i.e. NOT provably
		// fully outside it) -- the standard conservative test: reject only when the
		// sphere's center is further than `radius` on the outside of some plane. A sphere
		// straddling a plane, or one that is a loose bound around a rotated/off-center
		// shape, is correctly kept rather than culled -- false positives (drawing
		// something invisible) cost a little GPU time; false negatives (culling something
		// visible) would be a visible bug, so this errs toward keeping.
		bool sphereIntersects(const glm::vec3& center, float radius) const
		{
			for (const glm::vec4& plane : m_planes)
			{
				const float distance = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;
				if (distance < -radius)
				{
					return false;
				}
			}
			return true;
		}

		const std::array<glm::vec4, 6>& planes() const { return m_planes; }

	private:
		static glm::vec4 normalizePlane(const glm::vec4& plane)
		{
			const float length = glm::length(glm::vec3(plane));
			return length > 1.0e-8f ? plane / length : plane;
		}

		std::array<glm::vec4, 6> m_planes{};
	};

} // namespace lightGraphics

#endif // LIGHT_VULKAN_GRAPHICS_FRUSTUM_H
