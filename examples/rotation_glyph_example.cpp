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

// Demonstrates RotationGlyph: an oriented-plane annular arrow for angular
// velocity, angular momentum, and torque, alongside the conventional axial
// arrow it's meant to complement (see docs/rotation_glyphs.md).

#include "RotationGlyph.h"
#include "VkApp.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>

namespace
{

// Applications decide how a physical magnitude maps to visual band width --
// this glyph module only knows about bandWidth, not units. See
// docs/rotation_glyphs.md "Mapping magnitude to band width".
float bandWidthForNormalizedMagnitude(float normalizedMagnitude, float minimumBandWidth,
                                       float maximumBandWidth)
{
	const float clamped = std::clamp(normalizedMagnitude, 0.0f, 1.0f);
	return minimumBandWidth + clamped * (maximumBandWidth - minimumBandWidth);
}

// Draws a small sphere at a glyph's reference point/pivot.
void addCentreMarker(lightGraphics::VkApp& app, const glm::vec3& centre, const glm::vec4& color)
{
	app.addObject(lightGraphics::ShapeType::SPHERE, centre, glm::vec3(0.06f), color,
	              glm::quat(1.0f, 0.0f, 0.0f, 0.0f), "Centre Marker", 0.0f);
}

// Draws the conventional axial/pseudovector arrow for comparison: a plain
// straight arrow along the rotation axis, through the same centre the ring
// glyph uses.
void addAxialArrow(lightGraphics::VkApp& app, const glm::vec3& centre, const glm::vec3& axis,
                    float halfLength, const glm::vec4& color)
{
	const glm::vec3 direction = glm::normalize(axis);
	const glm::vec3 tail = centre - direction * halfLength;
	const glm::vec3 tip = centre + direction * halfLength;

	glm::vec3 position;
	glm::quat rotation;
	glm::vec3 scale;
	app.computeArrowTransform(tail, tip, 0.035f, position, rotation, scale);

	const lightGraphics::ObjectHandle arrow =
	    app.addObject(lightGraphics::ShapeType::ARROW, position, scale, color, rotation,
	                  "Axial Arrow (conventional)", 0.0f);
	(void)arrow;
}

}

