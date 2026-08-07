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

#pragma once

#include "Camera.h"
#include "Light.h"
#include "VolumeRendering.h"
#include "pObject.h"

#include <vulkan/vulkan.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <array>
#include <thread>
#include <tuple>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <glm/glm.hpp>

struct GLFWwindow;

namespace lightGraphics::detail
{
	enum class RiggedSkinningBindCorrectionMode : int;

	struct Vertex
	{
		glm::vec3 pos;
		glm::vec3 nrm;
		glm::vec2 uv{0.0f};
	};

	struct Mesh
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		ShapeType shapeType = ShapeType::SPHERE;
		std::string name;
	};
	using MeshPtr = std::shared_ptr<Mesh>;

	struct Instance
	{
		glm::mat4 model;
		glm::vec3 color;
		float shapeType;
	};

	struct Buffer
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkDeviceSize size = 0;
	};

	struct UniformBufferObject
	{
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 proj;
	};

	struct GpuLight
	{
		glm::vec4 positionRange;
		glm::vec4 directionType;
		glm::vec4 colorIntensity;
		glm::vec4 spotAngles;
		glm::vec4 shadowInfo;
	};

	struct LightingBufferObject
	{
		glm::vec4 ambientAndCount;
		GpuLight lights[lightGraphics::MaxForwardLights];
		glm::mat4 shadowMatrices[lightGraphics::MaxForwardLights];
	};

	struct ShadowPushConstants
	{
		glm::mat4 lightViewProj;
	};

	// Bound before each (shape type, texture) draw batch in the flexible-shape
	// pass; see ObjectDescription::textureTiling. xy = repeat count, zw unused
	// (kept for std140-friendly 16-byte alignment across drivers).
	struct FlexibleShapeTexturePushConstants
	{
		glm::vec4 tiling{1.0f, 1.0f, 0.0f, 0.0f};
	};

	struct Texture
	{
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		VkDescriptorSet descriptor = VK_NULL_HANDLE;
		uint32_t width = 0;
		uint32_t height = 0;
		std::string path;
	};

	// Maps a generation-checked handle to the current position of its object in a
	// dense, compacting vector (e.g. _objects_, lights_, riggedInstances_). The
	// vector itself keeps shifting elements down on removal exactly as before;
	// only currentIndex is kept in sync, so a handle stays valid for its object's
	// whole lifetime even when unrelated earlier entries are removed.
	struct HandleSlot
	{
		std::uint32_t generation = 1;
		bool alive = false;
		std::uint32_t currentIndex = 0;
	};
}

#ifdef LVG_WITH_UI
#include <lightVulkanGraphics/ui/Types.h>

namespace lightGraphics::ui
{
	class DrawList;
	class Font;
	class UiRenderer;
	class UiPlatformGlfw;
	class GuiContext;
}
#endif

namespace lightGraphics
{
	class RiggedObject;
	class SceneGraph;
	struct EmbeddedTextureData;
	struct RiggedMesh;

	struct ObjectTag;
	struct LightTag;
	struct RiggedObjectTag;
	using ObjectHandle = ResourceHandle<ObjectTag>;
	using LightHandle = ResourceHandle<LightTag>;
	using RiggedObjectHandle = ResourceHandle<RiggedObjectTag>;

	// VkApp is not thread-safe. Scene mutation (addObject/removeObject, addLight/
	// removeLight, setObjectPosition and friends, addRiggedObject, etc.) and the
	// render loop (run()/drawFrame()) all read and write the same object/light/
	// rigged-instance containers with no synchronization, and must all happen on
	// the single thread that constructed this VkApp. A subset of the most
	// structurally risky entry points (container resize via add/remove, and the
	// render path itself) assert this in debug builds and log a warning in all
	// builds; the per-field setters funnel through the same check via
	// markObjectDirty(). This is a best-effort diagnostic net, not exhaustive
	// coverage or real synchronization — if you need to mutate the scene from a
	// worker thread, hand the data to the owning thread instead.
	class VkApp
	{
	public:
		// ==================== CORE LIFECYCLE & CONFIGURATION ====================

		enum class LogLevel
		{
			Error,
			Warning,
			Info,
			Debug
		};
		using LogCallback = std::function<void(LogLevel, const std::string&)>;

		VkApp();
		virtual ~VkApp();
		void init(int width, int height, const char* title);
		void finalizeScene();
		void run();
		void cleanup();
		void setShaderPath(const std::string &path);
		void setDebugOutput(bool enabled) { debugOutput = enabled; }
		bool getDebugOutput() const { return debugOutput; }
		void setLogCallback(LogCallback callback) { logCallback_ = callback; }
		void setManageGlfwLifecycle(bool manage) { manageGlfwLifecycle_ = manage; }
		bool getManageGlfwLifecycle() const { return manageGlfwLifecycle_; }
		// Caps how many distinct textures/solid-color textures can exist at once
		// (each consumes one descriptor set from a fixed-size pool sized at init()
		// time). Must be called before init(); has no effect afterward. Raise this
		// if a scene with many distinct textures throws "texture descriptor pool
		// exhausted" from createTextureFromPixels/createSolidColorTexture.
		void setMaxTextureCount(uint32_t count) { maxTextureDescriptorCount_ = count; }
		uint32_t getMaxTextureCount() const { return maxTextureDescriptorCount_; }

		// ==================== OBJECTS ====================

