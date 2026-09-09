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

#include "VolumeRendering.h"

#include <vector>

#include <glm/glm.hpp>

namespace lightGraphics
{
	// A flat polygon is a graphics-only module, in the same spirit as
	// RotationGlyph.h: it only builds a CPU-side MeshData for use with the
	// existing custom-mesh API (VkApp::createStaticMesh / createDynamicMesh /
	// updateDynamicMesh, docs/custom_mesh_api.md) -- it owns no Vulkan resources
	// and adds no new pipeline.
	//
	// Unlike RotationGlyph (authored in a local plane, placed by a separate
	// Transform helper), buildFlatPolygonMesh authors vertices directly in
	// whatever space `boundary` is given in -- callers that already have real
	// corner positions (e.g. world space) can draw the result with an identity
	// Transform, with no separate orientation step needed.

	// Builds an indexed triangle-fan mesh for a flat, CONVEX polygon from its
	// boundary vertices, given in order (winding either way) around the
	// polygon. Fanning from vertex 0 only produces a correct (non-self-
	// intersecting) triangulation for a convex polygon -- every diagonal from
	// any one vertex of a convex polygon stays inside it, which does not hold
	// in general for a non-convex polygon. The boundary is assumed
	// (approximately) coplanar; the shared face normal is computed via
	// Newell's method, which tolerates small out-of-plane noise better than
	// picking an arbitrary vertex triple would.
	//
	// `boundary.size()` must be at least 3. Throws std::invalid_argument
	// otherwise, matching RotationGlyph's validation style. To hide a
	// previously-drawn polygon (e.g. a dynamic mesh whose draw registration
	// should render nothing this frame), call this with 3 copies of the same
	// point -- validateMeshData() only rejects empty/malformed data and
	// non-finite attributes, not degenerate (zero-area) triangles, so a
	// collapsed polygon is valid input and renders nothing.
	[[nodiscard]] MeshData buildFlatPolygonMesh(
		const std::vector<glm::vec3>& boundary,
		const glm::vec4& color);
}
