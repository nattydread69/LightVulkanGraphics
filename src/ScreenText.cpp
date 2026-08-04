// SPDX-License-Identifier: LGPL-3.0-or-later

#include "VkApp.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505)
#endif
#define STB_EASY_FONT_IMPLEMENTATION
#include "stb/stb_easy_font.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace lightGraphics
{
namespace
{
constexpr std::size_t EASY_FONT_BYTES_PER_CHARACTER = 270;
constexpr std::size_t EASY_FONT_VERTICES_PER_CHARACTER = 20;
constexpr std::size_t EASY_FONT_INDICES_PER_CHARACTER = 30;
// Mesh capacity is always sized for a shadow copy of the glyphs plus the
// real ones (see ScreenTextDescription::shadowEnabled), so toggling the
// shadow on later never requires recreating the mesh.
constexpr std::size_t EASY_FONT_TEXT_PASSES = 2;
// Capacity for the optional background rectangle (see
// ScreenTextDescription::backgroundEnabled) is likewise always reserved, one
// quad's worth, so toggling it on later never requires recreating the mesh.
constexpr std::size_t BACKGROUND_QUAD_VERTICES = 4;
constexpr std::size_t BACKGROUND_QUAD_INDICES = 6;

struct EasyFontVertex
{
	float x;
	float y;
	float z;
	unsigned char color[4];
};

static_assert(sizeof(EasyFontVertex) == 16);

bool
finite(const glm::vec2& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y);
}

bool
finite(const glm::vec4& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) &&
		std::isfinite(value.z) && std::isfinite(value.w);
}

MeshData
hiddenTextMesh()
{
	MeshData mesh;
	mesh.vertices.resize(3);
	for (MeshVertex& vertex : mesh.vertices)
	{
		vertex.position = glm::vec3(-1.0f, -1.0f, 0.0f);
		vertex.color = glm::vec4(0.0f);
	}
	mesh.indices = {0, 1, 2};
	return mesh;
}

std::string
sanitizedEasyFontText(const std::string& text)
{
	std::string result = text;
	for (char& character : result)
	{
		unsigned char const value = static_cast<unsigned char>(character);
		if (character != '\n' && (value < 32 || value > 126))
		{
			character = '?';
		}
	}
	return result;
}

std::size_t
checkedCapacity(std::size_t characters, std::size_t perCharacter)
{
	if (characters > std::numeric_limits<std::size_t>::max() / perCharacter)
	{
		throw std::overflow_error("Screen text capacity overflows size_t");
	}
	return characters * perCharacter;
}

template <typename Resources>
void
validateScreenTextHandle(
	const Resources& resources,
	ScreenTextHandle handle)
{
	if (handle.index >= resources.size() ||
		!resources[handle.index].alive ||
		resources[handle.index].generation != handle.generation)
	{
		throw std::invalid_argument("Screen text handle is invalid or stale");
	}
}
}

void
validateScreenTextDescription(const ScreenTextDescription& description)
{
	if (!finite(description.positionPixels))
	{
		throw std::invalid_argument("Screen text position must be finite");
	}
	if (!std::isfinite(description.scale) || description.scale <= 0.0f)
	{
		throw std::invalid_argument("Screen text scale must be finite and positive");
	}
	if (!finite(description.color))
	{
		throw std::invalid_argument("Screen text color must be finite");
	}
	if (!std::isfinite(description.sortKey))
	{
		throw std::invalid_argument("Screen text sortKey must be finite");
	}
	if (!finite(description.shadowColor))
	{
		throw std::invalid_argument("Screen text shadow color must be finite");
	}
	if (!finite(description.shadowOffsetPixels))
	{
		throw std::invalid_argument(
			"Screen text shadow offset must be finite");
	}
	if (!finite(description.backgroundColor))
	{
		throw std::invalid_argument(
			"Screen text background color must be finite");
	}
	if (!std::isfinite(description.backgroundPaddingPixels) ||
		description.backgroundPaddingPixels < 0.0f)
	{
		throw std::invalid_argument(
			"Screen text background padding must be finite and non-negative");
	}
	if (description.maximumCharacters == 0)
	{
		throw std::invalid_argument(
			"Screen text maximumCharacters must be non-zero");
	}
	if (description.text.size() > description.maximumCharacters)
	{
		throw std::length_error(
			"Screen text exceeds its maximumCharacters capacity");
	}
	(void)checkedCapacity(
		description.maximumCharacters,
		EASY_FONT_VERTICES_PER_CHARACTER * EASY_FONT_TEXT_PASSES);
	(void)checkedCapacity(
		description.maximumCharacters,
		EASY_FONT_INDICES_PER_CHARACTER * EASY_FONT_TEXT_PASSES);
	std::size_t const scratchCapacity = checkedCapacity(
		description.maximumCharacters,
		EASY_FONT_BYTES_PER_CHARACTER);
	if (scratchCapacity >
		static_cast<std::size_t>(std::numeric_limits<int>::max()))
	{
		throw std::length_error(
			"Screen text maximumCharacters exceeds stb_easy_font capacity");
	}
}

