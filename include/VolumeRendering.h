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

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <filesystem>
#include <string>
#include <vector>

// Defined from 3.3: bricked (sparse) volumes, BrickPool, updateTexture3DRegions.
#define LVG_HAS_BRICKED_VOLUMES 1

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
struct BrickedTextureTag;

using MeshHandle = ResourceHandle<MeshTag>;
using MaterialHandle = ResourceHandle<MaterialTag>;
using ScreenTextHandle = ResourceHandle<ScreenTextTag>;
using Texture3DHandle = ResourceHandle<Texture3DTag>;
using TransferFunctionHandle = ResourceHandle<TransferFunctionTag>;
using VolumeHandle = ResourceHandle<VolumeTag>;
using BrickedTextureHandle = ResourceHandle<BrickedTextureTag>;

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
	// A dark copy of the glyphs is drawn offset behind the real text so it
	// stays legible over bright/busy backgrounds, without needing a
	// texture-atlas font. Capacity is always reserved for this second pass
	// (see EASY_FONT_TEXT_PASSES in ScreenText.cpp) so toggling it on later
	// never requires recreating the mesh.
	bool shadowEnabled = true;
	glm::vec4 shadowColor{0.0f, 0.0f, 0.0f, 0.75f};
	glm::vec2 shadowOffsetPixels{1.5f, 1.5f};
	// An optional solid rectangle drawn behind the glyphs (e.g. so a status
	// panel stays readable over busy 3D content). Sized to the text's own
	// pixel bounding box plus backgroundPaddingPixels on every side.
	// Capacity for it is always reserved (see ScreenText.cpp) so toggling it
	// on later never requires recreating the mesh.
	bool backgroundEnabled = false;
	glm::vec4 backgroundColor{0.0f, 0.0f, 0.0f, 0.6f};
	float backgroundPaddingPixels = 6.0f;

	// ---- font-atlas rendering ----
	// When the GUI is available (built with LVG_WITH_UI and initialised) the text is drawn
	// from the GUI's real TrueType atlas -- anti-aliased, kerned, full Latin/Greek/punctuation
	// and anything the GUI's fallback faces cover -- instead of the blocky stb_easy_font
	// strokes. It is drawn by the UI renderer in the same pass, just under the GUI panels,
	// so `sortKey` orders it against other screen texts but NOT against scene overlay
	// meshes. Read at creation: changing it in updateScreenText() has no effect. Set false
	// to force the legacy stroke font (ASCII only; also the automatic fallback when no GUI
	// exists).
	bool useFontAtlas = true;
	// Atlas rendering only: use the GUI's monospace / bold face (when it has one).
	bool monospace = false;
	bool bold = false;
	// Atlas rendering only: glyph height in framebuffer pixels before `scale`. 0 = the GUI
	// font size scaled by the display content scale, i.e. exactly the size the atlas was
	// baked at (crispest).
	float fontPixelSize = 0.0f;
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

// Where a volume's colour comes from. Opacity always comes from the transfer
// function's alpha.
//  - TransferFunction: the texture's red channel is the scalar, and the transfer
//    function maps it to both colour and alpha.
//  - TextureRgba: the texture must be RGBA8_UNORM. Its RGB is the colour, and its
//    alpha is the scalar the transfer function maps to opacity (the transfer
//    function's own colour is ignored). This lets colour show one quantity and
//    opacity another, e.g. colour by speed and opacity by density.
enum class VolumeColorSource
{
	TransferFunction,
	TextureRgba
};

// How a volume is turned into an image (lightVulkanGraphics 3.2; set with
// VkApp::setVolumeStyle -- VolumeRenderDescription is unchanged for ABI stability).
//  - Composite: front-to-back emission/absorption through the transfer function.
//  - MaximumIntensity / MinimumIntensity: the largest / smallest scalar along each ray
//    (MIP / MinIP), coloured by the transfer function, whose opacity (times
//    opacityScale x 4) sets how strongly it shows.
//  - Isosurface: the first point where the scalar reaches isoValue, lit by the
//    gradient; opaque.
//  - Average: the mean scalar along each ray (a simulated radiograph).
enum class VolumeRenderStyle
{
	Composite,
	MaximumIntensity,
	MinimumIntensity,
	Isosurface,
	Average
};

struct VolumeStyleDescription
{
	VolumeRenderStyle style = VolumeRenderStyle::Composite;
	// Gradient (headlight) shading in Composite; Isosurface is always shaded.
	bool shading = false;
	// 0 = flat .. 1 = fully lit.
	float shadingStrength = 0.75f;
	// Normalised scalar (0..1 after densityScale) for Isosurface.
	float isoValue = 0.5f;
};

[[nodiscard]] const char* volumeRenderStyleName(VolumeRenderStyle style);

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
	VolumeColorSource colorSource = VolumeColorSource::TransferFunction;
	float referenceStepLength = 1.0f;
	bool normalizeOpacityByStepLength = true;
};


