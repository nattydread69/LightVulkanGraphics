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

// Renders the same volume as one 3D texture and as bricks, in every render style, and
// requires the images to agree; then checks partial residency, eviction and detaching.
// Needs a Vulkan device and a display (lavapipe under Xvfb is enough).

#include "VkApp.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace lightGraphics;

namespace
{
	constexpr glm::uvec3 kVoxels{100, 90, 70};
	constexpr std::uint32_t kBrick = 32;

	// A soft shell with an off-centre hole and a ramp, so every brick has structure.
	std::uint16_t voxelValue(std::uint32_t x, std::uint32_t y, std::uint32_t z)
	{
		const float u = (x + 0.5f) / kVoxels.x * 2.0f - 1.0f;
		const float v = (y + 0.5f) / kVoxels.y * 2.0f - 1.0f;
		const float w = (z + 0.5f) / kVoxels.z * 2.0f - 1.0f;
		const float r = std::sqrt(u * u + v * v + w * w);
		float value = std::exp(-std::pow((r - 0.6f) / 0.18f, 2.0f));
		const float hole = std::sqrt((u - 0.35f) * (u - 0.35f) + (v + 0.2f) * (v + 0.2f) + w * w);
		if (hole < 0.25f) value *= 0.15f;
		value += 0.25f * (u * 0.5f + 0.5f) * std::exp(-4.0f * r * r);
		return static_cast<std::uint16_t>(std::clamp(value, 0.0f, 1.0f) * 65535.0f);
	}

	std::vector<std::uint16_t> wholeVolume()
	{
		std::vector<std::uint16_t> data(static_cast<std::size_t>(kVoxels.x) * kVoxels.y * kVoxels.z);
		for (std::uint32_t z = 0; z < kVoxels.z; ++z)
			for (std::uint32_t y = 0; y < kVoxels.y; ++y)
				for (std::uint32_t x = 0; x < kVoxels.x; ++x)
					data[(static_cast<std::size_t>(z) * kVoxels.y + y) * kVoxels.x + x] = voxelValue(x, y, z);
		return data;
	}

	// (kBrick + 2)^3 samples with a one-voxel border of neighbours, edges replicated.
	std::vector<std::uint16_t> paddedBrick(const std::vector<std::uint16_t>& volume, glm::uvec3 brick)
	{
		const std::uint32_t p = kBrick + 2;
		std::vector<std::uint16_t> out(static_cast<std::size_t>(p) * p * p);
		for (std::uint32_t z = 0; z < p; ++z)
			for (std::uint32_t y = 0; y < p; ++y)
				for (std::uint32_t x = 0; x < p; ++x)
				{
					const auto at = [&](std::uint32_t b, std::uint32_t i, std::uint32_t limit) {
						const std::int64_t v = static_cast<std::int64_t>(b) * kBrick + i - 1;
						return static_cast<std::uint32_t>(std::clamp<std::int64_t>(v, 0, limit - 1));
					};
					const std::uint32_t sx = at(brick.x, x, kVoxels.x);
					const std::uint32_t sy = at(brick.y, y, kVoxels.y);
					const std::uint32_t sz = at(brick.z, z, kVoxels.z);
					out[(static_cast<std::size_t>(z) * p + y) * p + x] =
						volume[(static_cast<std::size_t>(sz) * kVoxels.y + sy) * kVoxels.x + sx];
				}
		return out;
	}

	void require(bool condition, const std::string& message)
	{
		if (!condition) throw std::runtime_error(message);
	}

	struct Difference { double mean = 0.0; int maximum = 0; double fractionOver = 0.0; };

	Difference compare(const CapturedFrame& a, const CapturedFrame& b, int tolerance)
	{
		require(a.ok && b.ok && a.width == b.width && a.height == b.height,
			"captures must match in size (a: " + std::to_string(a.ok) + " " + a.error + " " + std::to_string(a.width) + "x" + std::to_string(a.height) +
			", b: " + std::to_string(b.ok) + " " + b.error + " " + std::to_string(b.width) + "x" + std::to_string(b.height) + ")");
		Difference d;
		std::size_t over = 0;
		for (std::size_t i = 0; i < a.rgba.size(); ++i)
		{
			const int diff = std::abs(static_cast<int>(a.rgba[i]) - static_cast<int>(b.rgba[i]));
			d.mean += diff;
			d.maximum = std::max(d.maximum, diff);
			over += diff > tolerance ? 1 : 0;
		}
		d.mean /= static_cast<double>(a.rgba.size());
		d.fractionOver = static_cast<double>(over) / static_cast<double>(a.rgba.size());
		return d;
	}