MeshData
buildScreenTextMesh(
	const ScreenTextDescription& description,
	std::uint32_t framebufferWidth,
	std::uint32_t framebufferHeight)
{
	validateScreenTextDescription(description);
	if (framebufferWidth == 0 || framebufferHeight == 0)
	{
		throw std::invalid_argument(
			"Screen text framebuffer dimensions must be non-zero");
	}
	if (!description.visible || description.text.empty())
	{
		return hiddenTextMesh();
	}

	std::string text = sanitizedEasyFontText(description.text);
	std::size_t const scratchSize = std::max<std::size_t>(
		checkedCapacity(text.size(), EASY_FONT_BYTES_PER_CHARACTER),
		64);
	if (scratchSize > static_cast<std::size_t>(std::numeric_limits<int>::max()))
	{
		throw std::length_error("Screen text is too large for stb_easy_font");
	}
	std::vector<unsigned char> scratch(scratchSize);
	int const quadCount = stb_easy_font_print(
		0.0f,
		0.0f,
		text.data(),
		nullptr,
		scratch.data(),
		static_cast<int>(scratch.size()));
	if (quadCount <= 0)
	{
		return hiddenTextMesh();
	}

	// Decode stb_easy_font's quads once into local (unscaled, unpositioned)
	// corners, then stamp them out once per pass below -- one pass for the
	// shadow copy (if enabled) and one for the real glyphs, so the geometry
	// doesn't need to be re-decoded per pass.
	std::vector<glm::vec2> localCorners(static_cast<std::size_t>(quadCount) * 4);
	for (int quad = 0; quad < quadCount; ++quad)
	{
		for (int corner = 0; corner < 4; ++corner)
		{
			EasyFontVertex source;
			std::size_t const offset =
				static_cast<std::size_t>(quad * 4 + corner) *
				sizeof(EasyFontVertex);
			std::memcpy(&source, scratch.data() + offset, sizeof(source));
			localCorners[static_cast<std::size_t>(quad * 4 + corner)] =
				glm::vec2(source.x, source.y);
		}
	}

	bool const withShadow = description.shadowEnabled;
	std::size_t const passCount = withShadow ? 2 : 1;

	MeshData mesh;
	mesh.vertices.reserve(
		BACKGROUND_QUAD_VERTICES + static_cast<std::size_t>(quadCount) * 4 * passCount);
	mesh.indices.reserve(
		BACKGROUND_QUAD_INDICES + static_cast<std::size_t>(quadCount) * 6 * passCount);
	float const width = static_cast<float>(framebufferWidth);
	float const height = static_cast<float>(framebufferHeight);

	// The background rectangle is emitted first (if enabled) so the shadow
	// and real glyphs composite on top of it -- everything here is alpha
	// blended with depth testing off, so draw order within this one mesh is
	// the paint order. It's sized to the text's own pixel bounding box
	// (across every pass, so it covers the shadow offset too) plus padding.
	if (description.backgroundEnabled)
	{
		glm::vec2 pixelMin(std::numeric_limits<float>::max());
		glm::vec2 pixelMax(std::numeric_limits<float>::lowest());
		for (std::size_t pass = 0; pass < passCount; ++pass)
		{
			bool const isShadowPassForBounds = withShadow && pass == 0;
			glm::vec2 const boundsOffset = isShadowPassForBounds
				? description.shadowOffsetPixels
				: glm::vec2(0.0f);
			for (const glm::vec2& local : localCorners)
			{
				glm::vec2 const pixel =
					description.positionPixels + boundsOffset + local * description.scale;
				pixelMin = glm::min(pixelMin, pixel);
				pixelMax = glm::max(pixelMax, pixel);
			}
		}
		glm::vec2 const padding(description.backgroundPaddingPixels);
		pixelMin -= padding;
		pixelMax += padding;

		std::uint32_t const base = static_cast<std::uint32_t>(mesh.vertices.size());
		glm::vec2 const corners[4] = {
			{pixelMin.x, pixelMin.y},
			{pixelMax.x, pixelMin.y},
			{pixelMax.x, pixelMax.y},
			{pixelMin.x, pixelMax.y},
		};
		for (const glm::vec2& corner : corners)
		{
			MeshVertex vertex;
			vertex.position = glm::vec3(
				2.0f * corner.x / width - 1.0f,
				2.0f * corner.y / height - 1.0f,
				0.0f);
			vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
			vertex.color = description.backgroundColor;
			mesh.vertices.push_back(vertex);
		}
		mesh.indices.insert(
			mesh.indices.end(),
			{base, base + 1, base + 2, base, base + 2, base + 3});
	}

	// The shadow pass is emitted next so the real glyphs composite on top
	// of it -- both are alpha blended with depth testing off, so draw order
	// within this one mesh is the paint order.
	for (std::size_t pass = 0; pass < passCount; ++pass)
	{
		bool const isShadowPass = withShadow && pass == 0;
		glm::vec2 const passOffset =
			isShadowPass ? description.shadowOffsetPixels : glm::vec2(0.0f);
		glm::vec4 const passColor =
			isShadowPass ? description.shadowColor : description.color;

		for (int quad = 0; quad < quadCount; ++quad)
		{
			std::uint32_t const base =
				static_cast<std::uint32_t>(mesh.vertices.size());
			for (int corner = 0; corner < 4; ++corner)
			{
				glm::vec2 const local =
					localCorners[static_cast<std::size_t>(quad * 4 + corner)];
				float const pixelX = description.positionPixels.x +
					passOffset.x + local.x * description.scale;
				float const pixelY = description.positionPixels.y +
					passOffset.y + local.y * description.scale;
				MeshVertex vertex;
				vertex.position = glm::vec3(
					2.0f * pixelX / width - 1.0f,
					2.0f * pixelY / height - 1.0f,
					0.0f);
				vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
				vertex.color = passColor;
				mesh.vertices.push_back(vertex);
			}
			mesh.indices.insert(
				mesh.indices.end(),
				{base, base + 1, base + 2, base, base + 2, base + 3});
		}
	}
	return mesh;
}