		// The raw pointer overload copies the object; the caller keeps ownership.
		ObjectHandle addObject(lightGraphics::pObject *newObject);
		ObjectHandle addObject(const lightGraphics::pObject& obj);
		ObjectHandle addObject(lightGraphics::ShapeType type, const glm::vec3& position,
		               const glm::vec3& size, const glm::vec4& color,
		               const glm::quat& rotation = glm::quat(1,0,0,0),
		               const std::string& name = "", float mass = 1.0f);
		ObjectHandle addObject(const lightGraphics::ObjectDescription& description);
		ObjectHandle addHexahedral(const glm::vec3& position, const glm::vec3& size,
		                   const glm::vec4& color,
		                   const glm::quat& rotation = glm::quat(1,0,0,0),
		                   const std::string& name = "Hexahedral",
		                   float mass = 1.0f);
		void removeObject(size_t index);
		void removeObject(ObjectHandle handle);
		void clearObjects();
		void updateObject(size_t index, const lightGraphics::pObject& obj);
		void updateObject(size_t index, const lightGraphics::ObjectDescription& description);
		size_t getObjectCount() const { return _objects_.size(); }
		const lightGraphics::pObject& getObject(size_t index) const { return _objects_[index]; }
		lightGraphics::ObjectDescription getObjectDescription(size_t index) const;
		bool isObjectHandleValid(ObjectHandle handle) const noexcept;
		size_t resolveObjectHandle(ObjectHandle handle) const;
		ObjectHandle objectHandleAt(size_t index) const;
		// Registers a substring that marks an object as a debug overlay: such
		// objects are excluded from shadow casting and drawn in a second pass
		// with the depth buffer cleared first, so they stay visible on top of
		// whatever they're marking regardless of true depth (e.g. a small
		// marker embedded inside a mesh) instead of being hidden by normal
		// depth testing. Matching is substring-based against pObject::name
		// (see addObject); callers own their own naming convention and
		// register whatever substrings they use -- this class has no
		// built-in list. Registering the same substring twice is harmless.
		void addDebugOverlayNameSubstring(const std::string& substring);

		size_t addRiggedObject(const std::shared_ptr<RiggedObject>& riggedObject);
		RiggedObjectHandle addRiggedObjectHandle(const std::shared_ptr<RiggedObject>& riggedObject);
		void removeRiggedObject(size_t index);
		void removeRiggedObject(RiggedObjectHandle handle);
		size_t getRiggedObjectCount() const { return riggedInstances_.size(); }
		bool isRiggedObjectHandleValid(RiggedObjectHandle handle) const noexcept;
		size_t resolveRiggedObjectHandle(RiggedObjectHandle handle) const;
		RiggedObjectHandle riggedObjectHandleAt(size_t index) const;
		// ==================== LIGHTS ====================

		// Main-thread-only — see the VkApp class doc comment.
		size_t addLight(const lightGraphics::LightSource& light);
		LightHandle addLightHandle(const lightGraphics::LightSource& light);
		size_t addDirectionalLight(const glm::vec3& direction,
		                           const glm::vec3& color = glm::vec3(1.0f),
		                           float intensity = 1.0f,
		                           const std::string& name = "");
		size_t addPointLight(const glm::vec3& position,
		                     const glm::vec3& color = glm::vec3(1.0f),
		                     float intensity = 1.0f,
		                     float range = 10.0f,
		                     const std::string& name = "");
		size_t addSpotLight(const glm::vec3& position,
		                    const glm::vec3& direction,
		                    const glm::vec3& color = glm::vec3(1.0f),
		                    float intensity = 1.0f,
		                    float range = 10.0f,
		                    float innerConeAngleRadians = glm::radians(20.0f),
		                    float outerConeAngleRadians = glm::radians(30.0f),
		                    const std::string& name = "");
		void removeLight(size_t index);
		void removeLight(LightHandle handle);
		void clearLights();
		void updateLight(size_t index, const lightGraphics::LightSource& light);
		size_t getLightCount() const { return lights_.size(); }
		const lightGraphics::LightSource& getLight(size_t index) const { return lights_[index]; }
		bool isLightHandleValid(LightHandle handle) const noexcept;
		size_t resolveLightHandle(LightHandle handle) const;
		LightHandle lightHandleAt(size_t index) const;
		void setLightPosition(size_t index, const glm::vec3& position);
		void setLightDirection(size_t index, const glm::vec3& direction);
		void setLightColor(size_t index, const glm::vec3& color);
		void setLightIntensity(size_t index, float intensity);
		void setLightRange(size_t index, float range);
		void setLightEnabled(size_t index, bool enabled);
		void setAmbientLight(const glm::vec3& ambientColor);
		glm::vec3 getAmbientLight() const { return ambientLight_; }
		void setShadowRenderingEnabled(bool enabled) { shadowRenderingEnabled_ = enabled; }
		bool getShadowRenderingEnabled() const { return shadowRenderingEnabled_; }
		// True once init() has created a Vulkan device. Scene-building APIs
		// (objects, lights, rigged objects) are safe to call before that: no
		// GLFW window or Vulkan device is required to build up and inspect
		// scene content, only to render it.
		bool isDeviceInitialized() const noexcept { return device_ != VK_NULL_HANDLE; }
		glm::mat4 getObjectModelMatrix(size_t index) const;
		void setObjectModelMatrixOverride(size_t index, const glm::mat4& model);
		void clearObjectModelMatrixOverride(size_t index);
		void setLightTransformMatrixOverride(size_t index, const glm::mat4& transform);
		void clearLightTransformMatrixOverride(size_t index);
		void setRiggedObjectTransformMatrixOverride(size_t index, const glm::mat4& transform);
		void clearRiggedObjectTransformMatrixOverride(size_t index);
		SceneGraph& sceneGraph();
		const SceneGraph& sceneGraph() const;

		// ==================== OBJECT PROPERTY UPDATES ====================

