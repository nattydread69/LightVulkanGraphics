// SPDX-License-Identifier: LGPL-3.0-or-later

#include "VkApp.h"

#include <array>
#include <exception>
#include <iostream>

int main()
{
	try
	{
		lightGraphics::VkApp app;
		app.init(64, 64, "LVG volume integration test");
		lightGraphics::Texture3DDescription description;
		description.width = 2;
		description.height = 2;
		description.depth = 2;
		std::array<float, 8> voxels{};
		const auto texture = app.createTexture3D(
			description, voxels.data(), sizeof(voxels));
		const auto transfer = app.createTransferFunction(
			lightGraphics::TransferFunctionPreset::SoftNeutralFog);
		lightGraphics::VolumeRenderDescription volumeDescription;
		volumeDescription.volumeTexture = texture;
		volumeDescription.transferFunction = transfer;
		const auto volume = app.createVolume(volumeDescription);
		app.drawVolume(volume);
		app.finalizeScene();
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