MaterialHandle
VkApp::getOrCreateScreenTextMaterial()
{
	if (screenTextMaterial_.isValid() &&
		screenTextMaterial_.index < materials_.size())
	{
		MaterialResource const& material =
			materials_[screenTextMaterial_.index];
		if (material.alive &&
			material.generation == screenTextMaterial_.generation)
		{
			return screenTextMaterial_;
		}
	}

	MaterialDescription description;
	description.alphaBlendingEnabled = true;
	description.depthTestEnabled = false;
	description.depthWriteEnabled = false;
	description.cullMode = CullMode::None;
	description.shaderProgram.selection = ShaderSelection::Custom;
	description.shaderProgram.vertexShader =
		findShaderPath("screen_text.vert.spv");
	description.shaderProgram.fragmentShader =
		findShaderPath("screen_text.frag.spv");
	screenTextMaterial_ = createMaterial(description);
	return screenTextMaterial_;
}

void
VkApp::updateScreenTextMesh(ScreenTextResource& resource)
{
	MeshData const mesh = buildScreenTextMesh(
		resource.description,
		swapChainExtent_.width,
		swapChainExtent_.height);
	updateDynamicMesh(resource.mesh, mesh);
}

ScreenTextHandle
VkApp::createScreenText(const ScreenTextDescription& description)
{
	validateScreenTextDescription(description);
	MaterialHandle const material = getOrCreateScreenTextMaterial();
	std::size_t const maximumVertices = std::max<std::size_t>(
		BACKGROUND_QUAD_VERTICES + checkedCapacity(
			description.maximumCharacters,
			EASY_FONT_VERTICES_PER_CHARACTER * EASY_FONT_TEXT_PASSES),
		3);
	std::size_t const maximumIndices = std::max<std::size_t>(
		BACKGROUND_QUAD_INDICES + checkedCapacity(
			description.maximumCharacters,
			EASY_FONT_INDICES_PER_CHARACTER * EASY_FONT_TEXT_PASSES),
		3);
	MeshHandle mesh = createDynamicMesh(maximumVertices, maximumIndices);

	std::uint32_t const index = freeScreenTexts_.empty()
		? static_cast<std::uint32_t>(screenTexts_.size())
		: freeScreenTexts_.back();
	if (freeScreenTexts_.empty())
	{
		screenTexts_.emplace_back();
	}
	else
	{
		freeScreenTexts_.pop_back();
	}
	ScreenTextResource& resource = screenTexts_[index];
	resource.description = description;
	resource.mesh = mesh;
	try
	{
		updateScreenTextMesh(resource);
		drawMesh(
			mesh,
			Transform{},
			material,
			{RenderLayer::Overlay, description.sortKey});
	}
	catch (...)
	{
		destroyMesh(mesh);
		freeScreenTexts_.push_back(index);
		throw;
	}
	resource.alive = true;
	return {index, resource.generation};
}

