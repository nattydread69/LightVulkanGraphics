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

#include "RotationGlyph.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace lightGraphics
{
namespace
{

constexpr float kTwoPi = 6.283185307179586f;
constexpr std::uint32_t kMinimumArcSegments = 8;

bool finite(float value)
{
	return std::isfinite(value);
}

bool finite(const glm::vec3& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(const glm::vec4& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) &&
		std::isfinite(value.z) && std::isfinite(value.w);
}

glm::vec3 pointOnPlane(float angle, float radius, float z)
{
	return glm::vec3(radius * std::cos(angle), radius * std::sin(angle), z);
}

std::uint32_t pushVertex(MeshData& mesh, const glm::vec3& position, const glm::vec3& normal,
                          const glm::vec4& color, const glm::vec2& uv)
{
	MeshVertex vertex;
	vertex.position = position;
	vertex.normal = normal;
	vertex.color = color;
	vertex.uv = uv;
	mesh.vertices.push_back(vertex);
	return static_cast<std::uint32_t>(mesh.vertices.size() - 1);
}

// Appends a quad given as p0,p1,p2,p3 in order around its perimeter. The flat
// normal is computed from the actual vertex positions, then the quad is
// re-wound (and the normal flipped) if it doesn't already point toward
// outwardHint. This lets every call site describe a quad geometrically
// without having to hand-derive CCW-from-outside vertex order for each of the
// many differently-oriented surfaces (curved walls, flared shoulder steps,
// end caps) that make up the glyph -- the winding is instead verified once,
// globally, by the closed-volume unit test.
void appendQuad(MeshData& mesh, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
                 const glm::vec3& outwardHint, const glm::vec4& color,
                 const glm::vec3* forcedNormal = nullptr)
{
	glm::vec3 normal = glm::normalize(glm::cross(p1 - p0, p3 - p0));
	if (glm::dot(normal, outwardHint) < 0.0f)
	{
		std::swap(p1, p3);
		normal = -normal;
	}
	if (forcedNormal != nullptr)
	{
		normal = *forcedNormal;
	}

	const std::uint32_t i0 = pushVertex(mesh, p0, normal, color, {0.0f, 0.0f});
	const std::uint32_t i1 = pushVertex(mesh, p1, normal, color, {1.0f, 0.0f});
	const std::uint32_t i2 = pushVertex(mesh, p2, normal, color, {1.0f, 1.0f});
	const std::uint32_t i3 = pushVertex(mesh, p3, normal, color, {0.0f, 1.0f});
	mesh.indices.insert(mesh.indices.end(), {i0, i1, i2, i0, i2, i3});
}

// Same self-correcting winding approach as appendQuad, for the single
// triangle needed where the ribbon's cap fan collapses onto the arrowhead
// tip point.
void appendTriangle(MeshData& mesh, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2,
                     const glm::vec3& outwardHint, const glm::vec4& color,
                     const glm::vec3* forcedNormal = nullptr)
{
	glm::vec3 normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
	if (glm::dot(normal, outwardHint) < 0.0f)
	{
		std::swap(p1, p2);
		normal = -normal;
	}
	if (forcedNormal != nullptr)
	{
		normal = *forcedNormal;
	}

	const std::uint32_t i0 = pushVertex(mesh, p0, normal, color, {0.0f, 0.0f});
	const std::uint32_t i1 = pushVertex(mesh, p1, normal, color, {1.0f, 0.0f});
	const std::uint32_t i2 = pushVertex(mesh, p2, normal, color, {0.5f, 1.0f});
	mesh.indices.insert(mesh.indices.end(), {i0, i1, i2});
}

// One cross-section of the ribbon's sweep path: an angle plus the inner/outer
// radius of the band at that angle (see buildPath).
struct RingPath
{
	std::vector<float> angles;
	std::vector<float> innerRadii;
	std::vector<float> outerRadii;
	float direction = 1.0f; // +1 for CounterClockwise, -1 for Clockwise
};

RingPath buildPath(const RotationRingDescription& description)
{
	const float direction =
		(description.sense == RotationSense::CounterClockwise) ? 1.0f : -1.0f;

	const float innerRadius = description.radius - 0.5f * description.bandWidth;
	const float outerRadius = description.radius + 0.5f * description.bandWidth;
	const float headWidth = description.bandWidth * description.arrowHeadWidthScale;
	const float headInnerRadius = description.radius - 0.5f * headWidth;
	const float headOuterRadius = description.radius + 0.5f * headWidth;

	const float visibleSpan = kTwoPi - description.gapAngleRadians;
	const float bodySpan = visibleSpan - description.arrowHeadAngleRadians;
	const float startAngle =
		description.gapCentreAngleRadians + direction * 0.5f * description.gapAngleRadians;
	const float arrowBaseAngle = startAngle + direction * bodySpan;

	// The arrowhead widens from bandWidth to arrowHeadWidth over a short
	// "flare" ramp at its base, then tapers from arrowHeadWidth down to a
	// point (radius == radius) over the rest of its span. Giving the flare a
	// small but non-zero angular width -- rather than an instantaneous jump
	// at a single angle -- keeps every cap quad non-degenerate (an
	// instantaneous jump would place four cap vertices on the same ray
	// through the origin, i.e. exactly colinear) while still reading as a
	// quick, visually integrated flare rather than a smooth taper.
	constexpr float kFlareFraction = 0.15f;
	const float flareSpan = kFlareFraction * description.arrowHeadAngleRadians;
	const float taperSpan = description.arrowHeadAngleRadians - flareSpan;
	const float flareBaseAngle = arrowBaseAngle + direction * flareSpan;

	// Split arcSegments between the constant-width body, the flare, and the
	// taper in proportion to their angular share of the visible arc, each
	// clamped to at least one segment. The split can shift with
	// gapAngleRadians/arrowHeadAngleRadians, but segmentsBody + segmentsFlare
	// + segmentsTaper is always exactly arcSegments, so overall topology
	// depends only on arcSegments as required.
	const float bodyFraction = bodySpan / visibleSpan;
	std::uint32_t segmentsBody = static_cast<std::uint32_t>(std::lround(
		static_cast<float>(description.arcSegments) * bodyFraction));
	segmentsBody = std::clamp(segmentsBody, 1u, description.arcSegments - 2u);
	const std::uint32_t segmentsHead = description.arcSegments - segmentsBody;
	const std::uint32_t segmentsFlare = std::clamp(
		static_cast<std::uint32_t>(std::lround(static_cast<float>(segmentsHead) * kFlareFraction)),
		1u, segmentsHead - 1u);
	const std::uint32_t segmentsTaper = segmentsHead - segmentsFlare;

	RingPath path;
	path.direction = direction;
	const std::size_t reserveCount =
		static_cast<std::size_t>(segmentsBody) + segmentsFlare + segmentsTaper + 1;
	path.angles.reserve(reserveCount);
	path.innerRadii.reserve(reserveCount);
	path.outerRadii.reserve(reserveCount);

	for (std::uint32_t i = 0; i <= segmentsBody; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(segmentsBody);
		path.angles.push_back(startAngle + direction * bodySpan * t);
		path.innerRadii.push_back(innerRadius);
		path.outerRadii.push_back(outerRadius);
	}

	for (std::uint32_t i = 1; i <= segmentsFlare; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(segmentsFlare);
		path.angles.push_back(arrowBaseAngle + direction * flareSpan * t);
		path.innerRadii.push_back(glm::mix(innerRadius, headInnerRadius, t));
		path.outerRadii.push_back(glm::mix(outerRadius, headOuterRadius, t));
	}

	for (std::uint32_t i = 1; i <= segmentsTaper; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(segmentsTaper);
		path.angles.push_back(flareBaseAngle + direction * taperSpan * t);
		path.innerRadii.push_back(glm::mix(headInnerRadius, description.radius, t));
		path.outerRadii.push_back(glm::mix(headOuterRadius, description.radius, t));
	}

	return path;
}

MeshData buildMeshFromPath(const RingPath& path, float thickness, const glm::vec4& color)
{
	MeshData mesh;
	const std::size_t n = path.angles.size();
	const float halfThickness = 0.5f * thickness;
	const glm::vec3 frontNormal(0.0f, 0.0f, 1.0f);
	const glm::vec3 backNormal(0.0f, 0.0f, -1.0f);

	// Front and back caps: a triangle-strip fan between the inner and outer
	// boundary at each cross-section, including through the flare (every
	// consecutive pair of cross-sections differs in angle, so every quad here
	// has real, non-zero area).
	for (std::size_t i = 0; i + 1 < n; ++i)
	{
		const bool intoTip = (i + 2 == n);

		const glm::vec3 innerAf = pointOnPlane(path.angles[i], path.innerRadii[i], halfThickness);
		const glm::vec3 outerAf = pointOnPlane(path.angles[i], path.outerRadii[i], halfThickness);
		const glm::vec3 innerBf = pointOnPlane(path.angles[i + 1], path.innerRadii[i + 1], halfThickness);
		const glm::vec3 outerBf = pointOnPlane(path.angles[i + 1], path.outerRadii[i + 1], halfThickness);
		if (!intoTip)
		{
			appendQuad(mesh, innerAf, outerAf, outerBf, innerBf, frontNormal, color, &frontNormal);
		}
		else
		{
			// outerBf == innerBf here: both collapse onto the tip point.
			appendTriangle(mesh, innerAf, outerAf, outerBf, frontNormal, color, &frontNormal);
		}

		const glm::vec3 innerAb = pointOnPlane(path.angles[i], path.innerRadii[i], -halfThickness);
		const glm::vec3 outerAb = pointOnPlane(path.angles[i], path.outerRadii[i], -halfThickness);
		const glm::vec3 innerBb = pointOnPlane(path.angles[i + 1], path.innerRadii[i + 1], -halfThickness);
		const glm::vec3 outerBb = pointOnPlane(path.angles[i + 1], path.outerRadii[i + 1], -halfThickness);
		if (!intoTip)
		{
			appendQuad(mesh, innerAb, outerAb, outerBb, innerBb, backNormal, color, &backNormal);
		}
		else
		{
			appendTriangle(mesh, innerAb, outerAb, outerBb, backNormal, color, &backNormal);
		}
	}

	// Outer and inner walls: extruded strips following each boundary curve.
	// Emitted unconditionally (never degenerate: consecutive cross-sections
	// always differ in angle, radius, or both), including through the
	// shoulder flare, which is exactly the step-shaped "shoulder" surface of
	// the arrowhead, and into the tip, where both walls converge onto the
	// same knife edge without leaving a gap.
	for (std::size_t i = 0; i + 1 < n; ++i)
	{
		const float midAngle = 0.5f * (path.angles[i] + path.angles[i + 1]);
		const glm::vec3 outwardRadial(std::cos(midAngle), std::sin(midAngle), 0.0f);

		const glm::vec3 outerAf = pointOnPlane(path.angles[i], path.outerRadii[i], halfThickness);
		const glm::vec3 outerAb = pointOnPlane(path.angles[i], path.outerRadii[i], -halfThickness);
		const glm::vec3 outerBf = pointOnPlane(path.angles[i + 1], path.outerRadii[i + 1], halfThickness);
		const glm::vec3 outerBb = pointOnPlane(path.angles[i + 1], path.outerRadii[i + 1], -halfThickness);
		appendQuad(mesh, outerAf, outerAb, outerBb, outerBf, outwardRadial, color);

		const glm::vec3 innerAf = pointOnPlane(path.angles[i], path.innerRadii[i], halfThickness);
		const glm::vec3 innerAb = pointOnPlane(path.angles[i], path.innerRadii[i], -halfThickness);
		const glm::vec3 innerBf = pointOnPlane(path.angles[i + 1], path.innerRadii[i + 1], halfThickness);
		const glm::vec3 innerBb = pointOnPlane(path.angles[i + 1], path.innerRadii[i + 1], -halfThickness);
		appendQuad(mesh, innerAf, innerAb, innerBb, innerBf, -outwardRadial, color);
	}

	// Start cap: seals the open end at t=0 (the boundary the tip's point
	// never reaches, on the other side of the gap).
	{
		const glm::vec3 innerF = pointOnPlane(path.angles[0], path.innerRadii[0], halfThickness);
		const glm::vec3 innerB = pointOnPlane(path.angles[0], path.innerRadii[0], -halfThickness);
		const glm::vec3 outerF = pointOnPlane(path.angles[0], path.outerRadii[0], halfThickness);
		const glm::vec3 outerB = pointOnPlane(path.angles[0], path.outerRadii[0], -halfThickness);
		const glm::vec3 backwardTangent = -path.direction *
			glm::vec3(-std::sin(path.angles[0]), std::cos(path.angles[0]), 0.0f);
		appendQuad(mesh, innerF, innerB, outerB, outerF, backwardTangent, color);
	}

	return mesh;
}

}

void validateRotationRingDescription(const RotationRingDescription& description)
{
	if (!finite(description.radius) || !finite(description.bandWidth) ||
		!finite(description.thickness) || !finite(description.gapAngleRadians) ||
		!finite(description.gapCentreAngleRadians) || !finite(description.arrowHeadAngleRadians) ||
		!finite(description.arrowHeadWidthScale))
	{
		throw std::invalid_argument(
			"RotationRingDescription contains a non-finite value");
	}
	if (!finite(description.color))
	{
		throw std::invalid_argument("RotationRingDescription color must be finite");
	}
	if (description.radius <= 0.0f)
	{
		throw std::invalid_argument("RotationRingDescription radius must be positive");
	}
	if (description.bandWidth <= 0.0f)
	{
		throw std::invalid_argument("RotationRingDescription bandWidth must be positive");
	}
	if (description.radius - 0.5f * description.bandWidth <= 0.0f)
	{
		throw std::invalid_argument(
			"RotationRingDescription bandWidth is too large for radius: "
			"inner radius would be zero or negative");
	}
	if (description.thickness <= 0.0f)
	{
		throw std::invalid_argument("RotationRingDescription thickness must be positive");
	}
	if (description.arcSegments < kMinimumArcSegments)
	{
		throw std::invalid_argument(
			"RotationRingDescription arcSegments must be at least " +
			std::to_string(kMinimumArcSegments));
	}
	if (description.gapAngleRadians <= 0.0f || description.gapAngleRadians >= kTwoPi)
	{
		throw std::invalid_argument(
			"RotationRingDescription gapAngleRadians must be in (0, 2*pi)");
	}
	if (description.arrowHeadAngleRadians <= 0.0f)
	{
		throw std::invalid_argument(
			"RotationRingDescription arrowHeadAngleRadians must be positive");
	}
	const float visibleSpan = kTwoPi - description.gapAngleRadians;
	if (description.arrowHeadAngleRadians >= visibleSpan)
	{
		throw std::invalid_argument(
			"RotationRingDescription arrowHeadAngleRadians does not fit inside "
			"the visible arc (2*pi - gapAngleRadians)");
	}
	if (description.arrowHeadWidthScale <= 1.0f)
	{
		throw std::invalid_argument(
			"RotationRingDescription arrowHeadWidthScale must be greater than one");
	}
}

MeshData buildRotationRingMesh(const RotationRingDescription& description)
{
	validateRotationRingDescription(description);

	const RingPath path = buildPath(description);
	MeshData mesh = buildMeshFromPath(path, description.thickness, description.color);
	validateMeshData(mesh);
	return mesh;
}

Transform makeRotationRingTransform(
	const glm::vec3& centre,
	const glm::vec3& planeNormal,
	const glm::vec3& inPlaneReference)
{
	if (!finite(planeNormal) || glm::dot(planeNormal, planeNormal) < 1.0e-12f)
	{
		throw std::invalid_argument(
			"makeRotationRingTransform requires a finite, non-zero-length planeNormal");
	}
	if (!finite(centre) || !finite(inPlaneReference))
	{
		throw std::invalid_argument(
			"makeRotationRingTransform requires finite centre/inPlaneReference values");
	}

	const glm::vec3 localZ = glm::normalize(planeNormal);

	glm::vec3 projectedReference =
		inPlaneReference - glm::dot(inPlaneReference, localZ) * localZ;

	if (glm::dot(projectedReference, projectedReference) < 1.0e-10f)
	{
		// Zero, or (near-)parallel to the normal: fall back to whichever
		// world axis is least parallel to localZ, so the choice is
		// deterministic rather than dependent on floating-point noise.
		const glm::vec3 candidates[3] = {
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f)};
		std::size_t bestIndex = 0;
		float bestAlignment = std::numeric_limits<float>::infinity();
		for (std::size_t i = 0; i < 3; ++i)
		{
			const float alignment = std::fabs(glm::dot(candidates[i], localZ));
			if (alignment < bestAlignment)
			{
				bestAlignment = alignment;
				bestIndex = i;
			}
		}
		const glm::vec3& fallback = candidates[bestIndex];
		projectedReference = fallback - glm::dot(fallback, localZ) * localZ;
	}

	const glm::vec3 localX = glm::normalize(projectedReference);
	const glm::vec3 localY = glm::cross(localZ, localX);

	Transform transform;
	transform.position = centre;
	transform.rotation = glm::normalize(glm::quat_cast(glm::mat3(localX, localY, localZ)));
	transform.scale = glm::vec3(1.0f);
	return transform;
}

}