		// Object property update methods for physics simulation. Main-thread-only —
		// see the VkApp class doc comment.
		void setObjectPosition(size_t index, const glm::vec3& position);
		void setObjectScale(size_t index, const glm::vec3& scale);
		void setObjectRotation(size_t index, const glm::quat& rotation);
		void setObjectColor(size_t index, const glm::vec4& color);
		// Path to an image file (PNG/JPG/etc.) to texture this object with.
		// Empty clears it back to a flat lit color. Flexible shapes only
		// (SPHERE/CUBE/CONE/CYLINDER/CAPSULE/ARROW/HEX) -- rigged meshes and
		// screen text are unaffected. The file is loaded and cached the first
		// time any object references it.
		void setObjectTexturePath(size_t index, const std::string& path);
		// How many times the texture repeats across the shape's UV range.
		void setObjectTextureTiling(size_t index, const glm::vec2& tiling);
		void updateObjectProperties(size_t index, const glm::vec3& position,
		                            const glm::vec3& scale, const glm::quat& rotation);

		// Optimized batch update methods
		void updateObjectPositions(const std::vector<std::pair<size_t, glm::vec3>>& updates);
		void updateObjectProperties(const std::vector<std::tuple<size_t, glm::vec3, glm::vec3, glm::quat>>& updates);
		void flushPendingUpdates(); // Force immediate update of all pending changes

		// Physics update callback
		void setUpdateCallback(std::function<void(float)> callback);

		// ==================== CAMERA ====================

		// Camera control API
		void setKeyboardCameraEnabled(bool enabled);
		bool getKeyboardCameraEnabled() const { return keyboardCameraEnabled_; }
		void setCameraPosition(const glm::vec3& pos);
		glm::vec3 getCameraPosition() const { return camera_.position; }
		void moveCameraForward(float distance);
		void moveCameraRight(float distance);
		void moveCameraUp(float distance);
		void setCameraYawPitch(float yawDeg, float pitchDeg);
		void addCameraYawPitch(float yawDeltaDeg, float pitchDeltaDeg);
		void setCameraLookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up = glm::vec3(0,1,0));
		void setCameraLookAtLevel(const glm::vec3& eye, const glm::vec3& target,
		                          const glm::vec3& up = glm::vec3(0,1,0));
		void setCameraFov(float fovDeg);
		float getCameraFov() const { return camera_.fov; }
		void setCameraPlanes(float zNear, float zFar);
		void setCameraSensitivity(float sens);
		glm::vec3 getCameraForward() const;
		glm::vec3 getCameraRight() const;
		glm::vec3 getCameraUp() const;

		// Orbit camera API
		void setOrbitEnabled(bool enabled);
		bool getOrbitEnabled() const { return orbitEnabled_; }
		void setOrbitTarget(const glm::vec3& target);
		glm::vec3 getOrbitTarget() const { return orbitTarget_; }
		void setOrbitRadius(float radius);
		float getOrbitRadius() const { return orbitRadius_; }
		void setOrbitAngles(float azimuthDeg, float elevationDeg);
		void addOrbitAngles(float deltaAzimuthDeg, float deltaElevationDeg);
		void panOrbitTarget(float deltaRight, float deltaUp);
		void dollyOrbitRadius(float deltaRadius);
		void setOrbitSensitivities(float rotate, float pan, float dolly);

		// ==================== GEOMETRY HELPERS ====================

		// Cylinder connection helpers
		glm::quat rotationFromDirection(const glm::vec3& direction, const glm::vec3& up = glm::vec3(0, 1, 0));
		void addCylinderBetweenPoints(const glm::vec3& pointA, const glm::vec3& pointB,
		                              float radius, const glm::vec4& color,
		                              const std::string& name = "Cylinder", float mass = 1.0f);
		void addCylinderAlongAxis(const glm::vec3& center, const glm::vec3& axis,
		                          float length, float radius, const glm::vec4& color,
		                          const std::string& name = "Cylinder", float mass = 1.0f);
		void addCylinderConnectingSpheres(const glm::vec3& sphereA, const glm::vec3& sphereB,
		                                  float sphereRadiusA, float sphereRadiusB,
		                                  const glm::vec4& color, const std::string& name = "Connector",
		                                  float mass = 1.0f);

		// Arrow orientation helper. The built-in ARROW mesh (ShapeType::ARROW) is authored
		// along +Y with its local origin at the shaft/head boundary rather than at the tail,
		// so placing one to visually span two world-space points needs this instead of the
		// plain position/size/rotation used by the cylinder helpers above. Callers typically
		// pass the result straight to setObjectPosition/setObjectRotation/setObjectScale.
		void computeArrowTransform(const glm::vec3& tail, const glm::vec3& tip, float shaftRadius,
		                           glm::vec3& outPosition, glm::quat& outRotation, glm::vec3& outScale);

		// ==================== LATTICE GENERATION API ====================

		// Convert pObjects to rendering data
		void updateInstanceData();

		// Current rendering mode
		enum class RenderMode
		{
			FLEXIBLE_SHAPES,
			WIREFRAME,
			UNLIT,
			ORIGINAL_SPHERES,
			LINE
		};

		// Rendering mode control
		void setRenderMode(RenderMode mode);
		VkPipeline getCurrentPipeline();

		// ==================== CUSTOM MESH / MATERIAL / TEXT RESOURCES ====================

		// Client-defined mesh, material, texture, and volume resources.
		MeshHandle createStaticMesh(const MeshData& meshData);
		MeshHandle createDynamicMesh(
			std::size_t maximumVertexCount,
			std::size_t maximumIndexCount);
		void updateDynamicMesh(MeshHandle handle, const MeshData& meshData);
		void drawMesh(
			MeshHandle handle,
			const Transform& transform,
			MaterialHandle material);
		void drawMesh(
			MeshHandle handle,
			const Transform& transform,
			MaterialHandle material,
			const DrawOptions& options);
		void clearMeshDraws();
		void destroyMesh(MeshHandle handle);

		MaterialHandle createMaterial(const MaterialDescription& description);
		void destroyMaterial(MaterialHandle handle);