int main()
{
	try
	{
		lightGraphics::VkApp app;
		app.init(1280, 800, "LightVulkanGraphics Rotation Glyph Example");

		// A camera that orbits the three glyphs, driven directly through
		// setCameraLookAt every frame (see the update callback below) rather
		// than the mouse-driven orbit helper, so the view is correctly
		// oriented from the very first frame without requiring mouse input.
		const glm::vec3 sceneCentre(0.0f, 0.3f, 0.0f);
		constexpr float kCameraRadius = 7.5f;
		constexpr float kCameraHeight = 2.6f;
		auto cameraEyeForAzimuthDeg = [&](float azimuthDeg)
		{
			const float azimuth = glm::radians(azimuthDeg);
			return sceneCentre + glm::vec3(kCameraRadius * std::cos(azimuth), kCameraHeight,
			                               kCameraRadius * std::sin(azimuth));
		};
		constexpr float kInitialAzimuthDeg = -125.0f;
		app.setCameraLookAt(cameraEyeForAzimuthDeg(kInitialAzimuthDeg), sceneCentre,
		                    glm::vec3(0.0f, 1.0f, 0.0f));

		// A soft fill light in addition to the app's default directional
		// "sun" light, so the ribbons read clearly as 3D solids from any
		// orbit angle rather than going flat-dark on their shadowed side.
		app.addPointLight(glm::vec3(-3.0f, 4.0f, -4.0f), glm::vec3(1.0f, 0.97f, 0.9f), 12.0f, 30.0f,
		                  "Fill Light");

		lightGraphics::MaterialDescription ringMaterialDescription;
		ringMaterialDescription.shaderProgram.selection = lightGraphics::ShaderSelection::VertexColorLit;
		ringMaterialDescription.alphaBlendingEnabled = false;
		ringMaterialDescription.depthWriteEnabled = true;
		ringMaterialDescription.cullMode = lightGraphics::CullMode::Back;
		const lightGraphics::MaterialHandle ringMaterial = app.createMaterial(ringMaterialDescription);

		// --- Angular velocity (omega): horizontal plane, counterclockwise,
		// smallest nominal magnitude, and the one glyph animated live via
		// updateDynamicMesh to show that band width can change without
		// reallocating or re-registering the draw. ---
		const glm::vec3 omegaCentre(0.0f, 0.0f, 0.0f);
		const glm::vec3 omegaNormal(0.0f, 1.0f, 0.0f);
		lightGraphics::RotationRingDescription omegaDescription;
		omegaDescription.radius = 1.0f;
		omegaDescription.bandWidth = 0.10f;
		omegaDescription.sense = lightGraphics::RotationSense::CounterClockwise;
		omegaDescription.color = glm::vec4(0.15f, 0.65f, 1.0f, 1.0f);
		lightGraphics::validateRotationRingDescription(omegaDescription);

		const lightGraphics::MeshData omegaInitialMesh = lightGraphics::buildRotationRingMesh(omegaDescription);
		const lightGraphics::MeshHandle omegaMesh = app.createDynamicMesh(
		    omegaInitialMesh.vertices.size(), omegaInitialMesh.indices.size());
		app.updateDynamicMesh(omegaMesh, omegaInitialMesh);
		app.drawMesh(omegaMesh,
		             lightGraphics::makeRotationRingTransform(omegaCentre, omegaNormal, glm::vec3(1, 0, 0)),
		             ringMaterial);

		addCentreMarker(app, omegaCentre, glm::vec4(0.9f, 0.9f, 0.95f, 1.0f));
		addAxialArrow(app, omegaCentre, omegaNormal, 0.85f, glm::vec4(0.85f, 0.85f, 0.85f, 1.0f));

		// --- Angular momentum (L): oblique plane, clockwise, largest
		// nominal magnitude -- built as a plain static mesh. ---
		const glm::vec3 momentumCentre(2.7f, 0.0f, 0.0f);
		const glm::vec3 momentumNormal = glm::normalize(glm::vec3(0.55f, 0.65f, 0.85f));
		lightGraphics::RotationRingDescription momentumDescription;
		momentumDescription.radius = 1.0f;
		momentumDescription.bandWidth = bandWidthForNormalizedMagnitude(0.9f, 0.06f, 0.24f);
		momentumDescription.sense = lightGraphics::RotationSense::Clockwise;
		momentumDescription.color = glm::vec4(1.0f, 0.55f, 0.15f, 1.0f);
		const lightGraphics::MeshHandle momentumMesh =
		    app.createStaticMesh(lightGraphics::buildRotationRingMesh(momentumDescription));
		app.drawMesh(momentumMesh,
		             lightGraphics::makeRotationRingTransform(momentumCentre, momentumNormal,
		                                                       glm::vec3(0, 1, 0)),
		             ringMaterial);
		addCentreMarker(app, momentumCentre, glm::vec4(0.9f, 0.9f, 0.95f, 1.0f));

		// --- Torque (tau): a third, distinct plane (facing the initial
		// camera), counterclockwise, moderate magnitude. ---
		const glm::vec3 torqueCentre(-2.7f, 0.0f, 0.0f);
		const glm::vec3 torqueNormal(0.0f, 0.0f, 1.0f);
		lightGraphics::RotationRingDescription torqueDescription;
		torqueDescription.radius = 1.0f;
		torqueDescription.bandWidth = bandWidthForNormalizedMagnitude(0.45f, 0.06f, 0.24f);
		torqueDescription.sense = lightGraphics::RotationSense::CounterClockwise;
		torqueDescription.color = glm::vec4(0.75f, 0.3f, 0.95f, 1.0f);
		const lightGraphics::MeshHandle torqueMesh =
		    app.createStaticMesh(lightGraphics::buildRotationRingMesh(torqueDescription));
		app.drawMesh(torqueMesh,
		             lightGraphics::makeRotationRingTransform(torqueCentre, torqueNormal, glm::vec3(0, 1, 0)),
		             ringMaterial);
		addCentreMarker(app, torqueCentre, glm::vec4(0.9f, 0.9f, 0.95f, 1.0f));

		lightGraphics::ScreenTextDescription explanationText;
		explanationText.text =
		    "Rotation-ring glyphs (blue=omega, orange=L, purple=tau)\n"
		    "Ring plane = plane of rotational action\n"
		    "Curved arrowhead = rotational sense (cw/ccw)\n"
		    "Ribbon width = relative magnitude\n"
		    "Gray straight arrow = conventional axial/pseudovector arrow";
		explanationText.positionPixels = {24.0f, 24.0f};
		explanationText.scale = 1.5f;
		explanationText.color = {0.95f, 0.97f, 1.0f, 1.0f};
		explanationText.maximumCharacters = 256;
		const lightGraphics::ScreenTextHandle explanationTextHandle = app.createScreenText(explanationText);

		app.finalizeScene();

		float elapsedTime = 0.0f;
		lightGraphics::RotationRingDescription animatedOmega = omegaDescription;
		app.setUpdateCallback([&](float deltaTime)
		{
			elapsedTime += deltaTime;

			// Gently animate omega's band width to show that a glyph's
			// magnitude can change every frame while its topology (vertex and
			// index counts) stays fixed -- see docs/rotation_glyphs.md
			// "Updating a glyph's magnitude".
			const float normalizedMagnitude = 0.5f + 0.5f * std::sin(elapsedTime * 1.3f);
			animatedOmega.bandWidth = bandWidthForNormalizedMagnitude(normalizedMagnitude, 0.05f, 0.22f);
			app.updateDynamicMesh(omegaMesh, lightGraphics::buildRotationRingMesh(animatedOmega));

			const float azimuthDeg = kInitialAzimuthDeg + elapsedTime * 6.0f;
			app.setCameraLookAt(cameraEyeForAzimuthDeg(azimuthDeg), sceneCentre,
			                    glm::vec3(0.0f, 1.0f, 0.0f));
		});

		app.run();

		app.destroyScreenText(explanationTextHandle);
		app.destroyMesh(omegaMesh);
		app.destroyMesh(momentumMesh);
		app.destroyMesh(torqueMesh);
		app.destroyMaterial(ringMaterial);
	}
	catch (const std::exception& error)
	{
		std::cerr << "RotationGlyphExample failed: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
