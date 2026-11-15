// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Light Vulkan Graphics (3.4): 2D textures, textured/splat materials and the compute API on a real device.

#include "VkApp.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

namespace
{
int failures = 0;
void expect(bool ok, const char* what)
{
	if (!ok)
	{
		std::fprintf(stderr, "FAIL: %s\n", what);
		++failures;
	}
}
}

int main()
{
	try
	{
		lightGraphics::VkApp app;
		app.init(64, 64, "LVG material texture and compute test");

		// ---- compute: out = in * 2 + 1, two dispatches chained through a barrier (second doubles the first result again)
		const std::uint32_t count = 1000;
		std::vector<float> input(count);
		for (std::uint32_t i = 0; i < count; ++i) input[i] = static_cast<float>(i);
		const auto a = app.createGpuBuffer(count * sizeof(float));
		const auto b = app.createGpuBuffer(count * sizeof(float));
		const auto c = app.createGpuBuffer(count * sizeof(float));
		app.uploadGpuBuffer(a, input.data(), count * sizeof(float));
		lightGraphics::ComputeKernelDescription kernelDescription;
		kernelDescription.spirv = LVG_TEST_COMPUTE_SPV;
		kernelDescription.storageBufferCount = 2;
		kernelDescription.pushConstantBytes = 12;
		const auto kernel = app.createComputeKernel(kernelDescription);
		struct Push { float scale; float offset; std::uint32_t count; };
		const Push first {2.0f, 1.0f, count};
		const Push second {3.0f, 0.0f, count};
		app.runCompute([&](lightGraphics::ComputeRecorder& r)
		{
			r.dispatch(kernel, {a, b}, &first, sizeof first, (count + 63) / 64);
			r.dispatch(kernel, {b, c}, &second, sizeof second, (count + 63) / 64);
		});
		std::vector<float> result(count);
		app.downloadGpuBuffer(c, result.data(), count * sizeof(float));
		bool exact = true;
		for (std::uint32_t i = 0; i < count; ++i)
		{
			exact = exact && result[i] == (input[i] * 2.0f + 1.0f) * 3.0f;
		}
		expect(exact, "chained compute dispatches produce the expected values");
		bool threw = false;
		try { app.uploadGpuBuffer(a, input.data(), count * sizeof(float) + 4); } catch (const std::invalid_argument&) { threw = true; }
		expect(threw, "an out-of-range upload is rejected");
		app.destroyComputeKernel(kernel);
		app.destroyGpuBuffer(a);
		app.destroyGpuBuffer(b);
		app.destroyGpuBuffer(c);

		// ---- textures and materials
		lightGraphics::Texture2DDescription description;
		description.width = 4;
		description.height = 4;
		description.format = lightGraphics::Texture2DFormat::RGBA8_UNORM;
		std::vector<std::uint8_t> pixels(4 * 4 * 4, 200);
		const auto weights = app.createTexture2D(description, pixels.data(), pixels.size());
		const auto layer = app.createTexture2D(description, pixels.data(), pixels.size());
		expect(weights.isValid() && layer.isValid(), "textures are created");
		app.updateTexture2D(layer, pixels.data(), pixels.size());
		lightGraphics::MaterialDescription textured;
		textured.shaderProgram.selection = lightGraphics::ShaderSelection::Textured;
		textured.textures[0] = layer;
		const auto texturedMaterial = app.createMaterial(textured);
		lightGraphics::MaterialDescription splat;
		splat.shaderProgram.selection = lightGraphics::ShaderSelection::TerrainSplat;
		splat.textures = {weights, weights, layer, layer, layer, layer, layer, layer, layer, layer};
		splat.parameters = {4.0f, 4.0f, 8.0f, 8.0f};
		const auto splatMaterial = app.createMaterial(splat);
		expect(texturedMaterial.isValid() && splatMaterial.isValid(), "textured and splat materials are created");
		bool missing = false;
		try { lightGraphics::MaterialDescription bad; bad.shaderProgram.selection = lightGraphics::ShaderSelection::Textured; (void)app.createMaterial(bad); }
		catch (const std::invalid_argument&) { missing = true; }
		expect(missing, "a textured material without a texture is rejected");

		lightGraphics::MeshData quad;
		quad.vertices = {{{-1, 0, -1}, {0, 1, 0}, {1, 1, 1, 1}, {0, 0}}, {{1, 0, -1}, {0, 1, 0}, {1, 1, 1, 1}, {1, 0}},
			{{1, 0, 1}, {0, 1, 0}, {1, 1, 1, 1}, {1, 1}}, {{-1, 0, 1}, {0, 1, 0}, {1, 1, 1, 1}, {0, 1}}};
		quad.indices = {0, 2, 1, 0, 3, 2};
		const auto mesh = app.createStaticMesh(quad);
		app.drawMesh(mesh, lightGraphics::Transform {}, texturedMaterial);
		app.drawMesh(mesh, lightGraphics::Transform {}, splatMaterial);
		app.finalizeScene();
		// Frames draw both materials without a validation or device error; the window closes itself after a few.
		int frames = 0;
		app.setUpdateCallback([&](float)
		{
			if (++frames == 5)
			{
				glfwSetWindowShouldClose(app.getWindowPointer(), GLFW_TRUE);
			}
		});
		app.run();
		expect(frames >= 5, "five frames rendered with the textured materials");

		app.destroyMaterial(texturedMaterial);
		app.destroyMaterial(splatMaterial);
		app.destroyTexture2D(weights);
		app.destroyTexture2D(layer);
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "exception: %s\n", error.what());
		return 1;
	}
	std::puts(failures == 0 ? "material_texture_compute_integration passed" : "material_texture_compute_integration FAILED");
	return failures == 0 ? 0 : 1;
}
