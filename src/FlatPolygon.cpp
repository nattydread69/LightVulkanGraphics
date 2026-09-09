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

#include "FlatPolygon.h"

#include <cmath>
#include <stdexcept>

namespace lightGraphics
{
namespace
{

bool finite(const glm::vec3& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(const glm::vec4& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) &&
		std::isfinite(value.z) && std::isfinite(value.w);
}

// Newell's method: sums the cross products of consecutive edge pairs around
// the polygon, which tolerates the boundary being only approximately planar
// (unlike normalize(cross(p1-p0, p2-p0)), which is sensitive to exactly which
// three vertices happen to get picked). Returns a zero vector, left
// unnormalized, if the polygon is degenerate (all points coincident/collinear)
// -- callers normalize with a fallback.
glm::vec3 newellNormal(const std::vector<glm::vec3>& boundary)
{
	glm::vec3 normal(0.0f);
	for (std::size_t i = 0; i < boundary.size(); ++i)
	{
		const glm::vec3& current = boundary[i];
		const glm::vec3& next = boundary[(i + 1) % boundary.size()];
		normal.x += (current.y - next.y) * (current.z + next.z);
		normal.y += (current.z - next.z) * (current.x + next.x);
		normal.z += (current.x - next.x) * (current.y + next.y);
	}
	return normal;
}

}

MeshData buildFlatPolygonMesh(
	const std::vector<glm::vec3>& boundary,
	const glm::vec4& color)
{
	if (boundary.size() < 3)
	{
		throw std::invalid_argument(
			"buildFlatPolygonMesh requires at least 3 boundary vertices");
	}
	for (const glm::vec3& point : boundary)
	{
		if (!finite(point))
		{
			throw std::invalid_argument("buildFlatPolygonMesh boundary contains a non-finite vertex");
		}
	}
	if (!finite(color))
	{
		throw std::invalid_argument("buildFlatPolygonMesh color is non-finite");
	}

	glm::vec3 normal = newellNormal(boundary);
	float const normalLength = glm::length(normal);
	normal = (normalLength > 1e-8f) ? (normal / normalLength) : glm::vec3(0.0f, 1.0f, 0.0f);

	MeshData mesh;
	mesh.vertices.reserve(boundary.size());
	for (const glm::vec3& point : boundary)
	{
		MeshVertex vertex;
		vertex.position = point;
		vertex.normal = normal;
		vertex.color = color;
		vertex.uv = glm::vec2(0.0f);
		mesh.vertices.push_back(vertex);
	}

	mesh.indices.reserve((boundary.size() - 2) * 3);
	for (std::size_t i = 1; i + 1 < boundary.size(); ++i)
	{
		mesh.indices.push_back(0);
		mesh.indices.push_back(static_cast<std::uint32_t>(i));
		mesh.indices.push_back(static_cast<std::uint32_t>(i + 1));
	}

	return mesh;
}

}
