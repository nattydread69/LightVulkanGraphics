#include "SceneGraph.h"
#include "VkApp.h"
#include "pObject.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <iostream>
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
	}
	catch (const std::exception& error)
	{
		std::cerr << "Unit test failed: " << error.what() << '\n';
		return 1;
	}

	std::cout << "Core type unit tests passed\n";
	return 0;
}
