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

#include "FBXLoader.h"
#include "RiggedObject.h"
#include "RotationGlyph.h"
#include "SceneGraph.h"
#include "VolumeRendering.h"
#include "VkApp.h"
#include "pObject.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
	bool nearlyEqual(float lhs, float rhs, float tolerance = 1.0e-4f)
	{
		return std::fabs(lhs - rhs) <= tolerance;
	}

	bool nearlyEqual(const glm::vec3& lhs, const glm::vec3& rhs, float tolerance = 1.0e-4f)
	{
		return nearlyEqual(lhs.x, rhs.x, tolerance) &&
		       nearlyEqual(lhs.y, rhs.y, tolerance) &&
		       nearlyEqual(lhs.z, rhs.z, tolerance);
	}

	bool nearlyEqual(const glm::vec4& lhs, const glm::vec4& rhs, float tolerance = 1.0e-4f)
	{
		return nearlyEqual(lhs.x, rhs.x, tolerance) &&
		       nearlyEqual(lhs.y, rhs.y, tolerance) &&
		       nearlyEqual(lhs.z, rhs.z, tolerance) &&
		       nearlyEqual(lhs.w, rhs.w, tolerance);
	}

	bool nearlyEqual(const glm::mat4& lhs, const glm::mat4& rhs, float tolerance = 1.0e-4f)
	{
		for (int column = 0; column < 4; ++column)
		{
			for (int row = 0; row < 4; ++row)
			{
				if (!nearlyEqual(lhs[column][row], rhs[column][row], tolerance))
				{
					return false;
				}
			}
		}
		return true;
	}

	void require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	template <typename Function>
	void requireOutOfRange(Function&& function, const std::string& message)
	{
		try
		{
			function();
		}
		catch (const std::out_of_range&)
		{
			return;
		}
		catch (const std::exception& error)
		{
			throw std::runtime_error(message + ": expected std::out_of_range, got " + error.what());
		}

		throw std::runtime_error(message + ": expected std::out_of_range");
	}

	template <typename Exception, typename Function>
	void requireThrows(Function&& function, const std::string& message)
	{
		try
		{
			function();
		}
		catch (const Exception&)
		{
			return;
		}
		catch (const std::exception& error)
		{
			throw std::runtime_error(message + ": unexpected exception: " + error.what());
		}
		throw std::runtime_error(message + ": expected exception was not thrown");
	}

	void testTransformIdentity()
	{
		const lightGraphics::Transform transform;
		require(nearlyEqual(transform.matrix(), glm::mat4(1.0f)),
		        "default Transform should produce identity matrix");
	}

	void testTransformRoundTrip()
	{
		lightGraphics::Transform transform;
		transform.position = glm::vec3(1.25f, -2.0f, 3.5f);
		transform.rotation = glm::angleAxis(glm::radians(35.0f),
		                                    glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f)));
		transform.scale = glm::vec3(2.0f, 0.5f, 3.0f);

		const glm::mat4 matrix = transform.matrix();
		const lightGraphics::Transform decomposed = lightGraphics::Transform::fromMatrix(matrix);
		require(nearlyEqual(decomposed.matrix(), matrix),
		        "Transform::fromMatrix should round-trip translation, rotation, and scale");
	}

	void testSceneNodeHandleBasics()
	{
		const lightGraphics::SceneNodeHandle invalid;
		require(!invalid.isValid(), "default SceneNodeHandle should be invalid");

		const lightGraphics::SceneNodeHandle first{2, 7};
		const lightGraphics::SceneNodeHandle same{2, 7};
		const lightGraphics::SceneNodeHandle differentGeneration{2, 8};

		require(first.isValid(), "non-empty SceneNodeHandle should be valid");
		require(first == same, "matching SceneNodeHandle values should compare equal");
		require(first != differentGeneration,
		        "SceneNodeHandle generation should participate in comparisons");
	}

	void testPObjectProperties()
	{
		const glm::quat initialRotation =
		    glm::angleAxis(glm::radians(10.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		lightGraphics::pObject object(lightGraphics::ShapeType::CUBE,
		                              glm::vec3(1.0f, 2.0f, 3.0f),
		                              glm::vec3(4.0f, 5.0f, 6.0f),
		                              glm::vec4(0.1f, 0.2f, 0.3f, 0.4f),
		                              initialRotation,
		                              "Test Cube",
		                              12.0f);

		require(object._type == lightGraphics::ShapeType::CUBE, "pObject should preserve shape type");
		require(nearlyEqual(object.getPosition(), glm::vec3(1.0f, 2.0f, 3.0f)),
		        "pObject should preserve initial position");
		require(nearlyEqual(object.getSize(), glm::vec3(4.0f, 5.0f, 6.0f)),
		        "pObject should preserve initial size");
		require(nearlyEqual(object.getColour(), glm::vec4(0.1f, 0.2f, 0.3f, 0.4f)),
		        "pObject should preserve initial color");
		require(object.getName() == "Test Cube", "pObject should preserve name");
		require(nearlyEqual(object.getMass(), 12.0f), "pObject should preserve mass");
		require(!object.isImmovable(), "pObject should default to movable");

		object.setPosition(glm::vec3(-1.0f, -2.0f, -3.0f));
		object.setSize(glm::vec3(0.5f, 0.75f, 1.25f));
		object.setColour(glm::vec4(0.9f, 0.8f, 0.7f, 0.6f));
		object.setMass(3.0f);
		object.setTexturePath("diffuse.png");
		object.setImmovable();

		require(nearlyEqual(object.getPosition(), glm::vec3(-1.0f, -2.0f, -3.0f)),
		        "pObject setPosition should update position");
		require(nearlyEqual(object.getSize(), glm::vec3(0.5f, 0.75f, 1.25f)),
		        "pObject setSize should update size");
		require(nearlyEqual(object.getColour(), glm::vec4(0.9f, 0.8f, 0.7f, 0.6f)),
		        "pObject setColour should update color");
		require(nearlyEqual(object.getMass(), 3.0f), "pObject setMass should update mass");
		require(object.getTexturePath() == "diffuse.png",
		        "pObject setTexturePath should update texture path");
		require(object.isImmovable(), "pObject setImmovable should mark object immovable");
	}

	lightGraphics::pObject makeCubeObject(const std::string& name = "Cube")
	{
		return lightGraphics::pObject(lightGraphics::ShapeType::CUBE,
		                              glm::vec3(0.0f),
		                              glm::vec3(1.0f),
		                              glm::vec4(1.0f),
		                              glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
		                              name,
		                              1.0f);
	}

	void testVkAppObjectIndexValidation()
	{
		lightGraphics::VkApp app;
		const lightGraphics::pObject cube = makeCubeObject();
		app.addObject(cube);
		require(app.getObjectCount() == 1, "VkApp addObject should add one object");

		app.updateObject(0, makeCubeObject("Updated Cube"));
		app.updateObjectPositions({{0, glm::vec3(2.0f, 0.0f, 0.0f)}});
		app.updateObjectProperties({
		    {0, glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(2.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f)}
		});
		app.removeObject(0);
		require(app.getObjectCount() == 0, "VkApp removeObject should remove the object");

		requireOutOfRange([&app]() { app.removeObject(0); },
		                  "VkApp removeObject should reject invalid indices");
		requireOutOfRange([&app, &cube]() { app.updateObject(0, cube); },
		                  "VkApp updateObject should reject invalid indices");
		requireOutOfRange([&app]() { app.updateObjectPositions({{0, glm::vec3(0.0f)}}); },
		                  "VkApp updateObjectPositions should reject invalid indices");
		requireOutOfRange(
		    [&app]() {
			    app.updateObjectProperties({
			        {0, glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f)}
			    });
		    },
		    "VkApp updateObjectProperties should reject invalid indices");

		app.addObject(cube);
		requireOutOfRange(
		    [&app]() {
			    app.updateObjectPositions({
			        {0, glm::vec3(9.0f, 0.0f, 0.0f)},
			        {1, glm::vec3(0.0f)}
			    });
		    },
		    "VkApp updateObjectPositions should validate a full batch before applying it");
		require(nearlyEqual(app.getObject(0).getPosition(), glm::vec3(0.0f)),
		        "VkApp updateObjectPositions should not partially apply invalid batches");

		requireOutOfRange(
		    [&app]() {
			    app.updateObjectProperties({
			        {0, glm::vec3(9.0f, 0.0f, 0.0f), glm::vec3(2.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f)},
			        {1, glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f)}
			    });
		    },
		    "VkApp updateObjectProperties should validate a full batch before applying it");
		require(nearlyEqual(app.getObject(0).getPosition(), glm::vec3(0.0f)),
		        "VkApp updateObjectProperties should not partially apply invalid batches");
		require(nearlyEqual(app.getObject(0).getSize(), glm::vec3(1.0f)),
		        "VkApp updateObjectProperties should leave size untouched for invalid batches");
	}

	void testVkAppObjectDescription()
	{
		lightGraphics::VkApp app;

		lightGraphics::ObjectDescription description;
		description.type = lightGraphics::ShapeType::CAPSULE;
		description.position = glm::vec3(1.0f, 2.0f, 3.0f);
		description.size = glm::vec3(0.5f, 1.5f, 0.5f);
		description.color = glm::vec4(0.25f, 0.5f, 0.75f, 1.0f);
		description.rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		description.name = "DescribedCapsule";
		description.mass = 2.5f;
		description.immovable = true;
		description.texturePath = "capsule.png";

		const lightGraphics::ObjectHandle handle = app.addObject(description);
		require(app.isObjectHandleValid(handle),
		        "ObjectDescription: addObject(description) should return a valid handle");
		require(app.getObjectCount() == 1,
		        "ObjectDescription: addObject(description) should add exactly one object");

		const size_t index = app.resolveObjectHandle(handle);
		const lightGraphics::ObjectDescription roundTrip = app.getObjectDescription(index);
		require(roundTrip.type == description.type,
		        "ObjectDescription: getObjectDescription should preserve type");
		require(nearlyEqual(roundTrip.position, description.position),
		        "ObjectDescription: getObjectDescription should preserve position");
		require(nearlyEqual(roundTrip.size, description.size),
		        "ObjectDescription: getObjectDescription should preserve size");
		require(nearlyEqual(roundTrip.color, description.color),
		        "ObjectDescription: getObjectDescription should preserve color");
		require(roundTrip.name == description.name,
		        "ObjectDescription: getObjectDescription should preserve name");
		require(nearlyEqual(roundTrip.mass, description.mass),
		        "ObjectDescription: getObjectDescription should preserve mass");
		require(roundTrip.immovable == description.immovable,
		        "ObjectDescription: getObjectDescription should preserve immovable");
		require(roundTrip.texturePath == description.texturePath,
		        "ObjectDescription: getObjectDescription should preserve texturePath");

		lightGraphics::ObjectDescription updated = description;
		updated.position = glm::vec3(9.0f, 9.0f, 9.0f);
		updated.name = "UpdatedCapsule";
		app.updateObject(index, updated);
		require(nearlyEqual(app.getObjectDescription(index).position, updated.position),
		        "ObjectDescription: updateObject(description) should apply the new position");
		require(app.getObjectDescription(index).name == "UpdatedCapsule",
		        "ObjectDescription: updateObject(description) should apply the new name");

		requireOutOfRange([&app]() { app.getObjectDescription(app.getObjectCount()); },
		                  "ObjectDescription: getObjectDescription should reject invalid indices");
	}

	void testVkAppObjectHandleValidity()
	{
		lightGraphics::VkApp app;
		const lightGraphics::ObjectHandle handleA = app.addObject(makeCubeObject("A"));
		const lightGraphics::ObjectHandle handleB = app.addObject(makeCubeObject("B"));
		const lightGraphics::ObjectHandle handleC = app.addObject(makeCubeObject("C"));

		require(app.isObjectHandleValid(handleA) && app.isObjectHandleValid(handleB) &&
		            app.isObjectHandleValid(handleC),
		        "ObjectHandle: freshly added handles should be valid");
		require(app.resolveObjectHandle(handleA) == 0 &&
		            app.resolveObjectHandle(handleB) == 1 &&
		            app.resolveObjectHandle(handleC) == 2,
		        "ObjectHandle: handles should resolve to their insertion order initially");

		// Removing an earlier object shifts the dense storage; the surviving
		// handles must keep tracking their own objects rather than becoming
		// stale or aliasing a shifted-in neighbor.
		app.removeObject(0);
		require(!app.isObjectHandleValid(handleA),
		        "ObjectHandle: handle to a removed object should become invalid");
		require(app.isObjectHandleValid(handleB) && app.resolveObjectHandle(handleB) == 0,
		        "ObjectHandle: unrelated handle should follow its object after a shift");
		require(app.isObjectHandleValid(handleC) && app.resolveObjectHandle(handleC) == 1,
		        "ObjectHandle: unrelated handle should follow its object after a shift");

		requireOutOfRange([&app, handleA]() { app.resolveObjectHandle(handleA); },
		                  "ObjectHandle: resolving a stale handle should throw");
		requireOutOfRange([&app, handleA]() { app.removeObject(handleA); },
		                  "ObjectHandle: removing via a stale handle should throw");

		app.removeObject(handleB);
		require(app.getObjectCount() == 1, "ObjectHandle: removeObject(handle) should remove the object");
		require(app.isObjectHandleValid(handleC) && app.resolveObjectHandle(handleC) == 0,
		        "ObjectHandle: surviving handle should track its object through repeated shifts");

		const lightGraphics::ObjectHandle handleD = app.addObject(makeCubeObject("D"));
		require(app.isObjectHandleValid(handleD), "ObjectHandle: newly added handle should be valid");
		require(!app.isObjectHandleValid(handleA) && !app.isObjectHandleValid(handleB),
		        "ObjectHandle: reused slots must not resurrect old handles");

		const lightGraphics::ObjectHandle handleAtZero = app.objectHandleAt(0);
		require(app.resolveObjectHandle(handleAtZero) == app.resolveObjectHandle(handleC),
		        "ObjectHandle: objectHandleAt should resolve to the same object as the surviving handle");
	}

	void testVkAppLightHandleValidity()
	{
		lightGraphics::VkApp app;
		app.clearLights(); // VkApp seeds a default light; start from a clean slate
		lightGraphics::LightSource light;
		light.color = glm::vec3(1.0f);
		light.intensity = 1.0f;

		const lightGraphics::LightHandle handleA = app.addLightHandle(light);
		const lightGraphics::LightHandle handleB = app.addLightHandle(light);
		const lightGraphics::LightHandle handleC = app.addLightHandle(light);

		require(app.isLightHandleValid(handleA) && app.isLightHandleValid(handleB) &&
		            app.isLightHandleValid(handleC),
		        "LightHandle: freshly added handles should be valid");

		app.removeLight(0);
		require(!app.isLightHandleValid(handleA),
		        "LightHandle: handle to a removed light should become invalid");
		require(app.isLightHandleValid(handleB) && app.resolveLightHandle(handleB) == 0,
		        "LightHandle: unrelated handle should follow its light after a shift");
		require(app.isLightHandleValid(handleC) && app.resolveLightHandle(handleC) == 1,
		        "LightHandle: unrelated handle should follow its light after a shift");

		requireOutOfRange([&app, handleA]() { app.resolveLightHandle(handleA); },
		                  "LightHandle: resolving a stale handle should throw");
		requireOutOfRange([&app, handleA]() { app.removeLight(handleA); },
		                  "LightHandle: removing via a stale handle should throw");

		app.removeLight(handleB);
		require(app.getLightCount() == 1, "LightHandle: removeLight(handle) should remove the light");
		require(app.isLightHandleValid(handleC) && app.resolveLightHandle(handleC) == 0,
		        "LightHandle: surviving handle should track its light through repeated shifts");

		const lightGraphics::LightHandle handleD = app.addLightHandle(light);
		require(app.isLightHandleValid(handleD), "LightHandle: newly added handle should be valid");
		require(!app.isLightHandleValid(handleA) && !app.isLightHandleValid(handleB),
		        "LightHandle: reused slots must not resurrect old handles");
	}

	void testVkAppHeadlessRiggedObject()
	{
		lightGraphics::VkApp app;
		require(!app.isDeviceInitialized(),
		        "VkApp should report no device before init() is called");

#ifdef LVG_SOURCE_DIR
		const std::filesystem::path modelPath =
		    std::filesystem::path(LVG_SOURCE_DIR) / "assets" / "Worker.fbx";
#else
		const std::filesystem::path modelPath =
		    std::filesystem::path("assets") / "Worker.fbx";
#endif

		lightGraphics::FBXLoader loader;
		auto model = loader.loadModel(modelPath.string());
		require(model != nullptr, "failed to load Worker.fbx: " + loader.getLastError());

		auto riggedObject = std::make_shared<lightGraphics::RiggedObject>(
		    glm::vec3(0.0f),
		    glm::vec3(1.0f),
		    glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
		    "HeadlessWorker",
		    1.0f,
		    model);

		// The whole point: registering a rigged object (mesh validation,
		// skinning-mode selection, handle bookkeeping) must not require a
		// GLFW window or Vulkan device.
		const lightGraphics::RiggedObjectHandle handle = app.addRiggedObjectHandle(riggedObject);
		require(app.isRiggedObjectHandleValid(handle),
		        "addRiggedObjectHandle should succeed without a Vulkan device");
		require(app.getRiggedObjectCount() == 1,
		        "addRiggedObjectHandle should register the rigged object without a device");
		require(app.resolveRiggedObjectHandle(handle) == 0,
		        "RiggedObjectHandle should resolve to its insertion index");

		app.removeRiggedObject(handle);
		require(app.getRiggedObjectCount() == 0,
		        "removeRiggedObject(handle) should work without a device");
		require(!app.isRiggedObjectHandleValid(handle),
		        "RiggedObjectHandle should become invalid after removal");
	}

	lightGraphics::MeshData makeTriangle()
	{
		lightGraphics::MeshData mesh;
		mesh.vertices.resize(3);
		mesh.vertices[0].position = {0.0f, 0.0f, 0.0f};
		mesh.vertices[1].position = {1.0f, 0.0f, 0.0f};
		mesh.vertices[2].position = {0.0f, 1.0f, 0.0f};
		mesh.indices = {0, 1, 2};
		return mesh;
	}

	void testMeshValidation()
	{
		const lightGraphics::MeshData triangle = makeTriangle();
		lightGraphics::validateMeshData(triangle);
		lightGraphics::validateDynamicMeshCapacity(32, 96);
		lightGraphics::validateDynamicMeshUpdate(3, 3, triangle);
		auto invalidIndex = triangle;
		invalidIndex.indices[2] = 3;
		requireThrows<std::out_of_range>(
			[&invalidIndex]() { lightGraphics::validateMeshData(invalidIndex); },
			"mesh validation should reject out-of-range indices");
		requireThrows<std::length_error>(
			[&triangle]() { lightGraphics::validateDynamicMeshUpdate(2, 3, triangle); },
			"dynamic mesh validation should enforce vertex capacity");
	}

	void testScreenTextMesh()
	{
		lightGraphics::ScreenTextDescription description;
		description.text = "Kamae\nready";
		description.positionPixels = {20.0f, 30.0f};
		description.scale = 2.0f;
		description.color = {1.0f, 0.8f, 0.2f, 0.75f};
		description.maximumCharacters = 32;
		lightGraphics::validateScreenTextDescription(description);

		const lightGraphics::MeshData mesh =
			lightGraphics::buildScreenTextMesh(description, 800, 600);
		lightGraphics::validateMeshData(mesh);
		require(mesh.vertices.size() % 4 == 0 &&
			mesh.indices.size() % 6 == 0,
			"screen text should contain indexed quads");
		// The shadow pass (enabled by default) is emitted first so the real
		// glyphs composite on top of it -- see buildScreenTextMesh -- so the
		// first vertex belongs to the shadow copy and the last belongs to
		// the real-colored pass.
		require(nearlyEqual(mesh.vertices.front().color, description.shadowColor),
			"screen text shadow pass should use its shadow color");
		require(nearlyEqual(mesh.vertices.back().color, description.color),
			"screen text should preserve its requested color");
		for (const lightGraphics::MeshVertex& vertex : mesh.vertices)
		{
			require(vertex.position.x >= -1.0f &&
				vertex.position.x <= 1.0f &&
				vertex.position.y >= -1.0f &&
				vertex.position.y <= 1.0f,
				"on-screen text vertices should use normalized device coordinates");
		}

		description.visible = false;
		const lightGraphics::MeshData hidden =
			lightGraphics::buildScreenTextMesh(description, 800, 600);
		require(hidden.indices.size() == 3 &&
			hidden.vertices.front().color.a == 0.0f,
			"hidden screen text should produce an invisible placeholder");

		description.visible = true;
		description.maximumCharacters = 2;
		requireThrows<std::length_error>(
			[&description]()
			{
				lightGraphics::validateScreenTextDescription(description);
			},
			"screen text should enforce its declared character capacity");
		requireThrows<std::invalid_argument>(
			[]()
			{
				lightGraphics::ScreenTextDescription invalid;
				invalid.text = "text";
				(void)lightGraphics::buildScreenTextMesh(invalid, 0, 600);
			},
			"screen text should reject zero framebuffer dimensions");
	}

	void testTexture3DValidation()
	{
		lightGraphics::Texture3DDescription description;
		description.width = 4;
		description.height = 8;
		description.depth = 16;
		description.format = lightGraphics::TextureFormat::R32_SFLOAT;
		require(lightGraphics::texture3DByteSize(description) == 4u * 8u * 16u * 4u,
			"R32 Texture3D byte size should include all voxels");
		description.format = lightGraphics::TextureFormat::R8_UNORM;
		require(lightGraphics::texture3DByteSize(description) == 4u * 8u * 16u,
			"R8 Texture3D byte size should use one byte per voxel");
		description.width = 0;
		requireThrows<std::invalid_argument>(
			[&description]() { lightGraphics::validateTexture3DDescription(description); },
			"Texture3D validation should reject zero dimensions");
	}

	void testTransferFunctionSampling()
	{
		const std::vector<lightGraphics::TransferFunctionPoint> points{
			{1.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
			{0.0f, {0.0f, 0.0f, 0.0f, 0.0f}}};
		const auto sorted = lightGraphics::normalizeTransferFunctionPoints(points);
		require(sorted.front().scalar == 0.0f && sorted.back().scalar == 1.0f,
			"transfer points should be sorted by scalar");
		const auto pixels = lightGraphics::sampleTransferFunctionRgba8(points, 3);
		require(pixels.size() == 12 && pixels[4] == 128 && pixels[7] == 128,
			"transfer sampling should linearly interpolate color and opacity");
		for (int preset = 0; preset < 4; ++preset)
		{
			require(lightGraphics::transferFunctionPreset(
				static_cast<lightGraphics::TransferFunctionPreset>(preset)).size() >= 2,
				"every transfer-function preset should contain a usable ramp");
		}
	}

	void testMaterialClippingAndVolumeValidation()
	{
		lightGraphics::MaterialDescription material;
		material.alphaBlendingEnabled = true;
		material.depthTestEnabled = true;
		material.depthWriteEnabled = false;
		material.cullMode = lightGraphics::CullMode::Back;
		lightGraphics::validateMaterialDescription(material);
		require(material.alphaBlendingEnabled && !material.depthWriteEnabled,
			"transparent material state should preserve independent depth-write control");

		lightGraphics::ClippingDescription clipping;
		clipping.clipPlaneEnabled = true;
		clipping.clipPlaneNormal = {0.0f, 1.0f, 0.0f};
		clipping.clipBoxEnabled = true;
		clipping.clipBoxMinimum = {0.1f, 0.2f, 0.3f};
		clipping.clipBoxMaximum = {0.9f, 0.8f, 0.7f};
		lightGraphics::validateClippingDescription(clipping);

		lightGraphics::VolumeRenderDescription volume;
		volume.volumeTexture = {0, 1};
		volume.transferFunction = {0, 1};
		volume.clipping = clipping;
		lightGraphics::validateVolumeRenderDescription(volume);
		require(volume.opacityModel ==
			lightGraphics::VolumeOpacityModel::ExponentialExtinction,
			"new volumes should default to exponential extinction");
		require(volume.normalizeOpacityByStepLength,
			"new volumes should normalize opacity by physical step length");
		volume.referenceStepLength = 0.0f;
		requireThrows<std::invalid_argument>(
			[&volume]() { lightGraphics::validateVolumeRenderDescription(volume); },
			"volume validation should reject a zero reference step length");
		volume.referenceStepLength = 1.0f;
		volume.raymarchSteps = 1;
		requireThrows<std::invalid_argument>(
			[&volume]() { lightGraphics::validateVolumeRenderDescription(volume); },
			"volume validation should reject unusable raymarch step counts");
	}

	void testVolumeOpacityAndRenderOrdering()
	{
		require(std::string(lightGraphics::volumeOpacityModelName(
			lightGraphics::VolumeOpacityModel::LinearAlpha)) == "linearAlpha",
			"linear opacity model should have a stable public name");
		require(lightGraphics::parseVolumeOpacityModel("Exponential Extinction") ==
			lightGraphics::VolumeOpacityModel::ExponentialExtinction,
			"opacity model parser should ignore case and separators");
		require(lightGraphics::parseRenderLayer("transparent") ==
			lightGraphics::RenderLayer::Transparent,
			"render layer parser should map public names");

		std::vector<lightGraphics::DrawOrderEntry> entries{
			{lightGraphics::RenderLayer::Transparent, 2.0f, 4},
			{lightGraphics::RenderLayer::Volume, 10.0f, 3},
			{lightGraphics::RenderLayer::Volume, -2.0f, 2},
			{lightGraphics::RenderLayer::Volume, -2.0f, 1},
			{lightGraphics::RenderLayer::Opaque, 8.0f, 9}};
		entries = lightGraphics::sortDrawOrder(std::move(entries));
		require(entries[0].layer == lightGraphics::RenderLayer::Opaque,
			"render layers should sort before their sort keys");
		require(entries[1].sortKey == -2.0f && entries[1].submissionIndex == 1 &&
			entries[2].sortKey == -2.0f && entries[2].submissionIndex == 2 &&
			entries[3].sortKey == 10.0f,
			"multiple volumes should sort by key then submission index");
		require(entries.back().layer == lightGraphics::RenderLayer::Transparent,
			"transparent layer should follow the volume layer");
		requireThrows<std::invalid_argument>(
			[]()
			{
				lightGraphics::validateDrawOptions(
					{lightGraphics::RenderLayer::Volume,
						std::numeric_limits<float>::infinity()});
			},
			"draw options should reject non-finite sort keys");
	}

	void testVolumeTwoReportsAndDocumentation()
	{
		const std::filesystem::path sourceDirectory = LVG_SOURCE_DIR;
		for (const std::filesystem::path& relativePath : {
			std::filesystem::path("docs/frame_capture.md"),
			std::filesystem::path("docs/volume_rendering.md"),
			std::filesystem::path("runs/lvg_vol_2_feature_status.csv"),
			std::filesystem::path("runs/lvg_vol_2_opacity_model_audit.csv"),
			std::filesystem::path("runs/lvg_vol_2_render_order_audit.csv"),
			std::filesystem::path("runs/lvg_vol_2_frame_capture_status.csv"),
			std::filesystem::path("runs/lvg_vol_2_known_limitations.csv")})
		{
			require(std::filesystem::is_regular_file(sourceDirectory / relativePath),
				"LVG-VOL-2 documentation/report is missing: " +
				relativePath.string());
		}
	}

	// FBXLoader::loadModel's documented contract is "return nullptr + set lastError
	// on failure, never throw" (see its own doc comment in FBXLoader.cpp, above the
	// try/catch wrapping most of its body). These three cover the
	// negative paths: a nonexistent file, a file that isn't FBX/any known format at
	// all, and a real FBX file truncated mid-stream. All three currently fail via
	// Assimp's own import-failure early return rather than the try/catch added around
	// the rest of loadModel, but the observable contract under test — never throw,
	// always leave a populated lastError — is the same either way.
	void testFBXLoaderRejectsMissingFile()
	{
		lightGraphics::FBXLoader loader;
		auto model = loader.loadModel("/nonexistent/path/does_not_exist.fbx");
		require(model == nullptr, "loadModel should return nullptr for a missing file");
		require(!loader.getLastError().empty(),
		        "loadModel should set lastError for a missing file");
	}

	void testFBXLoaderRejectsMalformedFile()
	{
		const std::filesystem::path scratchPath =
		    std::filesystem::temp_directory_path() / "lvg_malformed_test.fbx";
		{
			std::ofstream out(scratchPath, std::ios::binary);
			out << "this is not an FBX file, just garbage bytes 0123456789";
		}

		lightGraphics::FBXLoader loader;
		auto model = loader.loadModel(scratchPath.string());
		require(model == nullptr, "loadModel should return nullptr for a malformed file");
		require(!loader.getLastError().empty(),
		        "loadModel should set lastError for a malformed file");

		std::filesystem::remove(scratchPath);
	}

	void testFBXLoaderRejectsTruncatedFile()
	{
#ifdef LVG_SOURCE_DIR
		const std::filesystem::path modelPath =
		    std::filesystem::path(LVG_SOURCE_DIR) / "assets" / "Worker.fbx";
#else
		const std::filesystem::path modelPath =
		    std::filesystem::path("assets") / "Worker.fbx";
#endif
		const std::filesystem::path scratchPath =
		    std::filesystem::temp_directory_path() / "lvg_truncated_test.fbx";
		{
			std::ifstream in(modelPath, std::ios::binary);
			std::vector<char> prefix(512);
			in.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
			const std::streamsize bytesRead = in.gcount();

			std::ofstream out(scratchPath, std::ios::binary);
			out.write(prefix.data(), bytesRead);
		}

		lightGraphics::FBXLoader loader;
		auto model = loader.loadModel(scratchPath.string());
		require(model == nullptr, "loadModel should return nullptr for a truncated file");
		require(!loader.getLastError().empty(),
		        "loadModel should set lastError for a truncated file");

		std::filesystem::remove(scratchPath);
	}

	float polarAngle(const glm::vec3& position)
	{
		return std::atan2(position.y, position.x);
	}

	float radialDistance(const glm::vec3& position)
	{
		return std::sqrt(position.x * position.x + position.y * position.y);
	}

	// Wraps `to - from` into (-pi, pi].
	float signedAngleDelta(float from, float to)
	{
		float delta = std::fmod(to - from, glm::two_pi<float>());
		if (delta > glm::pi<float>())
		{
			delta -= glm::two_pi<float>();
		}
		if (delta <= -glm::pi<float>())
		{
			delta += glm::two_pi<float>();
		}
		return delta;
	}

	void testRotationGlyphDefaultMesh()
	{
		lightGraphics::RotationRingDescription description;
		lightGraphics::validateRotationRingDescription(description);

		const lightGraphics::MeshData mesh = lightGraphics::buildRotationRingMesh(description);
		require(!mesh.vertices.empty(), "rotation ring mesh should be non-empty");
		lightGraphics::validateMeshData(mesh);
		require(mesh.indices.size() % 3 == 0,
		        "rotation ring indices should form complete triangles");

		bool sawPositiveZ = false;
		bool sawNegativeZ = false;
		for (const lightGraphics::MeshVertex& vertex : mesh.vertices)
		{
			require(std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) &&
			            std::isfinite(vertex.position.z),
			        "rotation ring vertex position should be finite");
			require(std::isfinite(vertex.normal.x) && std::isfinite(vertex.normal.y) &&
			            std::isfinite(vertex.normal.z),
			        "rotation ring vertex normal should be finite");
			require(std::isfinite(vertex.color.r) && std::isfinite(vertex.color.g) &&
			            std::isfinite(vertex.color.b) && std::isfinite(vertex.color.a),
			        "rotation ring vertex color should be finite");
			require(std::isfinite(vertex.uv.x) && std::isfinite(vertex.uv.y),
			        "rotation ring vertex uv should be finite");

			require(nearlyEqual(glm::length(vertex.normal), 1.0f, 1.0e-3f),
			        "rotation ring vertex normal should have unit length");
			require(std::fabs(vertex.position.z) <= description.thickness * 0.5f + 1.0e-4f,
			        "rotation ring vertices should stay within +/- thickness/2");

			if (nearlyEqual(vertex.position.z, description.thickness * 0.5f, 1.0e-3f))
			{
				sawPositiveZ = true;
			}
			if (nearlyEqual(vertex.position.z, -description.thickness * 0.5f, 1.0e-3f))
			{
				sawNegativeZ = true;
			}
		}
		require(sawPositiveZ && sawNegativeZ,
		        "rotation ring mesh should have both a front (+Z) and back (-Z) surface");

		for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
		{
			const glm::vec3& a = mesh.vertices[mesh.indices[i]].position;
			const glm::vec3& b = mesh.vertices[mesh.indices[i + 1]].position;
			const glm::vec3& c = mesh.vertices[mesh.indices[i + 2]].position;
			const float area = glm::length(glm::cross(b - a, c - a)) * 0.5f;
			require(area > 1.0e-8f, "rotation ring mesh should not contain zero-area triangles");
		}
	}

	void testRotationGlyphRadialDimensions()
	{
		lightGraphics::RotationRingDescription description;
		description.radius = 2.0f;
		description.bandWidth = 0.3f;
		description.arrowHeadWidthScale = 2.5f;
		lightGraphics::validateRotationRingDescription(description);

		const lightGraphics::MeshData mesh = lightGraphics::buildRotationRingMesh(description);

		const float innerRadius = description.radius - 0.5f * description.bandWidth;
		const float outerRadius = description.radius + 0.5f * description.bandWidth;
		const float headWidth = description.bandWidth * description.arrowHeadWidthScale;
		const float headInnerRadius = description.radius - 0.5f * headWidth;
		const float headOuterRadius = description.radius + 0.5f * headWidth;

		float minRadius = std::numeric_limits<float>::infinity();
		float maxRadius = 0.0f;
		bool sawBodyInner = false;
		bool sawBodyOuter = false;
		for (const lightGraphics::MeshVertex& vertex : mesh.vertices)
		{
			const float radius = radialDistance(vertex.position);
			minRadius = std::min(minRadius, radius);
			maxRadius = std::max(maxRadius, radius);
			if (nearlyEqual(radius, innerRadius, 1.0e-3f))
			{
				sawBodyInner = true;
			}
			if (nearlyEqual(radius, outerRadius, 1.0e-3f))
			{
				sawBodyOuter = true;
			}
		}

		require(sawBodyInner, "rotation ring body should reach its inner radius");
		require(sawBodyOuter, "rotation ring body should reach its outer radius");
		require(nearlyEqual(minRadius, headInnerRadius, 1.0e-2f),
		        "rotation ring global minimum radius should be the arrowhead's inner radius");
		require(nearlyEqual(maxRadius, headOuterRadius, 1.0e-2f),
		        "rotation ring global maximum radius should be the arrowhead's outer radius");

		const float bodyWidth = outerRadius - innerRadius;
		const float measuredHeadWidth = maxRadius - minRadius;
		require(measuredHeadWidth > bodyWidth,
		        "arrowhead width should exceed body width when arrowHeadWidthScale > 1");
	}

	void testRotationGlyphDirection()
	{
		lightGraphics::RotationRingDescription description;
		description.gapCentreAngleRadians = 0.0f;

		description.sense = lightGraphics::RotationSense::CounterClockwise;
		const lightGraphics::MeshData ccwMesh = lightGraphics::buildRotationRingMesh(description);
		description.sense = lightGraphics::RotationSense::Clockwise;
		const lightGraphics::MeshData cwMesh = lightGraphics::buildRotationRingMesh(description);

		// The tip is the unique point (front+back pair) where inner and outer
		// radius have converged back to description.radius exactly -- every
		// other vertex sits at a strictly different radius (innerRadius,
		// outerRadius, or somewhere on the flare/taper ramp).
		auto findTipAngle = [&](const lightGraphics::MeshData& mesh) -> float
		{
			float bestDelta = std::numeric_limits<float>::infinity();
			float bestAngle = 0.0f;
			for (const lightGraphics::MeshVertex& vertex : mesh.vertices)
			{
				const float delta = std::fabs(radialDistance(vertex.position) - description.radius);
				if (delta < bestDelta)
				{
					bestDelta = delta;
					bestAngle = polarAngle(vertex.position);
				}
			}
			require(bestDelta < 1.0e-3f,
			        "rotation ring mesh should contain a vertex at the tip radius");
			return bestAngle;
		};

		const float ccwTipAngle = findTipAngle(ccwMesh);
		const float cwTipAngle = findTipAngle(cwMesh);

		const float ccwOffset = signedAngleDelta(description.gapCentreAngleRadians, ccwTipAngle);
		const float cwOffset = signedAngleDelta(description.gapCentreAngleRadians, cwTipAngle);

		require(ccwOffset < 0.0f,
		        "counterclockwise arrow tip should land on the negative-angle side of the gap");
		require(cwOffset > 0.0f,
		        "clockwise arrow tip should land on the positive-angle side of the gap");
		require(nearlyEqual(std::fabs(ccwOffset), description.gapAngleRadians * 0.5f, 0.05f),
		        "counterclockwise tip should sit just outside the gap's edge");
		require(nearlyEqual(std::fabs(cwOffset), description.gapAngleRadians * 0.5f, 0.05f),
		        "clockwise tip should sit just outside the gap's edge");
	}

	void testRotationGlyphTransform()
	{
		using lightGraphics::makeRotationRingTransform;

		{
			const glm::vec3 centre(1.0f, 2.0f, 3.0f);
			const glm::vec3 normal(0.0f, 0.0f, 1.0f);
			const glm::vec3 reference(1.0f, 0.0f, 0.0f);
			const lightGraphics::Transform transform =
			    makeRotationRingTransform(centre, normal, reference);
			require(nearlyEqual(transform.position, centre),
			        "transform should store the requested centre");

			const glm::mat4 matrix = transform.matrix();
			const glm::vec3 worldZ = glm::vec3(matrix * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
			const glm::vec3 worldX = glm::vec3(matrix * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
			require(nearlyEqual(glm::normalize(worldZ), normal, 1.0e-3f),
			        "local +Z should align with planeNormal");
			require(nearlyEqual(glm::normalize(worldX), reference, 1.0e-3f),
			        "local +X should align with the projected in-plane reference");
		}

		{
			// Oblique plane, non-axis-aligned reference.
			const glm::vec3 normal = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
			const glm::vec3 reference(0.0f, 1.0f, 0.0f);
			const lightGraphics::Transform transform =
			    makeRotationRingTransform(glm::vec3(0.0f), normal, reference);
			const glm::mat4 matrix = transform.matrix();
			const glm::vec3 worldZ =
			    glm::normalize(glm::vec3(matrix * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
			const glm::vec3 worldX =
			    glm::normalize(glm::vec3(matrix * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
			require(nearlyEqual(worldZ, normal, 1.0e-3f),
			        "oblique plane: local +Z should align with planeNormal");
			require(nearlyEqual(glm::dot(worldX, normal), 0.0f, 1.0e-3f),
			        "oblique plane: local +X should stay in the plane");
			const glm::vec3 expectedX =
			    glm::normalize(reference - glm::dot(reference, normal) * normal);
			require(nearlyEqual(worldX, expectedX, 1.0e-3f),
			        "oblique plane: local +X should align with the projected reference");
		}

		{
			// Reference (near-)parallel to the normal: deterministic fallback axis.
			const glm::vec3 normal(0.0f, 1.0f, 0.0f);
			const lightGraphics::Transform transform = makeRotationRingTransform(
			    glm::vec3(0.0f), normal, glm::vec3(0.0f, 5.0f, 0.0f));
			const glm::mat4 matrix = transform.matrix();
			const glm::vec3 worldZ =
			    glm::normalize(glm::vec3(matrix * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
			const glm::vec3 worldX =
			    glm::normalize(glm::vec3(matrix * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
			require(nearlyEqual(worldZ, normal, 1.0e-3f),
			        "parallel reference: local +Z should still align with planeNormal");
			require(nearlyEqual(glm::dot(worldX, normal), 0.0f, 1.0e-3f),
			        "parallel reference: fallback local +X should stay in the plane");
			require(nearlyEqual(glm::length(worldX), 1.0f, 1.0e-3f),
			        "parallel reference: fallback local +X should be a valid unit direction");

			const lightGraphics::Transform transformAgain = makeRotationRingTransform(
			    glm::vec3(0.0f), normal, glm::vec3(0.0f, 5.0f, 0.0f));
			require(nearlyEqual(transform.rotation.x, transformAgain.rotation.x) &&
			            nearlyEqual(transform.rotation.y, transformAgain.rotation.y) &&
			            nearlyEqual(transform.rotation.z, transformAgain.rotation.z) &&
			            nearlyEqual(transform.rotation.w, transformAgain.rotation.w),
			        "the fallback axis choice should be deterministic");
		}

		{
			// Zero in-plane reference: also falls back, and must not throw.
			const lightGraphics::Transform transform = makeRotationRingTransform(
			    glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f));
			require(nearlyEqual(glm::length(transform.rotation), 1.0f, 1.0e-3f),
			        "zero in-plane reference should still produce a valid rotation");
		}

		requireThrows<std::invalid_argument>(
		    []() {
			    (void)makeRotationRingTransform(
			        glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		    },
		    "makeRotationRingTransform should reject a zero-length planeNormal");
		requireThrows<std::invalid_argument>(
		    []() {
			    (void)makeRotationRingTransform(
			        glm::vec3(0.0f),
			        glm::vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f),
			        glm::vec3(1.0f, 0.0f, 0.0f));
		    },
		    "makeRotationRingTransform should reject a non-finite planeNormal");
	}

	void testRotationGlyphInvalidDescriptions()
	{
		auto base = []() { return lightGraphics::RotationRingDescription{}; };

		{
			auto d = base();
			d.radius = 0.0f;
			requireThrows<std::invalid_argument>(
			    [&d]() { lightGraphics::validateRotationRingDescription(d); },
			    "rotation ring should reject zero radius");
		}
		{
			auto d = base();
			d.bandWidth = d.radius * 3.0f;
			requireThrows<std::invalid_argument>(
			    [&d]() { lightGraphics::validateRotationRingDescription(d); },
			    "rotation ring should reject bandWidth that drives the inner radius non-positive");
		}
		{
			auto d = base();
			d.thickness = 0.0f;
			requireThrows<std::invalid_argument>(
			    [&d]() { lightGraphics::validateRotationRingDescription(d); },
			    "rotation ring should reject zero thickness");
		}
		{
			auto d = base();
			d.arcSegments = 3;
			requireThrows<std::invalid_argument>(
			    [&d]() { lightGraphics::validateRotationRingDescription(d); },
			    "rotation ring should reject too few arc segments");
		}
		{
			auto d = base();
			d.gapAngleRadians = 0.0f;
			requireThrows<std::invalid_argument>(
			    [&d]() { lightGraphics::validateRotationRingDescription(d); },
			    "rotation ring should reject a zero gap angle");
		}
		{
			auto d = base();
			d.gapAngleRadians = glm::two_pi<float>();
			requireThrows<std::invalid_argument>(
			    [&d]() { lightGraphics::validateRotationRingDescription(d); },
			    "rotation ring should reject a gap angle that consumes the circle");
		}
		{
			auto d = base();
			d.arrowHeadAngleRadians = 0.0f;
			requireThrows<std::invalid_argument>(
			    [&d]() { lightGraphics::validateRotationRingDescription(d); },
			    "rotation ring should reject a non-positive arrowhead angle");
		}
		{
			auto d = base();
			d.arrowHeadAngleRadians = glm::two_pi<float>() - d.gapAngleRadians;
			requireThrows<std::invalid_argument>(
			    [&d]() { lightGraphics::validateRotationRingDescription(d); },
			    "rotation ring should reject an arrowhead angle that doesn't fit the visible arc");
		}
		{
			auto d = base();
			d.arrowHeadWidthScale = 1.0f;
			requireThrows<std::invalid_argument>(
			    [&d]() { lightGraphics::validateRotationRingDescription(d); },
			    "rotation ring should reject arrowHeadWidthScale <= 1");
		}
		{
			auto d = base();
			d.radius = std::numeric_limits<float>::quiet_NaN();
			requireThrows<std::invalid_argument>(
			    [&d]() { lightGraphics::validateRotationRingDescription(d); },
			    "rotation ring should reject a non-finite radius");
		}
		{
			auto d = base();
			d.color.r = std::numeric_limits<float>::infinity();
			requireThrows<std::invalid_argument>(
			    [&d]() { lightGraphics::validateRotationRingDescription(d); },
			    "rotation ring should reject a non-finite color");
		}
	}

	// Pins today's observed load-time invariants for assets/Worker.fbx so a future
	// change to the FBX loading/skinning pipeline (exporter update, dependency bump,
	// or a regression in FBXLoader.cpp) is caught even if it stays within the looser
	// plausibility bounds checked by tests/worker_skinning_sanity.cpp. Two things this
	// doubles as regression coverage for, using the real asset rather than a synthetic
	// fixture (none of Worker.fbx's meshes have >4 raw bone influences per vertex or
	// zero bones, so those specific edge cases aren't exercised here — see the note
	// at the end of this function):
	//  - Duplicate node names (Worker.fbx's RootNode->RootNode case, see
	//    FBXLoader::buildBoneHierarchy) must not introduce a self-parenting cycle.
	//  - Per-vertex bone weight normalization (FBXLoader::processBones) must keep
	//    every weighted vertex's four weights summing to ~1.0.
	void testFBXLoaderWorkerAssetInvariants()
	{
#ifdef LVG_SOURCE_DIR
		const std::filesystem::path modelPath =
		    std::filesystem::path(LVG_SOURCE_DIR) / "assets" / "Worker.fbx";
#else
		const std::filesystem::path modelPath =
		    std::filesystem::path("assets") / "Worker.fbx";
#endif
		lightGraphics::FBXLoader loader;
		auto model = loader.loadModel(modelPath.string());
		require(model != nullptr, "failed to load Worker.fbx: " + loader.getLastError());

		require(model->bones.size() == 85,
		        "Worker.fbx global bone count changed from the pinned value of 85");
		require(model->meshes.size() == 12,
		        "Worker.fbx mesh count changed from the pinned value of 12");
		require(model->animations.size() == 24,
		        "Worker.fbx animation count changed from the pinned value of 24");

		int skinBindCount = 0;
		for (const auto& bone : model->bones)
		{
			if (bone.hasSkinBindTransform)
			{
				++skinBindCount;
			}
		}
		require(skinBindCount == 62,
		        "count of bones with an authoritative skin-cluster bind pose changed "
		        "from the pinned value of 62 — this drives which per-bone skinning "
		        "formula RiggedSkinning.h::buildRiggedFinalBoneMatrix uses");

		for (size_t i = 0; i < model->bones.size(); ++i)
		{
			require(model->bones[i].parentIndex != static_cast<int>(i),
			        "bone " + model->bones[i].name +
			            " is its own parent (duplicate-node-name handling regressed "
			            "into a self-cycle)");
		}

		for (const auto& mesh : model->meshes)
		{
			require(!mesh.bones.empty(), "Worker.fbx mesh unexpectedly has zero bones");
			for (const auto& vertex : mesh.vertices)
			{
				const float weightSum = vertex.boneWeights.x + vertex.boneWeights.y +
				                         vertex.boneWeights.z + vertex.boneWeights.w;
				if (weightSum > 1.0e-6f)
				{
					require(nearlyEqual(weightSum, 1.0f, 1.0e-3f),
					        "skinned vertex bone weights should sum to ~1.0 after "
					        "top-4 truncation and renormalization");
				}
			}
		}
	}
}

int main()
{
	try
	{
		testTransformIdentity();
		testTransformRoundTrip();
		testSceneNodeHandleBasics();
		testPObjectProperties();
		testVkAppObjectIndexValidation();
		testVkAppObjectDescription();
		testVkAppObjectHandleValidity();
		testVkAppLightHandleValidity();
		testVkAppHeadlessRiggedObject();
		testMeshValidation();
		testScreenTextMesh();
		testTexture3DValidation();
		testTransferFunctionSampling();
		testMaterialClippingAndVolumeValidation();
		testVolumeOpacityAndRenderOrdering();
		testVolumeTwoReportsAndDocumentation();
		testRotationGlyphDefaultMesh();
		testRotationGlyphRadialDimensions();
		testRotationGlyphDirection();
		testRotationGlyphTransform();
		testRotationGlyphInvalidDescriptions();
		testFBXLoaderRejectsMissingFile();
		testFBXLoaderRejectsMalformedFile();
		testFBXLoaderRejectsTruncatedFile();
		testFBXLoaderWorkerAssetInvariants();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Unit test failed: " << error.what() << '\n';
		return 1;
	}

	std::cout << "Core type unit tests passed\n";
	return 0;
}
