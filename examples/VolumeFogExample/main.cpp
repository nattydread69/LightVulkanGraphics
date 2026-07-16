// SPDX-License-Identifier: LGPL-3.0-or-later

#include "VkApp.h"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <vector>

namespace
{

lightGraphics::MeshData makeTorus(float majorRadius, float minorRadius)
{
	constexpr std::uint32_t majorSegments = 96;
	constexpr std::uint32_t minorSegments = 16;
	lightGraphics::MeshData mesh;
	mesh.vertices.reserve(majorSegments * minorSegments);
	mesh.indices.reserve(majorSegments * minorSegments * 6);
	for (std::uint32_t major = 0; major < majorSegments; ++major)
	{
		const float u = glm::two_pi<float>() * static_cast<float>(major) /
			static_cast<float>(majorSegments);
		for (std::uint32_t minor = 0; minor < minorSegments; ++minor)
		{
			const float v = glm::two_pi<float>() * static_cast<float>(minor) /
				static_cast<float>(minorSegments);
			const glm::vec3 normal{
				std::cos(u) * std::cos(v),
				std::sin(v),
				std::sin(u) * std::cos(v)};
			lightGraphics::MeshVertex vertex;
			vertex.position = {
				(majorRadius + minorRadius * std::cos(v)) * std::cos(u),
				minorRadius * std::sin(v),
				(majorRadius + minorRadius * std::cos(v)) * std::sin(u)};
			vertex.normal = normal;
			vertex.color = {0.95f, 0.9f, 0.35f, 1.0f};
			vertex.uv = {static_cast<float>(major) / majorSegments,
				static_cast<float>(minor) / minorSegments};
			mesh.vertices.push_back(vertex);
		}
	}
	for (std::uint32_t major = 0; major < majorSegments; ++major)
	{
		for (std::uint32_t minor = 0; minor < minorSegments; ++minor)
		{
			const std::uint32_t nextMajor = (major + 1) % majorSegments;
			const std::uint32_t nextMinor = (minor + 1) % minorSegments;
			const std::uint32_t a = major * minorSegments + minor;
			const std::uint32_t b = nextMajor * minorSegments + minor;
			const std::uint32_t c = nextMajor * minorSegments + nextMinor;
			const std::uint32_t d = major * minorSegments + nextMinor;
			mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
		}
	}
	return mesh;
}

std::vector<float> makeToroidalGaussian(std::uint32_t resolution)
{
	std::vector<float> field(
		static_cast<std::size_t>(resolution) * resolution * resolution);
	for (std::uint32_t z = 0; z < resolution; ++z)
	{
		for (std::uint32_t y = 0; y < resolution; ++y)
		{
			for (std::uint32_t x = 0; x < resolution; ++x)
			{
				const glm::vec3 position = glm::vec3(x, y, z) /
					static_cast<float>(resolution - 1) * 2.0f - 1.0f;
				const float radial = std::sqrt(
					position.x * position.x + position.z * position.z);
				const float distanceSquared =
					(radial - 0.55f) * (radial - 0.55f) +
					position.y * position.y;
				const std::size_t index = x + resolution * (y + resolution * z);
				field[index] = std::exp(-distanceSquared / (2.0f * 0.13f * 0.13f));
			}
		}
	}
	return field;
}

}

int main()
{
	try
	{
		lightGraphics::VkApp app;
		app.init(1280, 720, "LightVulkanGraphics Volume Fog Example");
		app.setOrbitEnabled(true);
		app.setOrbitTarget({0.0f, 0.0f, 0.0f});
		app.setOrbitRadius(3.8f);
		app.setOrbitAngles(-45.0f, 24.0f);

		constexpr std::uint32_t resolution = 64;
		const std::vector<float> field = makeToroidalGaussian(resolution);
		lightGraphics::Texture3DDescription textureDescription;
		textureDescription.width = resolution;
		textureDescription.height = resolution;
		textureDescription.depth = resolution;
		textureDescription.format = lightGraphics::TextureFormat::R32_SFLOAT;
		const lightGraphics::Texture3DHandle texture = app.createTexture3D(
			textureDescription, field.data(), field.size() * sizeof(float));
		const lightGraphics::TransferFunctionHandle transfer = app.createTransferFunction(
			lightGraphics::TransferFunctionPreset::DensityDeficitCyan);
		lightGraphics::VolumeRenderDescription linearDescription;
		linearDescription.volumeTexture = texture;
		linearDescription.transferFunction = transfer;
		linearDescription.volumeMin = {-2.1f, -1.0f, -1.0f};
		linearDescription.volumeMax = {-0.1f, 1.0f, 1.0f};
		linearDescription.opacityModel = lightGraphics::VolumeOpacityModel::LinearAlpha;
		linearDescription.opacityScale = 0.018f;
		linearDescription.raymarchSteps = 64;
		linearDescription.enableJitter = true;
		const lightGraphics::VolumeHandle linearVolume =
			app.createVolume(linearDescription);
		app.drawVolume(linearVolume,
			{lightGraphics::RenderLayer::Volume, 0.0f});

		lightGraphics::VolumeRenderDescription exponentialDescription = linearDescription;
		exponentialDescription.volumeMin = {0.1f, -1.0f, -1.0f};
		exponentialDescription.volumeMax = {2.1f, 1.0f, 1.0f};
		exponentialDescription.opacityModel =
			lightGraphics::VolumeOpacityModel::ExponentialExtinction;
		exponentialDescription.opacityScale = 1.25f;
		exponentialDescription.raymarchSteps = 256;
		exponentialDescription.referenceStepLength = 1.0f;
		exponentialDescription.normalizeOpacityByStepLength = true;
		const lightGraphics::VolumeHandle exponentialVolume =
			app.createVolume(exponentialDescription);
		app.drawVolume(exponentialVolume,
			{lightGraphics::RenderLayer::Volume, 1.0f});

		const lightGraphics::MeshHandle ring = app.createStaticMesh(makeTorus(0.55f, 0.018f));
		lightGraphics::MaterialDescription ringMaterialDescription;
		ringMaterialDescription.shaderProgram.selection =
			lightGraphics::ShaderSelection::VertexColorUnlit;
		ringMaterialDescription.alphaBlendingEnabled = true;
		ringMaterialDescription.depthWriteEnabled = false;
		const lightGraphics::MaterialHandle ringMaterial =
			app.createMaterial(ringMaterialDescription);
		lightGraphics::Transform leftRingTransform;
		leftRingTransform.position = {-1.1f, 0.0f, 0.0f};
		app.drawMesh(ring, leftRingTransform, ringMaterial,
			{lightGraphics::RenderLayer::Transparent, 1.0f});
		lightGraphics::Transform rightRingTransform;
		rightRingTransform.position = {1.1f, 0.0f, 0.0f};
		app.drawMesh(ring, rightRingTransform, ringMaterial,
			{lightGraphics::RenderLayer::Transparent, 2.0f});

		float angle = -45.0f;
		app.setUpdateCallback([&app, &angle](float deltaTime)
		{
			angle += deltaTime * 8.0f;
			app.setOrbitAngles(angle, 24.0f);
		});
		app.finalizeScene();
		app.run();
	}
	catch (const std::exception& error)
	{
		std::cerr << "VolumeFogExample failed: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