		ScreenTextHandle createScreenText(
			const ScreenTextDescription& description);
		void updateScreenText(
			ScreenTextHandle handle,
			const ScreenTextDescription& description);
		void updateScreenText(
			ScreenTextHandle handle,
			const std::string& text);
		void setScreenTextVisible(ScreenTextHandle handle, bool visible);
		void destroyScreenText(ScreenTextHandle handle);

		// ==================== VOLUME RENDERING ====================

		Texture3DHandle createTexture3D(
			const Texture3DDescription& description,
			const void* data,
			std::size_t byteSize);
		void updateTexture3D(
			Texture3DHandle handle,
			const void* data,
			std::size_t byteSize);
		void destroyTexture3D(Texture3DHandle handle);

		TransferFunctionHandle createTransferFunction(
			const std::vector<TransferFunctionPoint>& points);
		TransferFunctionHandle createTransferFunction(
			TransferFunctionPreset preset);
		void destroyTransferFunction(TransferFunctionHandle handle);

		VolumeHandle createVolume(const VolumeRenderDescription& description);
		void updateVolume(
			VolumeHandle handle,
			const VolumeRenderDescription& description);
		void drawVolume(VolumeHandle handle);
		void drawVolume(VolumeHandle handle, const DrawOptions& options);
		void hideVolume(VolumeHandle handle);
		void destroyVolume(VolumeHandle handle);

		GLFWwindow* getWindowPointer() const { return window_; }

	private:
		// See the class doc comment above: the thread that constructs this VkApp is
		// its only valid caller for scene mutation and rendering.
		std::thread::id ownerThreadId_;
		void assertOwnerThread(const char* where) const;

		bool debugOutput = false;
		LogCallback logCallback_;
		uint32_t maxTextureDescriptorCount_ = 256;
		// Window
		GLFWwindow* window_ = nullptr;
		bool manageGlfwLifecycle_ = true;
		bool glfwLifecycleAcquired_ = false;
		int width_;
		int height_;
		std::atomic<bool> framebufferResized_{false};

		// Geometry counts
		uint32_t indexCount = 0;
		uint32_t instanceCount = 0;

		// Scene state
		bool sceneFinalized_ = false;
		bool shadowRenderingEnabled_ = true;

		// Camera and input
		Camera camera_;
		bool mouseLook_ = false;
		bool firstMouse_ = true;
		double lastX_ = 0.0;
		double lastY_ = 0.0;
		double prevTime_ = 0.0;
		bool riggedNextKeyDown_ = false;
		bool riggedPrevKeyDown_ = false;
		bool riggedStopKeyDown_ = false;
		float scaleFactor = 1.0;
		uint32_t qFamGfx = 0, qFamPresent = 0;
		bool keyboardCameraEnabled_ = true; // allow programmatic control toggle
		// Orbit state
		bool orbitEnabled_ = false;
		glm::vec3 orbitTarget_ = glm::vec3(0.0f);
		float orbitRadius_ = 5.0f;
		float orbitAzimuthDeg_ = -90.0f;   // yaw-like around target
		float orbitElevationDeg_ = 15.0f;  // pitch-like around target
		float orbitRotateSens_ = 0.25f;
		float orbitPanSens_ = 0.01f;
		float orbitDollySens_ = 0.5f;
		bool supportsNonSolidFill_ = false;
		bool supportsWideLines_ = false;
		bool validationEnabled_ = false;

		// Vulkan core
		VkInstance inst = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
		VkSurfaceKHR surface_ = VK_NULL_HANDLE;
		VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
		VkPhysicalDeviceType physicalDeviceType_ = VK_PHYSICAL_DEVICE_TYPE_OTHER;
		VkDevice device_ = VK_NULL_HANDLE;
		VkQueue graphicsQueue_ = VK_NULL_HANDLE;
		VkQueue presentQueue_ = VK_NULL_HANDLE;

		// Swapchain and related
		VkSwapchainKHR swapChain_ = VK_NULL_HANDLE;
		std::vector<VkImage> swapChainImages_;
		VkFormat swapChainImageFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
		VkExtent2D swapChainExtent_{};
		std::vector<VkImageView> swapChainImageViews_;
		std::vector<VkFramebuffer> swapChainFramebuffers_;

		// Render pass / pipeline
		VkRenderPass renderPass_ = VK_NULL_HANDLE;
		VkRenderPass shadowRenderPass_ = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
		VkPipelineLayout shadowPipelineLayout_ = VK_NULL_HANDLE;
		VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
		VkPipeline shadowPipeline_ = VK_NULL_HANDLE;

		// Multiple pipelines for different rendering modes
		VkPipeline flexibleShapePipeline_ = VK_NULL_HANDLE;
		VkPipeline flexibleShapeOverlayPipeline_ = VK_NULL_HANDLE;
		VkPipelineLayout flexibleShapePipelineLayout_ = VK_NULL_HANDLE;
		VkPipeline wireframePipeline_ = VK_NULL_HANDLE;
		VkPipeline unlitPipeline_ = VK_NULL_HANDLE;
		VkPipeline linePipeline_ = VK_NULL_HANDLE;
		VkPipeline riggedPipeline_ = VK_NULL_HANDLE;
		VkPipelineLayout riggedPipelineLayout_ = VK_NULL_HANDLE;

	private:
		RenderMode currentRenderMode_ = RenderMode::FLEXIBLE_SHAPES;

		// Custom shader path (overrides automatic detection)
		std::string customShaderPath_;

		// Command infrastructure
		VkCommandPool commandPool_ = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer> commandBuffers_;

