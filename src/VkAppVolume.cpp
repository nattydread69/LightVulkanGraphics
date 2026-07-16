// SPDX-License-Identifier: LGPL-3.0-or-later

#include "VkApp.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace lightGraphics
{
namespace
{

void checkVk(VkResult result, const char* operation)
{
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error(operation);
	}
}

VkFormat textureFormat(TextureFormat format)
{
	switch (format)
	{
	case TextureFormat::R8_UNORM:
		return VK_FORMAT_R8_UNORM;
	case TextureFormat::R16_UNORM:
		return VK_FORMAT_R16_UNORM;
	case TextureFormat::R16_SFLOAT:
		return VK_FORMAT_R16_SFLOAT;
	case TextureFormat::R32_SFLOAT:
		return VK_FORMAT_R32_SFLOAT;
	case TextureFormat::RGBA8_UNORM:
		return VK_FORMAT_R8G8B8A8_UNORM;
	}
	throw std::invalid_argument("Unknown TextureFormat");
}

VkCullModeFlags cullMode(CullMode mode)
{
	switch (mode)
	{
	case CullMode::None:
		return VK_CULL_MODE_NONE;
	case CullMode::Front:
		return VK_CULL_MODE_FRONT_BIT;
	case CullMode::Back:
		return VK_CULL_MODE_BACK_BIT;
	}
	return VK_CULL_MODE_NONE;
}

struct VolumePushConstants
{
	glm::vec4 cameraWorld{0.0f};
	glm::vec4 volumeMinimum{0.0f};
	glm::vec4 volumeMaximum{1.0f};
	glm::vec4 clipPlane{0.0f};
	glm::vec4 clipBoxMinimum{0.0f};
	glm::vec4 clipBoxMaximum{1.0f};
	glm::vec4 settings{1.0f};
	glm::ivec4 flags{0};
};

static_assert(sizeof(VolumePushConstants) == 128,
	"Volume push constants must fit Vulkan's minimum guaranteed size");

}

MeshHandle VkApp::createStaticMesh(const MeshData& meshData)
{
	validateMeshData(meshData);
	const VkDeviceSize vertexBytes = sizeof(MeshVertex) * meshData.vertices.size();
	const VkDeviceSize indexBytes = sizeof(std::uint32_t) * meshData.indices.size();
	detail::Buffer vertexStaging;
	detail::Buffer indexStaging;
	createBuffer(vertexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		vertexStaging);
	createBuffer(indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		indexStaging);
	void* mapped = nullptr;
	checkVk(vkMapMemory(device_, vertexStaging.memory, 0, vertexBytes, 0, &mapped),
		"Failed to map static mesh vertex staging memory");
	std::memcpy(mapped, meshData.vertices.data(), static_cast<std::size_t>(vertexBytes));
	vkUnmapMemory(device_, vertexStaging.memory);
	checkVk(vkMapMemory(device_, indexStaging.memory, 0, indexBytes, 0, &mapped),
		"Failed to map static mesh index staging memory");
	std::memcpy(mapped, meshData.indices.data(), static_cast<std::size_t>(indexBytes));
	vkUnmapMemory(device_, indexStaging.memory);

	const std::uint32_t index = freeCustomMeshes_.empty()
		? static_cast<std::uint32_t>(customMeshes_.size())
		: freeCustomMeshes_.back();
	if (freeCustomMeshes_.empty())
	{
		customMeshes_.emplace_back();
	}
	else
	{
		freeCustomMeshes_.pop_back();
	}
	CustomMeshResource& mesh = customMeshes_[index];
	try
	{
		createBuffer(vertexBytes,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.vertexBuffer);
		createBuffer(indexBytes,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.indexBuffer);
		copyBuffer(vertexStaging.buffer, mesh.vertexBuffer.buffer, vertexBytes);
		copyBuffer(indexStaging.buffer, mesh.indexBuffer.buffer, indexBytes);
	}
	catch (...)
	{
		destroyBuffer(device_, vertexStaging);
		destroyBuffer(device_, indexStaging);
		destroyBuffer(device_, mesh.vertexBuffer);
		destroyBuffer(device_, mesh.indexBuffer);
		freeCustomMeshes_.push_back(index);
		throw;
	}
	destroyBuffer(device_, vertexStaging);
	destroyBuffer(device_, indexStaging);
	mesh.alive = true;
	mesh.dynamic = false;
	mesh.maximumVertexCount = meshData.vertices.size();
	mesh.maximumIndexCount = meshData.indices.size();
	mesh.vertexCount = static_cast<std::uint32_t>(meshData.vertices.size());
	mesh.indexCount = static_cast<std::uint32_t>(meshData.indices.size());
	return {index, mesh.generation};
}

