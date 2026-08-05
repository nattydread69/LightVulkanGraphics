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

#include "SceneGraph.h"
#include "VolumeRendering.h"

#include <cstdint>

#include <glm/glm.hpp>

namespace lightGraphics
{
	// A rotation-ring glyph visualises an angular quantity (angular velocity,
	// angular momentum, torque, ...) as an oriented annular arrow lying in the
	// actual plane of rotation, rather than as a conventional axial/pseudovector
	// arrow perpendicular to that plane. See docs/rotation_glyphs.md for the
	// full rationale. This module only builds CPU-side geometry and an
	// orientation transform -- it owns no Vulkan resources and never touches
	// the physical definitions of the quantities it visualises.

	// Rotational sense as seen from the positive side of the glyph's plane
	// (the side local +Z points toward). See buildRotationRingMesh's local
	// construction comment for the exact angle convention.
	enum class RotationSense
	{
		CounterClockwise,
		Clockwise
	};

	struct RotationRingDescription
	{
		float radius = 1.0f;
		float bandWidth = 0.16f;
		float thickness = 0.04f;

		// The ribbon covers the full circle minus a gap of this angular width,
		// centred on gapCentreAngleRadians. The gap is what makes the shape
		// read as an open, oriented arc rather than a closed torus.
		float gapAngleRadians = glm::radians(40.0f);
		float gapCentreAngleRadians = 0.0f;

		// The ribbon's trailing arcLength (in the direction of travel implied
		// by `sense`) widens into an arrowhead over this many radians before
		// terminating in a point.
		float arrowHeadAngleRadians = glm::radians(28.0f);
		// Radial width of the arrowhead's widest point, as a multiple of
		// bandWidth. Must be > 1 so the head is visibly wider than the body.
		float arrowHeadWidthScale = 2.0f;

		// Tessellation of the curved body/arrowhead. Vertex and index counts
		// depend only on this (and the angle fields above), never on radius,
		// bandWidth, color, or arrowHeadWidthScale -- so a glyph's topology is
		// stable while an application animates its magnitude (bandWidth) into
		// an existing dynamic mesh allocation. See docs/rotation_glyphs.md.
		std::uint32_t arcSegments = 96;

		RotationSense sense = RotationSense::CounterClockwise;
		glm::vec4 color{0.10f, 0.65f, 1.0f, 1.0f};
	};

	// Rejects non-finite or geometrically invalid descriptions. Throws
	// std::invalid_argument with a diagnostic beginning with
	// "RotationRingDescription" (see RotationGlyph.cpp for the exact checks).
	void validateRotationRingDescription(
		const RotationRingDescription& description);

	// Builds an indexed triangle-list mesh of the ring glyph, authored in the
	// local XY plane around the origin, with its front face along local +Z.
	// The result satisfies validateMeshData() and is suitable for
	// VkApp::createStaticMesh or VkApp::createDynamicMesh /
	// VkApp::updateDynamicMesh. Throws whatever
	// validateRotationRingDescription() throws for an invalid description.
	[[nodiscard]] MeshData buildRotationRingMesh(
		const RotationRingDescription& description);

	// Builds a world-space Transform placing a ring glyph's centre at `centre`
	// with local +Z aligned to `planeNormal` (normalized internally) and local
	// +X aligned to `inPlaneReference` projected into the plane. The in-plane
	// reference controls where local angle zero -- and therefore the default
	// gap -- appears in world space. If `inPlaneReference` is zero or
	// (near-)parallel to `planeNormal`, a deterministic fallback axis (the
	// world axis least parallel to `planeNormal`) is used instead. Throws
	// std::invalid_argument if `planeNormal` is zero-length or non-finite.
	[[nodiscard]] Transform makeRotationRingTransform(
		const glm::vec3& centre,
		const glm::vec3& planeNormal,
		const glm::vec3& inPlaneReference = glm::vec3(0.0f));
}