void
VkApp::updateScreenText(
	ScreenTextHandle handle,
	const ScreenTextDescription& description)
{
	validateScreenTextHandle(screenTexts_, handle);
	validateScreenTextDescription(description);
	ScreenTextResource& resource = screenTexts_[handle.index];

	bool const capacityChanged =
		description.maximumCharacters !=
		resource.description.maximumCharacters;
	if (capacityChanged)
	{
		std::size_t const maximumVertices = std::max<std::size_t>(
			BACKGROUND_QUAD_VERTICES + checkedCapacity(
				description.maximumCharacters,
				EASY_FONT_VERTICES_PER_CHARACTER * EASY_FONT_TEXT_PASSES),
			3);
		std::size_t const maximumIndices = std::max<std::size_t>(
			BACKGROUND_QUAD_INDICES + checkedCapacity(
				description.maximumCharacters,
				EASY_FONT_INDICES_PER_CHARACTER * EASY_FONT_TEXT_PASSES),
			3);
		MeshHandle const replacement =
			createDynamicMesh(maximumVertices, maximumIndices);
		MeshData const mesh = buildScreenTextMesh(
			description,
			swapChainExtent_.width,
			swapChainExtent_.height);
		try
		{
			updateDynamicMesh(replacement, mesh);
			drawMesh(
				replacement,
				Transform{},
				getOrCreateScreenTextMaterial(),
				{RenderLayer::Overlay, description.sortKey});
		}
		catch (...)
		{
			destroyMesh(replacement);
			throw;
		}
		destroyMesh(resource.mesh);
		resource.mesh = replacement;
	}
	else
	{
		ScreenTextDescription const previous = resource.description;
		resource.description = description;
		try
		{
			updateScreenTextMesh(resource);
		}
		catch (...)
		{
			resource.description = previous;
			throw;
		}
		for (MeshDrawRequest& request : meshDrawRequests_)
		{
			if (request.mesh.index == resource.mesh.index &&
				request.mesh.generation == resource.mesh.generation)
			{
				request.drawOptions =
					{RenderLayer::Overlay, description.sortKey};
			}
		}
		rebuildOrderedDrawResources();
	}
	resource.description = description;
}

void
VkApp::updateScreenText(
	ScreenTextHandle handle,
	const std::string& text)
{
	validateScreenTextHandle(screenTexts_, handle);
	ScreenTextDescription description =
		screenTexts_[handle.index].description;
	description.text = text;
	updateScreenText(handle, description);
}

void
VkApp::setScreenTextVisible(ScreenTextHandle handle, bool visible)
{
	validateScreenTextHandle(screenTexts_, handle);
	ScreenTextDescription description =
		screenTexts_[handle.index].description;
	description.visible = visible;
	updateScreenText(handle, description);
}

void
VkApp::destroyScreenText(ScreenTextHandle handle)
{
	validateScreenTextHandle(screenTexts_, handle);
	ScreenTextResource& resource = screenTexts_[handle.index];
	destroyMesh(resource.mesh);
	resource.mesh = {};
	resource.alive = false;
	++resource.generation;
	freeScreenTexts_.push_back(handle.index);
}

void
VkApp::rebuildScreenTextMeshes()
{
	for (ScreenTextResource& resource : screenTexts_)
	{
		if (resource.alive)
		{
			updateScreenTextMesh(resource);
		}
	}
}
}