MeshHandle VkApp::createDynamicMesh(
	std::size_t maximumVertexCount,
	std::size_t maximumIndexCount)
{
	validateDynamicMeshCapacity(maximumVertexCount, maximumIndexCount);
	const std::uint32_t index = freeCustomMeshes_.empty()
		? static_cast<std::uint32_t>(customMeshes_.size())
		: freeCustomMeshes_.back();
	if (freeCustomMeshes_.empty())
	{
		customMeshes_.emplace_back();
	}
	else
	{
		freeCustomMeshes_.pop_back();
	}
	CustomMeshResource& mesh = customMeshes_[index];
	try
	{
		createBuffer(sizeof(MeshVertex) * maximumVertexCount,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			mesh.vertexBuffer);
		createBuffer(sizeof(std::uint32_t) * maximumIndexCount,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			mesh.indexBuffer);
	}
	catch (...)
	{
		destroyBuffer(device_, mesh.vertexBuffer);
		destroyBuffer(device_, mesh.indexBuffer);
		freeCustomMeshes_.push_back(index);
		throw;
	}
	mesh.alive = true;
	mesh.dynamic = true;
	mesh.maximumVertexCount = maximumVertexCount;
	mesh.maximumIndexCount = maximumIndexCount;
	return {index, mesh.generation};
}

void VkApp::updateDynamicMesh(MeshHandle handle, const MeshData& meshData)
{
	if (handle.index >= customMeshes_.size())
	{
		throw std::out_of_range("Dynamic mesh handle index is invalid");
	}
	CustomMeshResource& mesh = customMeshes_[handle.index];
	if (!mesh.alive || mesh.generation != handle.generation)
	{
		throw std::invalid_argument("Dynamic mesh handle is stale");
	}
	if (!mesh.dynamic)
	{
		throw std::invalid_argument("Static meshes cannot be updated");
	}
	validateDynamicMeshUpdate(
		mesh.maximumVertexCount, mesh.maximumIndexCount, meshData);
	void* mapped = nullptr;
	const VkDeviceSize vertexBytes = sizeof(MeshVertex) * meshData.vertices.size();
	const VkDeviceSize indexBytes = sizeof(std::uint32_t) * meshData.indices.size();
	checkVk(vkMapMemory(device_, mesh.vertexBuffer.memory, 0, vertexBytes, 0, &mapped),
		"Failed to map dynamic mesh vertex memory");
	std::memcpy(mapped, meshData.vertices.data(), static_cast<std::size_t>(vertexBytes));
	vkUnmapMemory(device_, mesh.vertexBuffer.memory);
	checkVk(vkMapMemory(device_, mesh.indexBuffer.memory, 0, indexBytes, 0, &mapped),
		"Failed to map dynamic mesh index memory");
	std::memcpy(mapped, meshData.indices.data(), static_cast<std::size_t>(indexBytes));
	vkUnmapMemory(device_, mesh.indexBuffer.memory);
	mesh.vertexCount = static_cast<std::uint32_t>(meshData.vertices.size());
	mesh.indexCount = static_cast<std::uint32_t>(meshData.indices.size());
}

void VkApp::drawMesh(
	MeshHandle meshHandle,
	const Transform& transform,
	MaterialHandle materialHandle)
{
	if (materialHandle.index >= materials_.size() ||
		!materials_[materialHandle.index].alive ||
		materials_[materialHandle.index].generation != materialHandle.generation)
	{
		throw std::invalid_argument("drawMesh received an invalid material handle");
	}
	const RenderLayer defaultLayer =
		materials_[materialHandle.index].description.alphaBlendingEnabled
		? RenderLayer::Transparent : RenderLayer::Opaque;
	drawMesh(meshHandle, transform, materialHandle, {defaultLayer, 0.0f});
}

void VkApp::drawMesh(
	MeshHandle meshHandle,
	const Transform& transform,
	MaterialHandle materialHandle,
	const DrawOptions& options)
{
	if (meshHandle.index >= customMeshes_.size() ||
		!customMeshes_[meshHandle.index].alive ||
		customMeshes_[meshHandle.index].generation != meshHandle.generation)
	{
		throw std::invalid_argument("drawMesh received an invalid mesh handle");
	}
	if (materialHandle.index >= materials_.size() ||
		!materials_[materialHandle.index].alive ||
		materials_[materialHandle.index].generation != materialHandle.generation)
	{
		throw std::invalid_argument("drawMesh received an invalid material handle");
	}
	validateDrawOptions(options);
	meshDrawRequests_.push_back(
		{meshHandle, materialHandle, transform, options, nextDrawSubmissionIndex_++});
	rebuildOrderedDrawResources();
}

void VkApp::clearMeshDraws()
{
	meshDrawRequests_.clear();
	rebuildOrderedDrawResources();
}