		// Depth
		VkImage depthImage_ = VK_NULL_HANDLE;
		VkDeviceMemory depthImageMemory_ = VK_NULL_HANDLE;
		VkImageView depthImageView_ = VK_NULL_HANDLE;
		VkFormat shadowDepthFormat_ = VK_FORMAT_UNDEFINED;
		VkImage shadowImage_ = VK_NULL_HANDLE;
		VkDeviceMemory shadowImageMemory_ = VK_NULL_HANDLE;
		VkImageView shadowImageView_ = VK_NULL_HANDLE;
		VkSampler shadowSampler_ = VK_NULL_HANDLE;
		std::vector<VkImageView> shadowLayerImageViews_;
		std::vector<VkFramebuffer> shadowFramebuffers_;
		uint32_t shadowMapSize_ = 1024;
		uint32_t shadowLayerCount_ = static_cast<uint32_t>(lightGraphics::MaxForwardLights);

#ifdef LVG_WITH_UI
		// ---- LVGUI (see docs/gui/) ----
		// The core library owns and drives the GUI's Vulkan backend; the dependency
		// runs core -> LightVulkanGraphicsUI and never the reverse. These are
		// unique_ptrs to incomplete types, so ~VkApp must stay out-of-line.
		std::unique_ptr<ui::UiRenderer> uiRenderer_;

		// Phase 4: raw GLFW input capture (docs/gui/04). Installed in createWindow() so
		// input capture starts as soon as the window exists, independent of Vulkan
		// readiness. GuiContext.h must never see a GLFW header (docs/gui/01's layering
		// rule), so it cannot itself be an installCallbacks() target; forwardInputToGui()
		// replays this object's already-resolved per-frame InputState into guiContext_
		// via the same inject* calls a platform layer would use.
		std::unique_ptr<ui::UiPlatformGlfw> uiPlatform_;

		// Phase 5: the real GUI (docs/gui/01-05). Owns the theme, font, and panels, and
		// produces the DrawList uiRenderer_ plays back each frame.
		std::unique_ptr<ui::GuiContext> guiContext_;

		// Logical display size as of this frame's guiContext_->beginFrame() call, kept
		// so recordUi() can pass it to UiRenderer::record()'s logicalSize parameter
		// (docs/gui/06, "DPI and content scale") without re-querying GLFW mid-recording.
		ui::Vec2 uiLastDisplaySize_{};

		void initUi();
		void destroyUi();
		void forwardInputToGui();
		void recordUi(VkCommandBuffer cmd);
		std::string findFontPath(const std::string& fontName);

		// The camera hand-off guard (docs/gui/04, "The camera hand-off"). Defined
		// out-of-line in VkAppUi.cpp -- ui::GuiContext is only forward-declared here, and
		// an inline body would need it complete in every translation unit that includes
		// this header.
		bool uiWantsMouse() const;
		bool uiWantsKeyboard() const;

	public:
		// Phase 10 (docs/gui/08-implementation-plan.md) formalises this into
		// LightVulkanGraphicsCreateInfo::enableGui / guiCreateInfo plus install rules for
		// the bundled font; until then the GUI is always constructed with theme defaults
		// as soon as LVG_WITH_UI is compiled in and init() has brought up a device and
		// render pass (see initUi()).
		bool hasGui() const { return guiContext_ != nullptr; }
		ui::GuiContext& gui();   // asserts hasGui()

	private:
#endif

		// Sync
		static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
		std::vector<VkSemaphore> imageAvailableSemaphores_;
		std::vector<VkSemaphore> renderFinishedSemaphores_;
		VkSemaphore semImage{}, semRender{};

		std::vector<VkFence> inFlight_;
		size_t currentFrame_ = 0;

		// Descriptors / UBOs
		VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
		VkDescriptorSetLayout textureSetLayout_ = VK_NULL_HANDLE;
		VkDescriptorSetLayout volumeSetLayout_ = VK_NULL_HANDLE;
		VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
		VkDescriptorPool textureDescriptorPool_ = VK_NULL_HANDLE;
		std::vector<VkBuffer> uniformBuffers_;
		std::vector<VkDeviceMemory> uniformBuffersMemory_;
		std::vector<VkDescriptorSet> descriptorSets_;
		std::vector<void*> uniformBuffersMapped_;
		std::vector<detail::Buffer> lightingBuffers_;
		std::vector<void*> lightingBuffersMapped_;

		// Timing
		std::chrono::steady_clock::time_point tPrev;

		// Sphere scene data (buffers etc.)
		detail::Buffer vbo, ibo, instanceBuf;
		std::vector<detail::Buffer> ubos;

		// Mesh buffers
		VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
		VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;

		VkBuffer indexBuffer_ = VK_NULL_HANDLE;
		VkDeviceMemory indexMemory_ = VK_NULL_HANDLE;
		uint32_t indexCount_ = 0;

		// Instance buffer (optional — comment out if you don't use instancing)
		VkBuffer instanceBuffer_ = VK_NULL_HANDLE;
		VkDeviceMemory instanceMemory_ = VK_NULL_HANDLE;
		uint32_t instanceCount_ = 0;

