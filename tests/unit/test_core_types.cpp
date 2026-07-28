#include "FBXLoader.h"
#include "RiggedObject.h"
#include "SceneGraph.h"
#include "VolumeRendering.h"
#include "VkApp.h"
#include "pObject.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>
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
		require(nearlyEqual(mesh.vertices.front().color, description.color),
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
	}
	catch (const std::exception& error)
	{
		std::cerr << "Unit test failed: " << error.what() << '\n';
		return 1;
	}

	std::cout << "Core type unit tests passed\n";
	return 0;
}