void VkApp::destroyMesh(MeshHandle handle)
{
	if (handle.index >= customMeshes_.size())
	{
		throw std::out_of_range("Mesh handle index is invalid");
	}
	CustomMeshResource& mesh = customMeshes_[handle.index];
	if (!mesh.alive || mesh.generation != handle.generation)
	{
		throw std::invalid_argument("Mesh handle is stale");
	}
	meshDrawRequests_.erase(
		std::remove_if(meshDrawRequests_.begin(), meshDrawRequests_.end(),
			[handle](const MeshDrawRequest& request)
			{
				return request.mesh.index == handle.index &&
					request.mesh.generation == handle.generation;
			}),
		meshDrawRequests_.end());
	rebuildOrderedDrawResources();
	destroyBuffer(device_, mesh.vertexBuffer);
	destroyBuffer(device_, mesh.indexBuffer);
	mesh.alive = false;
	++mesh.generation;
	freeCustomMeshes_.push_back(handle.index);
}

MaterialHandle VkApp::createMaterial(const MaterialDescription& description)
{
	validateMaterialDescription(description);
	if (description.polygonMode == PolygonMode::Line && !supportsNonSolidFill_)
	{
		throw std::runtime_error(
			"Line polygon mode is not supported by the selected Vulkan device");
	}
	if (description.shaderProgram.selection == ShaderSelection::VolumeRaymarch)
	{
		throw std::invalid_argument(
			"VolumeRaymarch is managed by createVolume, not createMaterial");
	}
	const std::uint32_t index = freeMaterials_.empty()
		? static_cast<std::uint32_t>(materials_.size())
		: freeMaterials_.back();
	if (freeMaterials_.empty())
	{
		materials_.emplace_back();
	}
	else
	{
		freeMaterials_.pop_back();
	}
	MaterialResource& material = materials_[index];
	material.description = description;
	VkPushConstantRange pushRange{};
	pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pushRange.size = sizeof(glm::mat4);
	VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &descriptorSetLayout_;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &pushRange;
	checkVk(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &material.layout),
		"Failed to create custom material pipeline layout");
	try
	{
		material.pipeline = createMaterialPipeline(description, material.layout);
	}
	catch (...)
	{
		vkDestroyPipelineLayout(device_, material.layout, nullptr);
		material.layout = VK_NULL_HANDLE;
		freeMaterials_.push_back(index);
		throw;
	}
	material.alive = true;
	return {index, material.generation};
}

void VkApp::destroyMaterial(MaterialHandle handle)
{
	if (handle.index >= materials_.size() || !materials_[handle.index].alive ||
		materials_[handle.index].generation != handle.generation)
	{
		throw std::invalid_argument("Material handle is invalid or stale");
	}
	MaterialResource& material = materials_[handle.index];
	meshDrawRequests_.erase(
		std::remove_if(meshDrawRequests_.begin(), meshDrawRequests_.end(),
			[handle](const MeshDrawRequest& request)
			{
				return request.material.index == handle.index &&
					request.material.generation == handle.generation;
			}), meshDrawRequests_.end());
	rebuildOrderedDrawResources();
	vkDestroyPipeline(device_, material.pipeline, nullptr);
	vkDestroyPipelineLayout(device_, material.layout, nullptr);
	material.pipeline = VK_NULL_HANDLE;
	material.layout = VK_NULL_HANDLE;
	material.alive = false;
	++material.generation;
	freeMaterials_.push_back(handle.index);
}