		// Performance optimization data structures
		std::vector<bool> dirtyObjects_; // Track which objects need updating
		bool instanceDataDirty_ = false; // Global dirty flag for instance data
		std::vector<std::optional<glm::mat4>> objectModelMatrixOverrides_;
		std::vector<lightGraphics::LightSource> lights_;
		std::vector<std::optional<glm::mat4>> lightTransformMatrixOverrides_;
		std::vector<detail::HandleSlot> lightSlots_;
		std::vector<std::uint32_t> freeLightSlots_;
		std::vector<std::uint32_t> lightSlotForIndex_;
		glm::vec3 ambientLight_{0.1f, 0.1f, 0.15f};
		// Populated by buildLightingBufferObject() (called once per frame, before
		// recordShadowPass() in the same frame — see drawFrame()) so recordShadowPass
		// can reuse each shadow-casting light's view-projection matrix instead of
		// recomputing it a second time. Indexed by light index, valid only for
		// entries a shadow-casting light actually wrote this frame; recordShadowPass
		// falls back to a direct call if an index isn't cached (e.g. out of range).
		mutable std::vector<glm::mat4> cachedShadowMatrices_;
		// Double-buffered (per-frame) instance buffers to avoid stalls
		detail::Buffer instanceBufs_[MAX_FRAMES_IN_FLIGHT]{};
		void* instanceBufferMappedPerFrame_[MAX_FRAMES_IN_FLIGHT]{}; // Persistently mapped per frame
		VkDeviceSize instanceBufferSizes_[MAX_FRAMES_IN_FLIGHT]{}; // Size per frame
		std::vector<detail::Instance> instanceDataCache_; // Cache for instance data
		struct RiggedMeshRenderData
		{
			const RiggedMesh* mesh = nullptr;
			detail::Buffer vertexBuffers[MAX_FRAMES_IN_FLIGHT]{};
			detail::Buffer vertexUploadBuffers[MAX_FRAMES_IN_FLIGHT]{};
			void* vertexUploadMapped[MAX_FRAMES_IN_FLIGHT]{};
			detail::Buffer indexBuffer;
			std::vector<detail::Vertex> skinnedVertices;
			// Persistent scratch buffer for updateRiggedInstances() so it isn't
			// reallocated every mesh, every instance, every frame.
			std::vector<glm::mat4> finalBoneMatrices;
			// True once skinnedVertices holds a real skinned pose (as opposed to
			// being freshly resized/default-constructed) — lets updateRiggedInstances()
			// know it's safe to reuse skinnedVertices as-is when the instance's pose
			// hasn't changed since last frame, skipping the per-vertex skin blend.
			bool hasValidSkinnedVertices = false;
			uint32_t indexCount = 0;
			std::shared_ptr<detail::Texture> texture;
		};
		struct RiggedInstanceRenderData
		{
			std::shared_ptr<RiggedObject> object;
			detail::Buffer instanceBuffers[MAX_FRAMES_IN_FLIGHT]{};
			detail::Buffer instanceUploadBuffers[MAX_FRAMES_IN_FLIGHT]{};
			void* instanceUploadMapped[MAX_FRAMES_IN_FLIGHT]{};
			glm::mat4 uprightCorrection = glm::mat4(1.0f);
			std::optional<glm::mat4> transformMatrixOverride;
			detail::RiggedSkinningBindCorrectionMode skinningCorrectionMode{};
			int activeAnimationIndex = -1;
			bool animationLoop = true;
			std::vector<RiggedMeshRenderData> meshes;
			// Last frame's bone transforms, so updateRiggedInstances() can skip
			// re-skinning entirely when a character's pose hasn't changed (e.g. a
			// held static pose) instead of re-blending every vertex every frame.
			std::vector<glm::mat4> lastBoneTransforms;
			bool hasLastBoneTransforms = false;
		};
		std::vector<RiggedInstanceRenderData> riggedInstances_;
		std::vector<detail::HandleSlot> riggedObjectSlots_;
		std::vector<std::uint32_t> freeRiggedObjectSlots_;
		std::vector<std::uint32_t> riggedObjectSlotForIndex_;
		std::unordered_map<std::string, std::shared_ptr<detail::Texture>> textureCache_;
		std::shared_ptr<detail::Texture> defaultTexture_;

		struct CustomMeshResource
		{
			std::uint32_t generation = 1;
			bool alive = false;
			bool dynamic = false;
			std::size_t maximumVertexCount = 0;
			std::size_t maximumIndexCount = 0;
			detail::Buffer vertexBuffer;
			detail::Buffer indexBuffer;
			std::uint32_t vertexCount = 0;
			std::uint32_t indexCount = 0;
		};

		struct MaterialResource
		{
			std::uint32_t generation = 1;
			bool alive = false;
			MaterialDescription description;
			VkPipeline pipeline = VK_NULL_HANDLE;
			VkPipelineLayout layout = VK_NULL_HANDLE;
		};

		struct ScreenTextResource
		{
			std::uint32_t generation = 1;
			bool alive = false;
			ScreenTextDescription description;
			MeshHandle mesh;
		};

		struct Texture3DResource
		{
			std::uint32_t generation = 1;
			bool alive = false;
			Texture3DDescription description;
			VkImage image = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
			VkSampler sampler = VK_NULL_HANDLE;
		};

		struct TransferFunctionResource
		{
			std::uint32_t generation = 1;
			bool alive = false;
			std::vector<TransferFunctionPoint> points;
			std::shared_ptr<detail::Texture> texture;
		};

		struct VolumeResource
		{
			std::uint32_t generation = 1;
			bool alive = false;
			bool visible = false;
			VolumeRenderDescription description;
			MeshHandle proxyMesh;
			VkDescriptorSet descriptor = VK_NULL_HANDLE;
			DrawOptions drawOptions{RenderLayer::Overlay, 0.0f};
			std::uint64_t submissionIndex = 0;
		};

		struct MeshDrawRequest
		{
			MeshHandle mesh;
			MaterialHandle material;
			Transform transform;
			DrawOptions drawOptions;
			std::uint64_t submissionIndex = 0;
		};

		enum class OrderedDrawKind
		{
			Mesh,
			Volume
		};

		struct OrderedDrawResource
		{
			OrderedDrawKind kind = OrderedDrawKind::Mesh;
			std::size_t index = 0;
		};