// ---------------------------------------------------------------------------------------
// Bricked (sparse) volumes (3.3)
//
// A volume too large for one 3D texture -- or one that is mostly empty -- is split into
// cubic bricks of `brickSize` voxels. Only the bricks the application chooses to make
// resident occupy GPU memory: they live in an atlas (a single Texture3D) and an indirection
// texture (one RGBA8 texel per brick) tells the ray marcher where each one is. Samples in a
// non-resident brick read as zero, so empty space costs nothing but a lookup.
//
// Every brick is stored with a one-voxel border copied from its neighbours
// ((brickSize + 2)^3 samples), so hardware trilinear filtering is seamless across brick
// faces. At the volume's outer faces the application replicates the edge voxels.
// ---------------------------------------------------------------------------------------

struct Texture3DRegionUpdate
{
	glm::uvec3 offset{0u};
	glm::uvec3 extent{1u};
	const void* data = nullptr;
	// Must equal extent.x * extent.y * extent.z * the texture's texel size.
	std::size_t byteSize = 0;
};

struct BrickedTextureDescription
{
	// Size of the whole volume in voxels (any size; not limited by maxImageDimension3D).
	glm::uvec3 voxels{0u};
	// Voxels per brick edge, excluding the 1-voxel border. 8..254.
	std::uint32_t brickSize = 64;
	// Scalar format of the atlas. RGBA8_UNORM is allowed (colour volumes).
	TextureFormat format = TextureFormat::R16_UNORM;
	// Atlas capacity in bricks per axis (1..255). The atlas is
	// atlasBricks * (brickSize + 2) texels per axis and must fit the device's 3D limit.
	glm::uvec3 atlasBricks{8u};
};

struct BrickUpload
{
	// Brick coordinates in the brick grid (voxel / brickSize).
	glm::uvec3 brick{0u};
	// (brickSize + 2)^3 samples in the atlas format, x fastest; the border comes first.
	const void* data = nullptr;
	std::size_t byteSize = 0;
};

// Which atlas slot holds which brick, with least-recently-used eviction. Pure bookkeeping
// (no Vulkan), exposed so applications can plan uploads and so it can be tested alone.
class BrickPool
{
public:
	BrickPool(glm::uvec3 gridBricks, glm::uvec3 atlasBricks);

	struct Placement
	{
		glm::uvec3 slot{0u};
		// True when another brick had to be evicted to make room.
		bool evicted = false;
		glm::uvec3 evictedBrick{0u};
	};

	// Slot for `brick`: its current one (marked used), a free one, or -- when
	// `allowEviction` -- that of the least recently used brick. Empty when full.
	std::optional<Placement> acquire(glm::uvec3 brick, bool allowEviction);
	bool release(glm::uvec3 brick);
	void touch(glm::uvec3 brick);
	[[nodiscard]] bool resident(glm::uvec3 brick) const;
	[[nodiscard]] std::optional<glm::uvec3> slotOf(glm::uvec3 brick) const;
	[[nodiscard]] std::size_t residentCount() const noexcept { return resident_; }
	[[nodiscard]] std::size_t capacity() const noexcept { return slotBrick_.size(); }
	[[nodiscard]] glm::uvec3 grid() const noexcept { return grid_; }
	[[nodiscard]] glm::uvec3 atlasBricks() const noexcept { return atlas_; }
	// RGBA8 indirection texel: slot x, y, z and 255 (resident), or all zero.
	[[nodiscard]] static std::array<std::uint8_t, 4> encode(const std::optional<glm::uvec3>& slot);

private:
	[[nodiscard]] std::size_t brickIndex(glm::uvec3 brick) const;
	[[nodiscard]] std::size_t slotIndex(glm::uvec3 slot) const;
	[[nodiscard]] glm::uvec3 slotCoordinate(std::size_t index) const;

	glm::uvec3 grid_;
	glm::uvec3 atlas_;
	std::vector<std::int64_t> brickSlot_;   // per brick: slot index or -1
	std::vector<std::int64_t> slotBrick_;   // per slot: brick index or -1
	std::vector<std::uint64_t> slotUsed_;   // per slot: last use tick
	std::vector<std::size_t> freeSlots_;
	std::uint64_t tick_ = 0;
	std::size_t resident_ = 0;
};

void validateBrickedTextureDescription(const BrickedTextureDescription& description);
[[nodiscard]] glm::uvec3 brickGridSize(const BrickedTextureDescription& description);

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
[[nodiscard]] const char* volumeColorSourceName(VolumeColorSource source);
[[nodiscard]] VolumeColorSource parseVolumeColorSource(const std::string& name);
[[nodiscard]] const char* renderLayerName(RenderLayer layer);
[[nodiscard]] RenderLayer parseRenderLayer(const std::string& name);
[[nodiscard]] std::uint32_t renderLayerOrdinal(RenderLayer layer);
void validateDrawOptions(const DrawOptions& options);
[[nodiscard]] std::vector<DrawOrderEntry> sortDrawOrder(
	std::vector<DrawOrderEntry> entries);

}