Texture3DHandle VkApp::createTexture3D(
	const Texture3DDescription& description,
	const void* sourceData,
	std::size_t byteSize)
{
	validateTexture3DDescription(description);
	if (sourceData == nullptr)
	{
		throw std::invalid_argument("Texture3D upload data cannot be null");
	}
	if (byteSize != texture3DByteSize(description))
	{
		throw std::invalid_argument("Texture3D upload byte size does not match its description");
	}
	const std::uint32_t index = freeTextures3D_.empty()
		? static_cast<std::uint32_t>(textures3D_.size())
		: freeTextures3D_.back();
	if (freeTextures3D_.empty())
	{
		textures3D_.emplace_back();
	}
	else
	{
		freeTextures3D_.pop_back();
	}
	Texture3DResource& texture = textures3D_[index];
	texture.description = description;
	detail::Buffer staging;
	createBuffer(byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		staging);
	void* mapped = nullptr;
	checkVk(vkMapMemory(device_, staging.memory, 0, byteSize, 0, &mapped),
		"Failed to map Texture3D staging memory");
	std::memcpy(mapped, sourceData, byteSize);
	vkUnmapMemory(device_, staging.memory);

	const VkFormat format = textureFormat(description.format);
	VkFormatProperties formatProperties{};
	vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &formatProperties);
	const VkFormatFeatureFlags requiredFeatures =
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
	if ((formatProperties.optimalTilingFeatures & requiredFeatures) != requiredFeatures)
	{
		destroyBuffer(device_, staging);
		freeTextures3D_.push_back(index);
		throw std::runtime_error(
			"Selected Vulkan device cannot linearly sample the requested Texture3D format");
	}
	VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
	imageInfo.imageType = VK_IMAGE_TYPE_3D;
	imageInfo.extent = {description.width, description.height, description.depth};
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	checkVk(vkCreateImage(device_, &imageInfo, nullptr, &texture.image),
		"Failed to create Texture3D image");
	VkMemoryRequirements requirements{};
	vkGetImageMemoryRequirements(device_, texture.image, &requirements);
	VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	allocation.allocationSize = requirements.size;
	allocation.memoryTypeIndex = findMemoryType(
		requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	checkVk(vkAllocateMemory(device_, &allocation, nullptr, &texture.memory),
		"Failed to allocate Texture3D image memory");
	checkVk(vkBindImageMemory(device_, texture.image, texture.memory, 0),
		"Failed to bind Texture3D image memory");
	transitionImageLayout(texture.image, format, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	copyBufferToImage3D(staging.buffer, texture.image,
		description.width, description.height, description.depth);
	transitionImageLayout(texture.image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	destroyBuffer(device_, staging);
	VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
	viewInfo.image = texture.image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;
	checkVk(vkCreateImageView(device_, &viewInfo, nullptr, &texture.view),
		"Failed to create Texture3D image view");
	VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.maxLod = 0.0f;
	checkVk(vkCreateSampler(device_, &samplerInfo, nullptr, &texture.sampler),
		"Failed to create Texture3D sampler");
	texture.alive = true;
	return {index, texture.generation};
}

void VkApp::updateTexture3D(
	Texture3DHandle handle,
	const void* sourceData,
	std::size_t byteSize)
{
	if (handle.index >= textures3D_.size() || !textures3D_[handle.index].alive ||
		textures3D_[handle.index].generation != handle.generation)
	{
		throw std::invalid_argument("Texture3D handle is invalid or stale");
	}
	Texture3DResource& texture = textures3D_[handle.index];
	if (sourceData == nullptr || byteSize != texture3DByteSize(texture.description))
	{
		throw std::invalid_argument("Texture3D update byte size does not match its description");
	}
	detail::Buffer staging;
	createBuffer(byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		staging);
	void* mapped = nullptr;
	checkVk(vkMapMemory(device_, staging.memory, 0, byteSize, 0, &mapped),
		"Failed to map Texture3D update staging memory");
	std::memcpy(mapped, sourceData, byteSize);
	vkUnmapMemory(device_, staging.memory);
	const VkFormat format = textureFormat(texture.description.format);
	transitionImageLayout(texture.image, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	copyBufferToImage3D(staging.buffer, texture.image, texture.description.width,
		texture.description.height, texture.description.depth);
	transitionImageLayout(texture.image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	destroyBuffer(device_, staging);
}

void VkApp::destroyTexture3D(Texture3DHandle handle)
{
	if (handle.index >= textures3D_.size() || !textures3D_[handle.index].alive ||
		textures3D_[handle.index].generation != handle.generation)
	{
		throw std::invalid_argument("Texture3D handle is invalid or stale");
	}
	for (const VolumeResource& volume : volumes_)
	{
		if (volume.alive && volume.description.volumeTexture.index == handle.index &&
			volume.description.volumeTexture.generation == handle.generation)
		{
			throw std::logic_error("Texture3D is still referenced by a volume");
		}
	}
	Texture3DResource& texture = textures3D_[handle.index];
	const std::uint32_t nextGeneration = texture.generation + 1;
	vkDestroySampler(device_, texture.sampler, nullptr);
	vkDestroyImageView(device_, texture.view, nullptr);
	vkDestroyImage(device_, texture.image, nullptr);
	vkFreeMemory(device_, texture.memory, nullptr);
	texture = Texture3DResource{};
	texture.generation = nextGeneration;
	freeTextures3D_.push_back(handle.index);
}

TransferFunctionHandle VkApp::createTransferFunction(
	const std::vector<TransferFunctionPoint>& points)
{
	const std::vector<TransferFunctionPoint> normalized =
		normalizeTransferFunctionPoints(points);
	const std::vector<std::uint8_t> pixels =
		sampleTransferFunctionRgba8(normalized);
	const std::uint32_t index = freeTransferFunctions_.empty()
		? static_cast<std::uint32_t>(transferFunctions_.size())
		: freeTransferFunctions_.back();
	if (freeTransferFunctions_.empty())
	{
		transferFunctions_.emplace_back();
	}
	else
	{
		freeTransferFunctions_.pop_back();
	}
	TransferFunctionResource& transfer = transferFunctions_[index];
	transfer.points = normalized;
	// Transfer functions are not periodic. Repeating linear sampling blends the
	// transparent first texel with the opaque final texel at scalar zero, which
	// makes an otherwise empty volume reveal its proxy cube.
	transfer.texture = createTextureFromPixels(
		pixels.data(), 256, 1, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
	if (!transfer.texture)
	{
		freeTransferFunctions_.push_back(index);
		throw std::runtime_error("Failed to create transfer-function lookup texture");
	}
	transfer.alive = true;
	return {index, transfer.generation};
}

TransferFunctionHandle VkApp::createTransferFunction(TransferFunctionPreset preset)
{
	return createTransferFunction(transferFunctionPreset(preset));
}

void VkApp::destroyTransferFunction(TransferFunctionHandle handle)
{
	if (handle.index >= transferFunctions_.size() ||
		!transferFunctions_[handle.index].alive ||
		transferFunctions_[handle.index].generation != handle.generation)
	{
		throw std::invalid_argument("Transfer-function handle is invalid or stale");
	}
	for (const VolumeResource& volume : volumes_)
	{
		if (volume.alive && volume.description.transferFunction.index == handle.index &&
			volume.description.transferFunction.generation == handle.generation)
		{
			throw std::logic_error("Transfer function is still referenced by a volume");
		}
	}
	TransferFunctionResource& transfer = transferFunctions_[handle.index];
	destroyTexture(*transfer.texture);
	transfer.texture.reset();
	transfer.points.clear();
	transfer.alive = false;
	++transfer.generation;
	freeTransferFunctions_.push_back(handle.index);
}

VolumeHandle VkApp::createVolume(const VolumeRenderDescription& description)
{
	validateVolumeRenderDescription(description);
	if (description.volumeTexture.index >= textures3D_.size() ||
		!textures3D_[description.volumeTexture.index].alive ||
		textures3D_[description.volumeTexture.index].generation !=
			description.volumeTexture.generation)
	{
		throw std::invalid_argument("Volume references an invalid Texture3D handle");
	}
	if (description.transferFunction.index >= transferFunctions_.size() ||
		!transferFunctions_[description.transferFunction.index].alive ||
		transferFunctions_[description.transferFunction.index].generation !=
			description.transferFunction.generation)
	{
		throw std::invalid_argument("Volume references an invalid transfer function");
	}
	MeshData cube;
	const std::array<glm::vec3, 8> positions{{
		{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
		{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}};
	for (const glm::vec3& position : positions)
	{
		MeshVertex vertex;
		vertex.position = position;
		cube.vertices.push_back(vertex);
	}
	cube.indices = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7,
		0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2,
		0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
	const std::uint32_t index = freeVolumes_.empty()
		? static_cast<std::uint32_t>(volumes_.size())
		: freeVolumes_.back();
	if (freeVolumes_.empty())
	{
		volumes_.emplace_back();
	}
	else
	{
		freeVolumes_.pop_back();
	}
	VolumeResource& volume = volumes_[index];
	volume.description = description;
	volume.proxyMesh = createStaticMesh(cube);
	try
	{
		createVolumeDescriptor(volume);
	}
	catch (...)
	{
		destroyMesh(volume.proxyMesh);
		freeVolumes_.push_back(index);
		throw;
	}
	volume.alive = true;
	return {index, volume.generation};
}

void VkApp::updateVolume(
	VolumeHandle handle,
	const VolumeRenderDescription& description)
{
	if (handle.index >= volumes_.size() || !volumes_[handle.index].alive ||
		volumes_[handle.index].generation != handle.generation)
	{
		throw std::invalid_argument("Volume handle is invalid or stale");
	}
	validateVolumeRenderDescription(description);
	if (description.volumeTexture.index >= textures3D_.size() ||
		!textures3D_[description.volumeTexture.index].alive ||
		textures3D_[description.volumeTexture.index].generation != description.volumeTexture.generation ||
		description.transferFunction.index >= transferFunctions_.size() ||
		!transferFunctions_[description.transferFunction.index].alive ||
		transferFunctions_[description.transferFunction.index].generation != description.transferFunction.generation)
	{
		throw std::invalid_argument("Updated volume references a stale texture handle");
	}
	VolumeResource& volume = volumes_[handle.index];
	volume.description = description;
	createVolumeDescriptor(volume);
}

void VkApp::drawVolume(VolumeHandle handle)
{
	// Preserve VOL-1 behavior for the simple overload: volumes follow legacy
	// transparent custom meshes. New code can explicitly request Volume.
	drawVolume(handle, {RenderLayer::Overlay, 0.0f});
}

void VkApp::drawVolume(VolumeHandle handle, const DrawOptions& options)
{
	if (handle.index >= volumes_.size() || !volumes_[handle.index].alive ||
		volumes_[handle.index].generation != handle.generation)
	{
		throw std::invalid_argument("Volume handle is invalid or stale");
	}
	validateDrawOptions(options);
	VolumeResource& volume = volumes_[handle.index];
	volume.visible = true;
	volume.drawOptions = options;
	volume.submissionIndex = nextDrawSubmissionIndex_++;
	rebuildOrderedDrawResources();
}

void VkApp::hideVolume(VolumeHandle handle)
{
	if (handle.index >= volumes_.size() || !volumes_[handle.index].alive ||
		volumes_[handle.index].generation != handle.generation)
	{
		throw std::invalid_argument("Volume handle is invalid or stale");
	}
	volumes_[handle.index].visible = false;
	rebuildOrderedDrawResources();
}

void VkApp::destroyVolume(VolumeHandle handle)
{
	if (handle.index >= volumes_.size() || !volumes_[handle.index].alive ||
		volumes_[handle.index].generation != handle.generation)
	{
		throw std::invalid_argument("Volume handle is invalid or stale");
	}
	VolumeResource& volume = volumes_[handle.index];
	if (volume.descriptor != VK_NULL_HANDLE)
	{
		vkFreeDescriptorSets(device_, textureDescriptorPool_, 1, &volume.descriptor);
		volume.descriptor = VK_NULL_HANDLE;
	}
	destroyMesh(volume.proxyMesh);
	volume.alive = false;
	volume.visible = false;
	++volume.generation;
	freeVolumes_.push_back(handle.index);
	rebuildOrderedDrawResources();
}

void VkApp::createVolumeDescriptor(VolumeResource& volume)
{
	if (volume.descriptor == VK_NULL_HANDLE)
	{
		VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
		allocation.descriptorPool = textureDescriptorPool_;
		allocation.descriptorSetCount = 1;
		allocation.pSetLayouts = &volumeSetLayout_;
		checkVk(vkAllocateDescriptorSets(device_, &allocation, &volume.descriptor),
			"Failed to allocate volume descriptor set");
	}
	const Texture3DResource& texture = textures3D_[volume.description.volumeTexture.index];
	const TransferFunctionResource& transfer =
		transferFunctions_[volume.description.transferFunction.index];
	std::array<VkDescriptorImageInfo, 2> imageInfos{};
	imageInfos[0] = {texture.sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	imageInfos[1] = {transfer.texture->sampler, transfer.texture->view,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	std::array<VkWriteDescriptorSet, 2> writes{};
	for (std::uint32_t index = 0; index < writes.size(); ++index)
	{
		writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[index].dstSet = volume.descriptor;
		writes[index].dstBinding = index;
		writes[index].descriptorCount = 1;
		writes[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[index].pImageInfo = &imageInfos[index];
	}
	vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()),
		writes.data(), 0, nullptr);
}

void VkApp::copyBufferToImage3D(
	VkBuffer buffer,
	VkImage image,
	std::uint32_t width,
	std::uint32_t height,
	std::uint32_t depth)
{
	VkCommandBuffer commandBuffer = beginSingleTimeCommands();
	VkBufferImageCopy region{};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = {width, height, depth};
	vkCmdCopyBufferToImage(commandBuffer, buffer, image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	endSingleTimeCommands(commandBuffer);
}

VkPipeline VkApp::createMaterialPipeline(
	const MaterialDescription& description,
	VkPipelineLayout layout)
{
	std::string vertexName = "custom_mesh.vert.spv";
	std::string fragmentName = description.shaderProgram.selection == ShaderSelection::VertexColorUnlit
		? "custom_mesh_unlit.frag.spv" : "custom_mesh.frag.spv";
	if (description.shaderProgram.selection == ShaderSelection::Custom)
	{
		vertexName = description.shaderProgram.vertexShader.string();
		fragmentName = description.shaderProgram.fragmentShader.string();
	}
	const std::vector<char> vertexCode = readFile(
		description.shaderProgram.selection == ShaderSelection::Custom
			? vertexName : findShaderPath(vertexName));
	const std::vector<char> fragmentCode = readFile(
		description.shaderProgram.selection == ShaderSelection::Custom
			? fragmentName : findShaderPath(fragmentName));
	auto module = [&](const std::vector<char>& code)
	{
		VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
		info.codeSize = code.size();
		info.pCode = reinterpret_cast<const std::uint32_t*>(code.data());
		VkShaderModule result = VK_NULL_HANDLE;
		checkVk(vkCreateShaderModule(device_, &info, nullptr, &result),
			"Failed to create custom material shader module");
		return result;
	};
	const VkShaderModule vertexModule = module(vertexCode);
	const VkShaderModule fragmentModule = module(fragmentCode);
	std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
	stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vertexModule;
	stages[0].pName = "main";
	stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fragmentModule;
	stages[1].pName = "main";
	VkVertexInputBindingDescription binding{0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX};
	std::array<VkVertexInputAttributeDescription, 4> attributes{{
		{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position)},
		{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal)},
		{2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MeshVertex, color)},
		{3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv)}}};
	VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &binding;
	vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
	vertexInput.pVertexAttributeDescriptions = attributes.data();
	VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;
	VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
	raster.polygonMode = description.polygonMode == PolygonMode::Line
		? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	raster.cullMode = cullMode(description.cullMode);
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
	depth.depthTestEnable = description.depthTestEnabled;
	depth.depthWriteEnable = description.depthWriteEnabled;
	depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	VkPipelineColorBlendAttachmentState blend{};
	blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blend.blendEnable = description.alphaBlendingEnabled;
	blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend.colorBlendOp = VK_BLEND_OP_ADD;
	blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend.alphaBlendOp = VK_BLEND_OP_ADD;
	VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
	colorBlend.attachmentCount = 1;
	colorBlend.pAttachments = &blend;
	std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
	dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
	dynamic.pDynamicStates = dynamicStates.data();
	VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
	info.stageCount = static_cast<std::uint32_t>(stages.size());
	info.pStages = stages.data();
	info.pVertexInputState = &vertexInput;
	info.pInputAssemblyState = &inputAssembly;
	info.pViewportState = &viewport;
	info.pRasterizationState = &raster;
	info.pMultisampleState = &multisample;
	info.pDepthStencilState = &depth;
	info.pColorBlendState = &colorBlend;
	info.pDynamicState = &dynamic;
	info.layout = layout;
	info.renderPass = renderPass_;
	VkPipeline pipeline = VK_NULL_HANDLE;
	const VkResult result = vkCreateGraphicsPipelines(
		device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
	vkDestroyShaderModule(device_, fragmentModule, nullptr);
	vkDestroyShaderModule(device_, vertexModule, nullptr);
	checkVk(result, "Failed to create custom material graphics pipeline");
	return pipeline;
}

void VkApp::createVolumePipeline()
{
	if (volumePipeline_ != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(device_, volumePipeline_, nullptr);
		volumePipeline_ = VK_NULL_HANDLE;
	}
	if (volumePipelineLayout_ != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(device_, volumePipelineLayout_, nullptr);
		volumePipelineLayout_ = VK_NULL_HANDLE;
	}
	VkPushConstantRange range{};
	range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	range.size = sizeof(VolumePushConstants);
	std::array<VkDescriptorSetLayout, 2> layouts{descriptorSetLayout_, volumeSetLayout_};
	VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	layoutInfo.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
	layoutInfo.pSetLayouts = layouts.data();
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &range;
	checkVk(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &volumePipelineLayout_),
		"Failed to create volume pipeline layout");
	MaterialDescription description;
	description.alphaBlendingEnabled = true;
	description.depthTestEnabled = true;
	description.depthWriteEnabled = false;
	description.cullMode = CullMode::Back;
	description.shaderProgram.selection = ShaderSelection::Custom;
	description.shaderProgram.vertexShader = findShaderPath("volume_raymarch.vert.spv");
	description.shaderProgram.fragmentShader = findShaderPath("volume_raymarch.frag.spv");
	volumePipeline_ = createMaterialPipeline(description, volumePipelineLayout_);
}

void VkApp::rebuildCustomPipelines()
{
	for (MaterialResource& material : materials_)
	{
		if (!material.alive)
		{
			continue;
		}
		if (material.pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, material.pipeline, nullptr);
		}
		material.pipeline = createMaterialPipeline(material.description, material.layout);
	}
}

void VkApp::drawCustomMeshRequest(
	VkCommandBuffer commandBuffer,
	std::uint32_t imageIndex,
	const MeshDrawRequest& request)
{
	if (request.mesh.index >= customMeshes_.size() ||
		request.material.index >= materials_.size())
	{
		return;
	}
	const CustomMeshResource& mesh = customMeshes_[request.mesh.index];
	const MaterialResource& material = materials_[request.material.index];
	if (!mesh.alive || !material.alive || mesh.indexCount == 0)
	{
		return;
	}
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, material.pipeline);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		material.layout, 0, 1, &descriptorSets_[imageIndex], 0, nullptr);
	const glm::mat4 model = request.transform.matrix();
	vkCmdPushConstants(commandBuffer, material.layout, VK_SHADER_STAGE_VERTEX_BIT,
		0, sizeof(model), &model);
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offset);
	vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
}

void VkApp::drawVolumeResource(
	VkCommandBuffer commandBuffer,
	std::uint32_t imageIndex,
	const VolumeResource& volume)
{
	if (volumePipeline_ == VK_NULL_HANDLE || !volume.alive || !volume.visible ||
		volume.proxyMesh.index >= customMeshes_.size())
	{
		return;
	}
		const CustomMeshResource& mesh = customMeshes_[volume.proxyMesh.index];
		const VolumeRenderDescription& description = volume.description;
		VolumePushConstants push;
		push.cameraWorld = glm::vec4(camera_.position, 1.0f);
		push.volumeMinimum = glm::vec4(description.volumeMin, 0.0f);
		push.volumeMaximum = glm::vec4(description.volumeMax, 0.0f);
		const glm::vec3 planeNormal = description.clipping.clipPlaneEnabled
			? glm::normalize(description.clipping.clipPlaneNormal) : glm::vec3(0.0f);
		push.clipPlane = glm::vec4(planeNormal, description.clipping.clipPlaneOffset);
		push.clipBoxMinimum = glm::vec4(description.clipping.clipBoxMinimum, 0.0f);
		push.clipBoxMaximum = glm::vec4(description.clipping.clipBoxMaximum, 0.0f);
		push.settings = glm::vec4(description.densityScale, description.opacityScale,
			static_cast<float>(description.raymarchSteps), description.referenceStepLength);
		const int flags = (description.enableJitter ? 1 : 0) |
			(description.enableEarlyTermination ? 2 : 0) |
			(description.clipping.clipPlaneEnabled ? 4 : 0) |
			(description.clipping.clipBoxEnabled ? 8 : 0) |
			(description.opacityModel == VolumeOpacityModel::ExponentialExtinction ? 16 : 0) |
			(description.normalizeOpacityByStepLength ? 32 : 0);
		push.flags = glm::ivec4(flags, 0, 0, 0);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, volumePipeline_);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			volumePipelineLayout_, 0, 1, &descriptorSets_[imageIndex], 0, nullptr);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			volumePipelineLayout_, 1, 1, &volume.descriptor, 0, nullptr);
		vkCmdPushConstants(commandBuffer, volumePipelineLayout_,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(push), &push);
		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offset);
		vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
	}