		std::vector<CustomMeshResource> customMeshes_;
		std::vector<std::uint32_t> freeCustomMeshes_;
		std::vector<MaterialResource> materials_;
		std::vector<std::uint32_t> freeMaterials_;
		std::vector<ScreenTextResource> screenTexts_;
		std::vector<std::uint32_t> freeScreenTexts_;
		MaterialHandle screenTextMaterial_;
		std::vector<Texture3DResource> textures3D_;
		std::vector<std::uint32_t> freeTextures3D_;
		std::vector<TransferFunctionResource> transferFunctions_;
		std::vector<std::uint32_t> freeTransferFunctions_;
		std::vector<VolumeResource> volumes_;
		std::vector<std::uint32_t> freeVolumes_;
		std::vector<MeshDrawRequest> meshDrawRequests_;
		VkPipeline volumePipeline_ = VK_NULL_HANDLE;
		VkPipelineLayout volumePipelineLayout_ = VK_NULL_HANDLE;
		std::vector<OrderedDrawResource> orderedDrawResources_;
		std::uint64_t nextDrawSubmissionIndex_ = 1;

	private:
		void logMessage(LogLevel level, const std::string& message) const;
		glm::vec3 camForward() const;
		void make_sphere(float r, int stacks, int slices,
							std::vector<detail::Vertex>& verts, std::vector<uint32_t>& idx);
		// Pointer-returning overloads for procedural geometry
		detail::MeshPtr make_sphere(float r, int stacks, int slices);
		void make_cone(float radius, float height, int slices,
					std::vector<detail::Vertex>& verts, std::vector<uint32_t>& idx);
		detail::MeshPtr make_cone(float radius, float height, int slices);
		void makeCapsule(float radius, float halfHeight, int slices, int stacks,
							std::vector<detail::Vertex>& verts, std::vector<uint32_t>& idx);
		detail::MeshPtr makeCapsule(float radius, float halfHeight, int slices, int stacks);
			void makeLine(glm::vec3 const &a, glm::vec3 const &b, std::vector<detail::Vertex> &verts, std::vector<uint32_t> &idx);
			detail::MeshPtr makeLine(glm::vec3 const &a, glm::vec3 const &b);

			void makeHexahedral(float size,
								std::vector<detail::Vertex>& verts, std::vector<uint32_t>& idx);
			void makeHexahedral(const glm::vec3& size,
								std::vector<detail::Vertex>& verts, std::vector<uint32_t>& idx);
			detail::MeshPtr makeHexahedral(float size);
			detail::MeshPtr makeHexahedral(const glm::vec3& size);

			void make_cylinder(float radius, float height, int slices,
							std::vector<detail::Vertex>& verts, std::vector<uint32_t>& idx);

		// Performance optimization methods
		void markObjectDirty(size_t index);
		void markLightingDirty();
		lightGraphics::LightSource lightForUpload(size_t index) const;
		glm::mat4 shadowMatrixForLight(size_t index) const;
		glm::vec3 shadowSceneCenter() const;
		detail::LightingBufferObject buildLightingBufferObject() const;
		void updateInstanceDataOptimized();
		void ensureInstanceBufferSizeForFrame(uint32_t frameIndex, VkDeviceSize requiredSize);
		detail::Instance makeInstanceForObject(size_t index) const;
		void clearObjectModelMatrixOverrideInternal(size_t index);

		// Shared bookkeeping for the generation-checked handle indirection tables
		// (objectSlots_/lightSlots_/riggedObjectSlots_). Each table maps a handle
		// to the current position of its entry within a dense, compacting vector.
		static std::uint32_t allocateHandleSlot(std::vector<detail::HandleSlot>& slots,
		                                        std::vector<std::uint32_t>& freeSlots,
		                                        std::uint32_t currentIndex);
		static void releaseHandleSlot(std::vector<detail::HandleSlot>& slots,
		                              std::vector<std::uint32_t>& freeSlots,
		                              std::uint32_t slot);
		static void reindexHandleSlotsFrom(std::vector<detail::HandleSlot>& slots,
		                                   const std::vector<std::uint32_t>& slotForIndex,
		                                   std::size_t startIndex);
		static bool isHandleSlotValid(const std::vector<detail::HandleSlot>& slots,
		                              std::uint32_t index,
		                              std::uint32_t generation) noexcept;
		static std::size_t resolveHandleSlot(const std::vector<detail::HandleSlot>& slots,
		                                     std::uint32_t index,
		                                     std::uint32_t generation,
		                                     const char* what);
		void updateRiggedInstances();
		void destroyRiggedInstance(RiggedInstanceRenderData& instance);
		void destroyRiggedInstances();
		detail::MeshPtr make_cylinder(float radius, float height, int slices);
		void makeArrow(float shaftRadius, float shaftLength, float headRadius, float headLength, int slices,
					std::vector<detail::Vertex>& verts, std::vector<uint32_t>& idx);
		detail::MeshPtr makeArrow(float shaftRadius, float shaftLength, float headRadius, float headLength, int slices);
		// GLFW callbacks
		static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
		static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
		static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
		static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

		// App handlers
		void onFramebufferResize(int width, int height);
		void onMouseButton(int button, int action, int mods);
		void onCursorPos(double xpos, double ypos);
		void onScroll(double xoffset, double yoffset);

		// Lifecycle helpers
		void createWindow(int w, int h, const char* title);
		void initVulkan();
		void setupDebugMessenger();
		void mainLoop();

		// Vulkan creation chain
		void createInstance();
		void createSurface();
		void pickPhysicalDevice();
		void createLogicalDevice();
		void createSwapChain();
		void createImageViews();
		void createRenderPass();
		void createShadowRenderPass();
		void createDescriptorSetLayout();
		void createGraphicsPipeline();
		void createShadowPipeline();
		void createOriginalSpherePipeline();
		void createFlexibleShapePipeline();
		void createRiggedPipeline();
		void createWireframePipeline();
		void createUnlitPipeline();
		void createLinePipeline();
		void createVolumePipeline();
		void createCommandPool();
		void createDepthResources();
		void createShadowResources();
		void createFramebuffers();
		void createUniformBuffers();
		void createDescriptorPool();
		void createDescriptorSets();
		void createCommandBuffers();
		void createSyncObjects();

