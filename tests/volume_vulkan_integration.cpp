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