void VkApp::rebuildOrderedDrawResources()
{
	struct Candidate
	{
		OrderedDrawResource resource;
		DrawOrderEntry order;
	};
	std::vector<Candidate> candidates;
	candidates.reserve(meshDrawRequests_.size() + volumes_.size());
	for (std::size_t index = 0; index < meshDrawRequests_.size(); ++index)
	{
		const MeshDrawRequest& request = meshDrawRequests_[index];
		candidates.push_back({{OrderedDrawKind::Mesh, index},
			{request.drawOptions.layer, request.drawOptions.sortKey,
				request.submissionIndex}});
	}
	for (std::size_t index = 0; index < volumes_.size(); ++index)
	{
		const VolumeResource& volume = volumes_[index];
		if (volume.alive && volume.visible)
		{
			candidates.push_back({{OrderedDrawKind::Volume, index},
				{volume.drawOptions.layer, volume.drawOptions.sortKey,
					volume.submissionIndex}});
		}
	}
	std::stable_sort(candidates.begin(), candidates.end(),
		[](const Candidate& left, const Candidate& right)
		{
			if (left.order.layer != right.order.layer)
			{
				return renderLayerOrdinal(left.order.layer) <
					renderLayerOrdinal(right.order.layer);
			}
			if (left.order.sortKey != right.order.sortKey)
			{
				return left.order.sortKey < right.order.sortKey;
			}
			return left.order.submissionIndex < right.order.submissionIndex;
		});
	orderedDrawResources_.clear();
	orderedDrawResources_.reserve(candidates.size());
	for (const Candidate& candidate : candidates)
	{
		orderedDrawResources_.push_back(candidate.resource);
	}
}

