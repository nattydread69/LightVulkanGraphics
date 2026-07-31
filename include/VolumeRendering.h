// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "SceneGraph.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lightGraphics
{

template <typename Tag>
struct ResourceHandle
{
	std::uint32_t index = UINT32_MAX;
	std::uint32_t generation = 0;

	[[nodiscard]] bool isValid() const noexcept
	{
		return index != UINT32_MAX && generation != 0;
	}
};

struct MeshTag;
struct MaterialTag;
struct ScreenTextTag;
struct Texture3DTag;
struct TransferFunctionTag;
struct VolumeTag;

using MeshHandle = ResourceHandle<MeshTag>;
using MaterialHandle = ResourceHandle<MaterialTag>;
using ScreenTextHandle = ResourceHandle<ScreenTextTag>;
using Texture3DHandle = ResourceHandle<Texture3DTag>;
using TransferFunctionHandle = ResourceHandle<TransferFunctionTag>;
using VolumeHandle = ResourceHandle<VolumeTag>;

struct MeshVertex
{
	glm::vec3 position{0.0f};
	glm::vec3 normal{0.0f, 1.0f, 0.0f};
	glm::vec4 color{1.0f};
	glm::vec2 uv{0.0f};
};

struct MeshData
{
	std::vector<MeshVertex> vertices;
	std::vector<std::uint32_t> indices;
};

struct ScreenTextDescription
{
	std::string text;
	glm::vec2 positionPixels{16.0f, 16.0f};
	float scale = 1.0f;
	glm::vec4 color{1.0f};
	std::size_t maximumCharacters = 256;
	bool visible = true;
	float sortKey = 1000.0f;
};

enum class CullMode
{
	None,
	Front,
	Back
};

enum class PolygonMode
{
	Fill,
	Line
};

enum class ShaderSelection
{
	VertexColorLit,
	VertexColorUnlit,
	VolumeRaymarch,
	Custom
};

struct ShaderProgramDescription
{
	ShaderSelection selection = ShaderSelection::VertexColorLit;
	std::filesystem::path vertexShader;
	std::filesystem::path fragmentShader;
};

struct MaterialDescription
{
	bool alphaBlendingEnabled = false;
	bool depthTestEnabled = true;
	bool depthWriteEnabled = true;
	CullMode cullMode = CullMode::Back;
	PolygonMode polygonMode = PolygonMode::Fill;
	ShaderProgramDescription shaderProgram;
};

enum class TextureFormat
{
	R8_UNORM,
	R16_UNORM,
	R16_SFLOAT,
	R32_SFLOAT,
	RGBA8_UNORM
};

struct Texture3DDescription
{
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::uint32_t depth = 0;
	TextureFormat format = TextureFormat::R32_SFLOAT;
	bool generateMipmaps = false;
};

struct TransferFunctionPoint
{
	float scalar = 0.0f;
	glm::vec4 color{0.0f};
};

enum class TransferFunctionPreset
{
	DensityDeficitCyan,
	CrossOverlapOrange,
	InvalidEndpointRed,
	SoftNeutralFog
};

struct ClippingDescription
{
	bool clipPlaneEnabled = false;
	glm::vec3 clipPlaneNormal{0.0f, 0.0f, 1.0f};
	float clipPlaneOffset = 0.0f;
	bool clipBoxEnabled = false;
	glm::vec3 clipBoxMinimum{0.0f};
	glm::vec3 clipBoxMaximum{1.0f};
};

enum class VolumeOpacityModel
{
	LinearAlpha,
	ExponentialExtinction
};

enum class RenderLayer
{
	Opaque,
	Volume,
	Transparent,
	Overlay,
	User0,
	User1
};

struct DrawOptions
{
	RenderLayer layer = RenderLayer::Opaque;
	float sortKey = 0.0f;
};

struct DrawOrderEntry
{
	RenderLayer layer = RenderLayer::Opaque;
	float sortKey = 0.0f;
	std::uint64_t submissionIndex = 0;
};

struct VolumeRenderDescription
{
	Texture3DHandle volumeTexture;
	TransferFunctionHandle transferFunction;
	glm::vec3 volumeMin{-1.0f};
	glm::vec3 volumeMax{1.0f};
	float densityScale = 1.0f;
	float opacityScale = 1.0f;
	std::uint32_t raymarchSteps = 128;
	bool enableJitter = false;
	bool enableEarlyTermination = true;
	ClippingDescription clipping;
	VolumeOpacityModel opacityModel = VolumeOpacityModel::ExponentialExtinction;
	float referenceStepLength = 1.0f;
	bool normalizeOpacityByStepLength = true;
};

void validateMeshData(const MeshData& meshData);
void validateScreenTextDescription(
	const ScreenTextDescription& description);
[[nodiscard]] MeshData buildScreenTextMesh(
	const ScreenTextDescription& description,
	std::uint32_t framebufferWidth,
	std::uint32_t framebufferHeight);
void validateDynamicMeshCapacity(
	std::size_t maximumVertexCount,
	std::size_t maximumIndexCount);
void validateDynamicMeshUpdate(
	std::size_t maximumVertexCount,
	std::size_t maximumIndexCount,
	const MeshData& meshData);
void validateMaterialDescription(const MaterialDescription& description);
[[nodiscard]] std::size_t textureFormatByteSize(TextureFormat format);
[[nodiscard]] std::size_t texture3DByteSize(
	const Texture3DDescription& description);
void validateTexture3DDescription(const Texture3DDescription& description);
[[nodiscard]] std::vector<TransferFunctionPoint> normalizeTransferFunctionPoints(
	const std::vector<TransferFunctionPoint>& points);
[[nodiscard]] std::vector<std::uint8_t> sampleTransferFunctionRgba8(
	const std::vector<TransferFunctionPoint>& points,
	std::size_t sampleCount = 256);
[[nodiscard]] std::vector<TransferFunctionPoint> transferFunctionPreset(
	TransferFunctionPreset preset);
void validateClippingDescription(const ClippingDescription& clipping);
void validateVolumeRenderDescription(const VolumeRenderDescription& description);
[[nodiscard]] const char* volumeOpacityModelName(VolumeOpacityModel model);
[[nodiscard]] VolumeOpacityModel parseVolumeOpacityModel(const std::string& name);
[[nodiscard]] const char* renderLayerName(RenderLayer layer);
[[nodiscard]] RenderLayer parseRenderLayer(const std::string& name);
[[nodiscard]] std::uint32_t renderLayerOrdinal(RenderLayer layer);
void validateDrawOptions(const DrawOptions& options);
[[nodiscard]] std::vector<DrawOrderEntry> sortDrawOrder(
	std::vector<DrawOrderEntry> entries);

}