		// Scene creation hooks
		void createSceneResources();

		// Teardown swapchain dependent
		void cleanupSwapChain();

		// Rebuild swapchain
		void recreateSwapChain();

		// Per-frame
		void updateCameraFromKeyboard(float dtSeconds);
		void handleRiggedAnimationInput();
		void updateUniformBuffer(uint32_t imageIndex);
		void updateLightingBuffer(uint32_t imageIndex);
		void recordShadowPass(VkCommandBuffer cmd);
		void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);
		void drawFrame();

		// Depth helpers
		VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
		VkFormat findDepthFormat();
		VkFormat findShadowDepthFormat();
		bool hasStencilComponent(VkFormat format);

		// Image helpers
		void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
			VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory,
			VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

		VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask);
		void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
		void copyBufferToImage3D(
			VkBuffer buffer,
			VkImage image,
			std::uint32_t width,
			std::uint32_t height,
			std::uint32_t depth);

		// One‑shot command helpers
		VkCommandBuffer beginSingleTimeCommands();
		void endSingleTimeCommands(VkCommandBuffer cmd);

		// Layout transition
		void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);

		// Utilities
		uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props);
		std::vector<char> readFile(const std::string& path);
		std::string findShaderPath(const std::string& shaderName);

		// Geometry generation
		void make_cube(std::vector<detail::Vertex>& verts, std::vector<uint32_t>& idx);
		detail::MeshPtr make_cube();
		void make_arrow(float headRadius, float headLength, float shaftRadius, float shaftLength, int slices, std::vector<detail::Vertex>& verts, std::vector<uint32_t>& idx);
		detail::MeshPtr make_arrow(float headRadius, float headLength, float shaftRadius, float shaftLength, int slices);
		void make_line(std::vector<detail::Vertex>& verts, std::vector<uint32_t>& idx);
		detail::MeshPtr make_line();
		detail::MeshPtr make_line(const glm::vec3& a, const glm::vec3& b);
		void make_hexahedral(std::vector<detail::Vertex>& verts, std::vector<uint32_t>& idx); // unit box
		void make_hexahedral(const glm::vec3& size, std::vector<detail::Vertex>& verts, std::vector<uint32_t>& idx); // axis-aligned box with varying x/y/z
		detail::MeshPtr make_hexahedral(float size);
		detail::MeshPtr make_hexahedral(const glm::vec3& size);
		void generateAllShapeGeometry(std::vector<detail::Vertex>& allVertices, std::vector<uint32_t>& allIndices);
		void storeShapeGeometryOffsets();

		void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, detail::Buffer& out);
		void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);


		void createBufferRaw(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
							VkBuffer& buffer, VkDeviceMemory& memory);

		void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const;
		void logSelectedPhysicalDeviceInfo(VkPhysicalDevice device) const;
		bool validateRiggedMesh(const RiggedMesh& mesh) const;
		static VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsMessengerCallback(
		    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
		    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
		    void* userData);

		void destroyBuffer(VkDevice device, detail::Buffer& buf);
		void createTextureDescriptorPool();
		void destroyTextureResources();
		std::shared_ptr<detail::Texture> createTextureFromPixels(
			const void* pixels,
			uint32_t width,
			uint32_t height,
			VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);
		std::shared_ptr<detail::Texture> createSolidColorTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
		std::shared_ptr<detail::Texture> getOrCreateSolidColorTexture(const glm::vec4& color);
		std::shared_ptr<detail::Texture> createTextureFromFile(const std::string& path);
		std::shared_ptr<detail::Texture> createTextureFromEmbedded(const EmbeddedTextureData& source, const std::string& cacheKey);
		std::shared_ptr<detail::Texture> getOrCreateTexture(const std::string& path);
		void destroyTexture(detail::Texture& texture);
		MaterialHandle getOrCreateScreenTextMaterial();
		void updateScreenTextMesh(ScreenTextResource& resource);
		void rebuildScreenTextMeshes();
		void destroyCustomResources();
		void rebuildCustomPipelines();
		VkPipeline createMaterialPipeline(
			const MaterialDescription& description,
			VkPipelineLayout layout);
		void createVolumeDescriptor(VolumeResource& volume);
		void rebuildOrderedDrawResources();
		void drawOrderedCustomResources(
			VkCommandBuffer commandBuffer,
			std::uint32_t imageIndex);
		void drawCustomMeshRequest(
			VkCommandBuffer commandBuffer,
			std::uint32_t imageIndex,
			const MeshDrawRequest& request);
		void drawVolumeResource(
			VkCommandBuffer commandBuffer,
			std::uint32_t imageIndex,
			const VolumeResource& volume);


		std::vector<detail::Buffer> uniformBuffers2_;          // using Buffer struct
		// Track which fence is using each swapchain image to avoid double-submit
		std::vector<VkFence> imagesInFlight_;

		std::vector<lightGraphics::pObject> _objects_;
		std::vector<detail::HandleSlot> objectSlots_;
		std::vector<std::uint32_t> freeObjectSlots_;
		std::vector<std::uint32_t> objectSlotForIndex_;
		std::unique_ptr<SceneGraph> sceneGraph_;

		// See addDebugOverlayNameSubstring().
		std::unordered_set<std::string> debugOverlayNameSubstrings_;
		bool isDebugOverlayObjectName(const std::string& name) const;

		// Physics update callback
		std::function<void(float)> updateCallback_;

		// Shape geometry offsets and counts
		struct ShapeGeometry {
			uint32_t vertexOffset;
			uint32_t vertexCount;
			uint32_t indexOffset;
			uint32_t indexCount;
		};
		std::vector<ShapeGeometry> shapeGeometries_;
	};
} // namespace lightGraphics
