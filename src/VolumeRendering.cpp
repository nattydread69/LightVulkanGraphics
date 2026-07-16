// SPDX-License-Identifier: LGPL-3.0-or-later

#include "VolumeRendering.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lightGraphics
{
namespace
{

bool finite(const glm::vec3& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) &&
		std::isfinite(value.z);
}

bool finite(const glm::vec4& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) &&
		std::isfinite(value.z) && std::isfinite(value.w);
}

std::uint8_t channel(float value)
{
	return static_cast<std::uint8_t>(
		std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

std::string normalizedName(const std::string& value)
{
	std::string result;
	result.reserve(value.size());
	for (const unsigned char character : value)
	{
		if (std::isalnum(character) != 0)
		{
			result.push_back(static_cast<char>(std::tolower(character)));
		}
	}
	return result;
}

}

void validateMeshData(const MeshData& meshData)
{
	if (meshData.vertices.empty())
	{
		throw std::invalid_argument("MeshData requires at least one vertex");
	}
	if (meshData.indices.empty() || meshData.indices.size() % 3 != 0)
	{
		throw std::invalid_argument(
			"MeshData triangle-list indices must be non-empty and divisible by three");
	}
	for (std::uint32_t index : meshData.indices)
	{
		if (index >= meshData.vertices.size())
		{
			throw std::out_of_range("MeshData index references a missing vertex");
		}
	}
	for (const MeshVertex& vertex : meshData.vertices)
	{
		if (!finite(vertex.position) || !finite(vertex.normal) ||
			!finite(vertex.color) || !std::isfinite(vertex.uv.x) ||
			!std::isfinite(vertex.uv.y))
		{
			throw std::invalid_argument("MeshData contains a non-finite vertex attribute");
		}
	}
}

void validateDynamicMeshCapacity(
	std::size_t maximumVertexCount,
	std::size_t maximumIndexCount)
{
	if (maximumVertexCount == 0 || maximumIndexCount == 0)
	{
		throw std::invalid_argument("Dynamic mesh capacities must be non-zero");
	}
	if (maximumIndexCount % 3 != 0)
	{
		throw std::invalid_argument(
			"Dynamic mesh index capacity must hold complete triangles");
	}
}

void validateDynamicMeshUpdate(
	std::size_t maximumVertexCount,
	std::size_t maximumIndexCount,
	const MeshData& meshData)
{
	validateDynamicMeshCapacity(maximumVertexCount, maximumIndexCount);
	validateMeshData(meshData);
	if (meshData.vertices.size() > maximumVertexCount ||
		meshData.indices.size() > maximumIndexCount)
	{
		throw std::length_error("Dynamic mesh update exceeds its declared capacity");
	}
}

void validateMaterialDescription(const MaterialDescription& description)
{
	if (description.shaderProgram.selection == ShaderSelection::Custom &&
		(description.shaderProgram.vertexShader.empty() ||
		 description.shaderProgram.fragmentShader.empty()))
	{
		throw std::invalid_argument(
			"Custom material requires both vertex and fragment shader modules");
	}
	if (description.polygonMode == PolygonMode::Line &&
		description.alphaBlendingEnabled)
	{
		// Supported, but intentionally accepted here; device feature validation
		// happens when the Vulkan pipeline is created.
	}
}

std::size_t textureFormatByteSize(TextureFormat format)
{
	switch (format)
	{
	case TextureFormat::R8_UNORM:
		return 1;
	case TextureFormat::R16_UNORM:
	case TextureFormat::R16_SFLOAT:
		return 2;
	case TextureFormat::R32_SFLOAT:
	case TextureFormat::RGBA8_UNORM:
		return 4;
	}
	throw std::invalid_argument("Unknown TextureFormat");
}

std::size_t texture3DByteSize(const Texture3DDescription& description)
{
	validateTexture3DDescription(description);
	const std::size_t texels = static_cast<std::size_t>(description.width) *
		static_cast<std::size_t>(description.height) *
		static_cast<std::size_t>(description.depth);
	if (texels > std::numeric_limits<std::size_t>::max() /
		textureFormatByteSize(description.format))
	{
		throw std::overflow_error("Texture3D byte size overflows size_t");
	}
	return texels * textureFormatByteSize(description.format);
}

void validateTexture3DDescription(const Texture3DDescription& description)
{
	if (description.width == 0 || description.height == 0 ||
		description.depth == 0)
	{
		throw std::invalid_argument("Texture3D dimensions must be non-zero");
	}
	if (description.generateMipmaps)
	{
		throw std::invalid_argument(
			"Texture3D mipmap generation is not implemented in LVG-VOL-1");
	}
	(void)textureFormatByteSize(description.format);
}

std::vector<TransferFunctionPoint> normalizeTransferFunctionPoints(
	const std::vector<TransferFunctionPoint>& points)
{
	if (points.size() < 2)
	{
		throw std::invalid_argument("Transfer function requires at least two points");
	}
	std::vector<TransferFunctionPoint> result = points;
	for (const TransferFunctionPoint& point : result)
	{
		if (!std::isfinite(point.scalar) || !finite(point.color) ||
			point.scalar < 0.0f || point.scalar > 1.0f)
		{
			throw std::invalid_argument(
				"Transfer function values must be finite and normalized to [0,1]");
		}
	}
	std::stable_sort(
		result.begin(),
		result.end(),
		[](const TransferFunctionPoint& left, const TransferFunctionPoint& right)
		{
			return left.scalar < right.scalar;
		});
	for (std::size_t index = 1; index < result.size(); ++index)
	{
		if (result[index].scalar <= result[index - 1].scalar)
		{
			throw std::invalid_argument(
				"Transfer function scalar positions must be unique");
		}
	}
	return result;
}

std::vector<std::uint8_t> sampleTransferFunctionRgba8(
	const std::vector<TransferFunctionPoint>& points,
	std::size_t sampleCount)
{
	if (sampleCount < 2)
	{
		throw std::invalid_argument("Transfer function lookup requires at least two samples");
	}
	const std::vector<TransferFunctionPoint> sorted =
		normalizeTransferFunctionPoints(points);
	std::vector<std::uint8_t> result(sampleCount * 4);
	std::size_t right = 1;
	for (std::size_t sample = 0; sample < sampleCount; ++sample)
	{
		const float scalar = static_cast<float>(sample) /
			static_cast<float>(sampleCount - 1);
		while (right + 1 < sorted.size() && scalar > sorted[right].scalar)
		{
			++right;
		}
		glm::vec4 colorValue;
		if (scalar <= sorted.front().scalar)
		{
			colorValue = sorted.front().color;
		}
		else if (scalar >= sorted.back().scalar)
		{
			colorValue = sorted.back().color;
		}
		else
		{
			const TransferFunctionPoint& leftPoint = sorted[right - 1];
			const TransferFunctionPoint& rightPoint = sorted[right];
			const float t = (scalar - leftPoint.scalar) /
				(rightPoint.scalar - leftPoint.scalar);
			colorValue = glm::mix(leftPoint.color, rightPoint.color, t);
		}
		result[sample * 4] = channel(colorValue.r);
		result[sample * 4 + 1] = channel(colorValue.g);
		result[sample * 4 + 2] = channel(colorValue.b);
		result[sample * 4 + 3] = channel(colorValue.a);
	}
	return result;
}

std::vector<TransferFunctionPoint> transferFunctionPreset(
	TransferFunctionPreset preset)
{
	switch (preset)
	{
	case TransferFunctionPreset::DensityDeficitCyan:
		return {{0.0f, glm::vec4(0.0f)}, {0.08f, {0.04f, 0.45f, 0.75f, 0.0f}},
			{0.55f, {0.05f, 0.78f, 1.0f, 0.22f}}, {1.0f, {0.25f, 0.95f, 1.0f, 0.72f}}};
	case TransferFunctionPreset::CrossOverlapOrange:
		return {{0.0f, glm::vec4(0.0f)}, {0.08f, {0.5f, 0.08f, 0.0f, 0.0f}},
			{0.55f, {1.0f, 0.34f, 0.01f, 0.30f}}, {1.0f, {1.0f, 0.92f, 0.12f, 0.78f}}};
	case TransferFunctionPreset::InvalidEndpointRed:
		return {{0.0f, glm::vec4(0.0f)}, {0.05f, {0.45f, 0.0f, 0.0f, 0.0f}},
			{0.5f, {1.0f, 0.02f, 0.01f, 0.35f}}, {1.0f, {1.0f, 0.25f, 0.08f, 0.82f}}};
	case TransferFunctionPreset::SoftNeutralFog:
		return {{0.0f, glm::vec4(0.0f)}, {0.1f, {0.4f, 0.48f, 0.55f, 0.0f}},
			{1.0f, {0.75f, 0.82f, 0.9f, 0.22f}}};
	}
	throw std::invalid_argument("Unknown TransferFunctionPreset");
}

void validateClippingDescription(const ClippingDescription& clipping)
{
	if (clipping.clipPlaneEnabled)
	{
		if (!finite(clipping.clipPlaneNormal) ||
			glm::dot(clipping.clipPlaneNormal, clipping.clipPlaneNormal) <= 1.0e-12f ||
			!std::isfinite(clipping.clipPlaneOffset))
		{
			throw std::invalid_argument("Enabled clip plane requires a finite non-zero normal");
		}
	}
	if (clipping.clipBoxEnabled)
	{
		if (!finite(clipping.clipBoxMinimum) || !finite(clipping.clipBoxMaximum) ||
			glm::any(glm::greaterThanEqual(
				clipping.clipBoxMinimum,
				clipping.clipBoxMaximum)))
		{
			throw std::invalid_argument("Enabled clip box requires finite ordered bounds");
		}
	}
}

void validateVolumeRenderDescription(const VolumeRenderDescription& description)
{
	if (!description.volumeTexture.isValid() ||
		!description.transferFunction.isValid())
	{
		throw std::invalid_argument("Volume description requires valid texture handles");
	}
	if (!finite(description.volumeMin) || !finite(description.volumeMax) ||
		glm::any(glm::greaterThanEqual(description.volumeMin, description.volumeMax)))
	{
		throw std::invalid_argument("Volume bounds must be finite and ordered");
	}
	if (!std::isfinite(description.densityScale) || description.densityScale < 0.0f ||
		!std::isfinite(description.opacityScale) || description.opacityScale < 0.0f)
	{
		throw std::invalid_argument("Volume density and opacity scales must be finite and non-negative");
	}
	if (description.raymarchSteps < 2 || description.raymarchSteps > 2048)
	{
		throw std::invalid_argument("Volume raymarchSteps must be in [2,2048]");
	}
	if (!std::isfinite(description.referenceStepLength) ||
		description.referenceStepLength <= 0.0f)
	{
		throw std::invalid_argument(
			"Volume referenceStepLength must be finite and positive");
	}
	(void)volumeOpacityModelName(description.opacityModel);
	validateClippingDescription(description.clipping);
}

const char* volumeOpacityModelName(VolumeOpacityModel model)
{
	switch (model)
	{
	case VolumeOpacityModel::LinearAlpha:
		return "linearAlpha";
	case VolumeOpacityModel::ExponentialExtinction:
		return "exponentialExtinction";
	}
	throw std::invalid_argument("Unknown VolumeOpacityModel");
}

VolumeOpacityModel parseVolumeOpacityModel(const std::string& name)
{
	const std::string normalized = normalizedName(name);
	if (normalized == "linear" || normalized == "linearalpha")
	{
		return VolumeOpacityModel::LinearAlpha;
	}
	if (normalized == "exponential" || normalized == "exponentialextinction")
	{
		return VolumeOpacityModel::ExponentialExtinction;
	}
	throw std::invalid_argument("Unknown volume opacity model: " + name);
}

const char* renderLayerName(RenderLayer layer)
{
	switch (layer)
	{
	case RenderLayer::Opaque: return "opaque";
	case RenderLayer::Volume: return "volume";
	case RenderLayer::Transparent: return "transparent";
	case RenderLayer::Overlay: return "overlay";
	case RenderLayer::User0: return "user0";
	case RenderLayer::User1: return "user1";
	}
	throw std::invalid_argument("Unknown RenderLayer");
}

RenderLayer parseRenderLayer(const std::string& name)
{
	const std::string normalized = normalizedName(name);
	for (const RenderLayer layer : {RenderLayer::Opaque, RenderLayer::Volume,
		RenderLayer::Transparent, RenderLayer::Overlay, RenderLayer::User0,
		RenderLayer::User1})
	{
		if (normalized == renderLayerName(layer))
		{
			return layer;
		}
	}
	throw std::invalid_argument("Unknown render layer: " + name);
}

std::uint32_t renderLayerOrdinal(RenderLayer layer)
{
	(void)renderLayerName(layer);
	return static_cast<std::uint32_t>(layer);
}

void validateDrawOptions(const DrawOptions& options)
{
	(void)renderLayerOrdinal(options.layer);
	if (!std::isfinite(options.sortKey))
	{
		throw std::invalid_argument("Draw sortKey must be finite");
	}
}

std::vector<DrawOrderEntry> sortDrawOrder(std::vector<DrawOrderEntry> entries)
{
	for (const DrawOrderEntry& entry : entries)
	{
		validateDrawOptions({entry.layer, entry.sortKey});
	}
	std::stable_sort(entries.begin(), entries.end(),
		[](const DrawOrderEntry& left, const DrawOrderEntry& right)
		{
			if (left.layer != right.layer)
			{
				return renderLayerOrdinal(left.layer) < renderLayerOrdinal(right.layer);
			}
			if (left.sortKey != right.sortKey)
			{
				return left.sortKey < right.sortKey;
			}
			return left.submissionIndex < right.submissionIndex;
		});
	return entries;
}

}