	double meanBrightness(const CapturedFrame& f)
	{
		double sum = 0.0;
		for (std::size_t i = 0; i < f.rgba.size(); i += 4) sum += f.rgba[i] + f.rgba[i + 1] + f.rgba[i + 2];
		return sum / (static_cast<double>(f.rgba.size() / 4) * 3.0);
	}
}

int main()
{
	try
	{
		VkApp app;
		app.init(160, 160, "LVG bricked volume integration test");
		app.setCameraLookAt({1.3f, 0.9f, 3.0f}, {0.0f, 0.0f, 0.0f});

		const std::vector<std::uint16_t> data = wholeVolume();
		Texture3DDescription plainDescription;
		plainDescription.width = kVoxels.x;
		plainDescription.height = kVoxels.y;
		plainDescription.depth = kVoxels.z;
		plainDescription.format = TextureFormat::R16_UNORM;
		const Texture3DHandle plainTexture = app.createTexture3D(
			plainDescription, data.data(), data.size() * sizeof(std::uint16_t));

		BrickedTextureDescription brickDescription;
		brickDescription.voxels = kVoxels;
		brickDescription.brickSize = kBrick;
		brickDescription.format = TextureFormat::R16_UNORM;
		const glm::uvec3 grid = brickGridSize(brickDescription);
		brickDescription.atlasBricks = grid; // everything fits
		const BrickedTextureHandle bricked = app.createBrickedTexture(brickDescription);
		require(app.brickCapacity(bricked) == static_cast<std::size_t>(grid.x) * grid.y * grid.z, "capacity");

		std::vector<std::vector<std::uint16_t>> storage;
		std::vector<BrickUpload> uploads;
		const std::size_t brickBytes = static_cast<std::size_t>(kBrick + 2) * (kBrick + 2) * (kBrick + 2) * 2;
		for (std::uint32_t z = 0; z < grid.z; ++z)
			for (std::uint32_t y = 0; y < grid.y; ++y)
				for (std::uint32_t x = 0; x < grid.x; ++x)
					storage.push_back(paddedBrick(data, {x, y, z}));
		std::size_t n = 0;
		for (std::uint32_t z = 0; z < grid.z; ++z)
			for (std::uint32_t y = 0; y < grid.y; ++y)
				for (std::uint32_t x = 0; x < grid.x; ++x)
					uploads.push_back({{x, y, z}, storage[n++].data(), brickBytes});
		require(app.uploadBricks(bricked, uploads) == uploads.size(), "all bricks should be placed");
		require(app.residentBrickCount(bricked) == uploads.size(), "resident count");

		const TransferFunctionHandle transfer = app.createTransferFunction(TransferFunctionPreset::SoftNeutralFog);
		VolumeRenderDescription plainVolume;
		plainVolume.volumeTexture = plainTexture;
		plainVolume.transferFunction = transfer;
		plainVolume.raymarchSteps = 160;
		plainVolume.opacityScale = 2.0f;
		VolumeRenderDescription brickVolume = plainVolume;
		brickVolume.volumeTexture = app.brickedAtlas(bricked);
		const VolumeHandle plain = app.createVolume(plainVolume);
		const VolumeHandle brick = app.createVolume(brickVolume);
		app.setVolumeBricks(brick, bricked);

		// Misuse is rejected.
		bool threw = false;
		try { app.destroyTexture3D(app.brickedAtlas(bricked)); } catch (const std::logic_error&) { threw = true; }
		require(threw, "the atlas cannot be destroyed behind the bricked texture's back");
		threw = false;
		try { app.destroyBrickedTexture(bricked); } catch (const std::logic_error&) { threw = true; }
		require(threw, "a bricked texture in use cannot be destroyed");

		enum class Step { Empty, Plain, Bricked, Partial, Gone, Detached, Compare, Finished };
		struct Scene { VolumeRenderStyle style; bool shading; };
		const std::vector<Scene> scenes{
			{VolumeRenderStyle::Composite, false}, {VolumeRenderStyle::Composite, true},
			{VolumeRenderStyle::MaximumIntensity, false}, {VolumeRenderStyle::MinimumIntensity, false},
			{VolumeRenderStyle::Average, false}, {VolumeRenderStyle::Isosurface, false}};

		std::size_t scene = 0;
		Step step = Step::Empty;
		int wait = 0;
		bool failed = false;
		std::string failure;
		CapturedFrame empty, plainFrame, brickFrame;
		bool haveFrame = false;
		auto capture = [&](CapturedFrame& into) {
			haveFrame = false;
			app.requestFrameCapture([&](const CapturedFrame& f) { into = f; haveFrame = true; });
		};
		auto showOnly = [&](VolumeHandle which) {
			app.hideVolume(plain);
			app.hideVolume(brick);
			app.drawVolume(which, {RenderLayer::Volume, 0.0f});
		};
		auto styleFor = [&](const Scene& s) {
			VolumeStyleDescription style;
			style.style = s.style;
			style.shading = s.shading;
			style.isoValue = 0.35f;
			return style;
		};

		app.setUpdateCallback([&](float)
		{
			try
			{
				if (wait > 0) { --wait; return; }
				if (step != Step::Finished && step != Step::Empty && !haveFrame) { return; }
				switch (step)
				{
				case Step::Empty:
					app.hideVolume(plain);
					app.hideVolume(brick);
					capture(empty);
					wait = 2;
					step = Step::Plain;
					scene = 0;
					break;
				case Step::Plain:
					if (!haveFrame) return;
					app.setVolumeStyle(plain, styleFor(scenes[scene]));
					app.setVolumeStyle(brick, styleFor(scenes[scene]));
					showOnly(plain);
					capture(plainFrame);
					wait = 2;
					step = Step::Bricked;
					break;
				case Step::Bricked:
				{
					showOnly(brick);
					capture(brickFrame);
					wait = 2;
					step = Step::Partial;
					break;
				}
				case Step::Partial:
				{
					const Difference d = compare(plainFrame, brickFrame, 4);
					std::cout << "style " << volumeRenderStyleName(scenes[scene].style)
						<< (scenes[scene].shading ? "+shading" : "") << ": mean |diff| " << d.mean
						<< ", max " << d.maximum << ", >4 levels " << d.fractionOver * 100.0 << "%, brightness "
						<< meanBrightness(plainFrame) << " vs empty " << meanBrightness(empty) << "\n";
					// MinIP of this mostly-empty volume is zero along every ray: nothing to see.
					if (scenes[scene].style != VolumeRenderStyle::MinimumIntensity)
					{
						require(std::abs(meanBrightness(plainFrame) - meanBrightness(empty)) > 0.5,
							"the plain volume should be visible");
					}
					require(d.mean < 1.0 && d.fractionOver < 0.01,
						"bricked and plain renders must agree (style " +
						std::string(volumeRenderStyleName(scenes[scene].style)) + ")");
					if (++scene < scenes.size())
					{
						step = Step::Plain;
						break;
					}
					// Drop half the bricks: the image must change but stay a valid render.
					std::vector<glm::uvec3> half;
					for (std::uint32_t x = 0; x < grid.x; x += 2)
						for (std::uint32_t y = 0; y < grid.y; ++y)
							for (std::uint32_t z = 0; z < grid.z; ++z) half.push_back({x, y, z});
					app.evictBricks(bricked, half);
					require(!app.isBrickResident(bricked, {0, 0, 0}) && app.isBrickResident(bricked, {1, 0, 0}),
						"eviction updates residency");
					app.setVolumeStyle(brick, styleFor(scenes[0]));
					showOnly(brick);
					capture(brickFrame);
					wait = 2;
					step = Step::Gone;
					break;
				}
				case Step::Gone:
				{
					app.setVolumeStyle(plain, styleFor(scenes[0]));
					const Difference d = compare(plainFrame, brickFrame, 4);
					require(d.mean > 0.5, "removing half of the bricks must change the picture");
					// Everything out: identical to the empty frame.
					app.evictAllBricks(bricked);
					require(app.residentBrickCount(bricked) == 0, "evictAll empties the pool");
					showOnly(brick);
					capture(plainFrame);
					wait = 2;
					step = Step::Detached;
					break;
				}
				case Step::Detached:
				{
					// The same view with nothing drawn (captured now: the window may have been
					// resized since the first frame).
					app.hideVolume(plain);
					app.hideVolume(brick);
					capture(empty);
					wait = 2;
					step = Step::Compare;
					break;
				}
				case Step::Compare:
				{
					const Difference d = compare(empty, plainFrame, 1);
					require(d.maximum <= 1, "a bricked volume with no resident bricks must draw nothing");
					// Detached, the volume samples its atlas directly (garbage, but it must not crash).
					app.setVolumeBricks(brick, BrickedTextureHandle{});
					showOnly(brick);
					capture(brickFrame);
					wait = 2;
					step = Step::Finished;
					break;
				}
				case Step::Finished:
					if (!haveFrame) return;
					glfwSetWindowShouldClose(app.getWindowPointer(), GLFW_TRUE);
					break;
				}
			}
			catch (const std::exception& error)
			{
				failed = true;
				failure = error.what();
				glfwSetWindowShouldClose(app.getWindowPointer(), GLFW_TRUE);
			}
		});
		app.finalizeScene();
		app.run();
		if (failed)
		{
			std::cerr << failure << '\n';
			return 1;
		}
		require(step == Step::Finished, "the test must run to completion");
		std::cout << "Bricked volume integration test passed\n";
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