void VkApp::drawOrderedCustomResources(
	VkCommandBuffer commandBuffer,
	std::uint32_t imageIndex)
{
	for (const OrderedDrawResource& resource : orderedDrawResources_)
	{
		if (resource.kind == OrderedDrawKind::Mesh &&
			resource.index < meshDrawRequests_.size())
		{
			drawCustomMeshRequest(
				commandBuffer, imageIndex, meshDrawRequests_[resource.index]);
		}
		else if (resource.kind == OrderedDrawKind::Volume &&
			resource.index < volumes_.size())
		{
			drawVolumeResource(commandBuffer, imageIndex, volumes_[resource.index]);
		}
	}
}

void VkApp::destroyCustomResources()
{
	for (VolumeResource& volume : volumes_)
	{
		if (volume.descriptor != VK_NULL_HANDLE && textureDescriptorPool_ != VK_NULL_HANDLE)
		{
			vkFreeDescriptorSets(device_, textureDescriptorPool_, 1, &volume.descriptor);
		}
		volume.descriptor = VK_NULL_HANDLE;
	}
	volumes_.clear();
	freeVolumes_.clear();
	for (TransferFunctionResource& transfer : transferFunctions_)
	{
		if (transfer.texture)
		{
			destroyTexture(*transfer.texture);
		}
	}
	transferFunctions_.clear();
	freeTransferFunctions_.clear();
	for (Texture3DResource& texture : textures3D_)
	{
		if (texture.sampler != VK_NULL_HANDLE) vkDestroySampler(device_, texture.sampler, nullptr);
		if (texture.view != VK_NULL_HANDLE) vkDestroyImageView(device_, texture.view, nullptr);
		if (texture.image != VK_NULL_HANDLE) vkDestroyImage(device_, texture.image, nullptr);
		if (texture.memory != VK_NULL_HANDLE) vkFreeMemory(device_, texture.memory, nullptr);
	}
	textures3D_.clear();
	freeTextures3D_.clear();
	for (CustomMeshResource& mesh : customMeshes_)
	{
		destroyBuffer(device_, mesh.vertexBuffer);
		destroyBuffer(device_, mesh.indexBuffer);
	}
	customMeshes_.clear();
	freeCustomMeshes_.clear();
	meshDrawRequests_.clear();
	orderedDrawResources_.clear();
	for (MaterialResource& material : materials_)
	{
		if (material.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device_, material.pipeline, nullptr);
		if (material.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, material.layout, nullptr);
	}
	materials_.clear();
	freeMaterials_.clear();
}

}
