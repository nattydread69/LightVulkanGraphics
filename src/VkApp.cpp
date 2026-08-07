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
#include "FBXLoader.h"
#include "LightVulkanGraphicsLogging.h"
#include "RiggedObject.h"
#include "RiggedSkinning.h"
#include "SceneGraph.h"

#ifdef LVG_WITH_UI
// ~VkApp lives in this translation unit, and destroying the GUI unique_ptr members
// requires their complete types.
#include "ui/UiRenderer.h"
#include "ui/UiPlatformGlfw.h"
#include <lightVulkanGraphics/ui/DrawList.h>
#include <lightVulkanGraphics/ui/Font.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <GLFW/glfw3.h>

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <optional>
#include <array>
#include <limits>
#include <mutex>
#include <type_traits>
#include <unordered_set>

#include <vulkan/vulkan.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace lightGraphics
{
	using detail::Buffer;
	using detail::GpuLight;
	using detail::Instance;
	using detail::LightingBufferObject;
	using detail::Mesh;
	using detail::MeshPtr;
	using detail::Texture;
	using detail::UniformBufferObject;
	using detail::Vertex;

	static_assert(std::is_standard_layout_v<Vertex>, "Vertex must use a stable standard layout");
	static_assert(offsetof(Vertex, pos) == 0, "Vertex position must start at offset 0");
	static_assert(offsetof(Vertex, nrm) == sizeof(glm::vec3), "Vertex normal layout changed");
	static_assert(offsetof(Vertex, uv) == sizeof(glm::vec3) * 2, "Vertex UV layout changed");
	static_assert(sizeof(Vertex) == sizeof(float) * 8, "Vertex size must stay tightly packed");


	static_assert(std::is_standard_layout_v<Instance>, "Instance must use a stable standard layout");
	static_assert(offsetof(Instance, model) == 0, "Instance model matrix must start at offset 0");
	static_assert(offsetof(Instance, color) == sizeof(glm::mat4), "Instance color offset changed");
	static_assert(offsetof(Instance, shapeType) == sizeof(glm::mat4) + sizeof(glm::vec3),
	              "Instance shapeType offset changed");
	static_assert(sizeof(Instance) == sizeof(float) * 20, "Instance size must match vertex input stride");
	static_assert(std::is_standard_layout_v<GpuLight>, "GpuLight must use a stable standard layout");
	static_assert(sizeof(GpuLight) == sizeof(glm::vec4) * 5, "GpuLight size must match shader layout");
	static_assert(std::is_standard_layout_v<LightingBufferObject>,
	              "LightingBufferObject must use a stable standard layout");
	static_assert(std::is_standard_layout_v<detail::ShadowPushConstants>,
	              "ShadowPushConstants must use a stable standard layout");

	namespace
	{
		constexpr std::array<const char*, 1> kValidationLayers = {
			"VK_LAYER_KHRONOS_validation"
		};

		std::mutex glfwLifecycleMutex;
		size_t glfwLifecycleReferenceCount = 0;

		constexpr bool shouldEnableValidationLayers()
		{
#ifndef NDEBUG
			return true;
#else
			return false;
#endif
		}

		void checkVkResult(VkResult result, const char* expression, const char* file, int line)
		{
			if (result == VK_SUCCESS)
			{
				return;
			}

			std::ostringstream message;
			message << "Vulkan call failed (" << static_cast<int>(result) << "): "
			        << expression << " at " << file << ":" << line;
			throw std::runtime_error(message.str());
		}

		std::vector<VkLayerProperties> enumerateInstanceLayers()
		{
			uint32_t count = 0;
			checkVkResult(vkEnumerateInstanceLayerProperties(&count, nullptr),
			              "vkEnumerateInstanceLayerProperties(&count, nullptr)",
			              __FILE__,
			              __LINE__);
			std::vector<VkLayerProperties> layers(count);
			if (count > 0)
			{
				checkVkResult(vkEnumerateInstanceLayerProperties(&count, layers.data()),
				              "vkEnumerateInstanceLayerProperties(&count, layers.data())",
				              __FILE__,
				              __LINE__);
			}
			return layers;
		}

		std::vector<VkExtensionProperties> enumerateInstanceExtensions()
		{
			uint32_t count = 0;
			checkVkResult(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr),
			              "vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr)",
			              __FILE__,
			              __LINE__);
			std::vector<VkExtensionProperties> extensions(count);
			if (count > 0)
			{
				checkVkResult(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()),
				              "vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data())",
				              __FILE__,
				              __LINE__);
			}
			return extensions;
		}

		std::vector<VkExtensionProperties> enumerateDeviceExtensions(VkPhysicalDevice device)
		{
			uint32_t count = 0;
			checkVkResult(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
			              "vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr)",
			              __FILE__,
			              __LINE__);
			std::vector<VkExtensionProperties> extensions(count);
			if (count > 0)
			{
				checkVkResult(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()),
				              "vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data())",
				              __FILE__,
				              __LINE__);
			}
			return extensions;
		}

		bool hasInstanceLayer(const std::vector<VkLayerProperties>& layers, const char* name)
		{
			return std::any_of(layers.begin(), layers.end(),
			                   [name](const VkLayerProperties& layer)
			                   {
				                   return std::strcmp(layer.layerName, name) == 0;
			                   });
		}

		bool hasInstanceExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
		{
			return std::any_of(extensions.begin(), extensions.end(),
			                   [name](const VkExtensionProperties& extension)
			                   {
				                   return std::strcmp(extension.extensionName, name) == 0;
			                   });
		}

		bool hasDeviceExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
		{
			return std::any_of(extensions.begin(), extensions.end(),
			                   [name](const VkExtensionProperties& extension)
			                   {
				                   return std::strcmp(extension.extensionName, name) == 0;
			                   });
		}

		void appendUniqueExtension(std::vector<const char*>& extensions, const char* name)
		{
			if (std::find_if(extensions.begin(), extensions.end(),
			                 [name](const char* existing)
			                 {
				                 return std::strcmp(existing, name) == 0;
			                 }) == extensions.end())
			{
				extensions.push_back(name);
			}
		}

		VkResult createDebugUtilsMessenger(VkInstance instance,
		                                   const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
		                                   const VkAllocationCallbacks* allocator,
		                                   VkDebugUtilsMessengerEXT* messenger)
		{
			auto createFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
			    vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
			if (!createFn)
			{
				return VK_ERROR_EXTENSION_NOT_PRESENT;
			}

			return createFn(instance, createInfo, allocator, messenger);
		}

		void destroyDebugUtilsMessenger(VkInstance instance,
		                                VkDebugUtilsMessengerEXT messenger,
		                                const VkAllocationCallbacks* allocator)
		{
			auto destroyFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
			    vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
			if (destroyFn)
			{
				destroyFn(instance, messenger, allocator);
			}
		}

		std::string formatVulkanVersion(uint32_t version)
		{
			std::ostringstream message;
			message << VK_VERSION_MAJOR(version) << '.'
			        << VK_VERSION_MINOR(version) << '.'
			        << VK_VERSION_PATCH(version);
			return message.str();
		}

		std::string vendorName(uint32_t vendorId)
		{
			switch (vendorId)
			{
				case 0x10DE: return "NVIDIA";
				case 0x1002: return "AMD";
				case 0x1022: return "AMD";
				case 0x8086: return "Intel";
				case 0x13B5: return "ARM";
				case 0x5143: return "Qualcomm";
				case 0x106B: return "Apple";
				default: return "Unknown";
			}
		}

		std::string formatDriverVersion(uint32_t vendorId, uint32_t driverVersion)
		{
			std::ostringstream message;
			if (vendorId == 0x10DE)
			{
				message << ((driverVersion >> 22) & 0x3ff) << '.'
				        << ((driverVersion >> 14) & 0x0ff) << '.'
				        << ((driverVersion >> 6) & 0x0ff) << '.'
				        << (driverVersion & 0x03f);
			}
			else if (vendorId == 0x8086)
			{
				message << (driverVersion >> 14) << '.' << (driverVersion & 0x3fff);
			}
			else
			{
				message << VK_VERSION_MAJOR(driverVersion) << '.'
				        << VK_VERSION_MINOR(driverVersion) << '.'
				        << VK_VERSION_PATCH(driverVersion);
			}

			message << " (0x" << std::hex << driverVersion << std::dec << ')';
			return message.str();
		}

		const char* physicalDeviceTypeName(VkPhysicalDeviceType type)
		{
			switch (type)
			{
				case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "Discrete GPU";
				case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated GPU";
				case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "Virtual GPU";
				case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
				case VK_PHYSICAL_DEVICE_TYPE_OTHER: return "Other";
				default: return "Unknown";
			}
		}

		int physicalDeviceTypeScore(VkPhysicalDeviceType type)
		{
			switch (type)
			{
				case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 4000;
				case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 3000;
				case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 2000;
				case VK_PHYSICAL_DEVICE_TYPE_OTHER: return 1000;
				case VK_PHYSICAL_DEVICE_TYPE_CPU: return 0;
				default: return -1000;
			}
		}

		std::string memoryPropertyFlagsToString(VkMemoryPropertyFlags flags)
		{
			std::ostringstream message;
			bool first = true;
			auto appendFlag = [&](VkMemoryPropertyFlags bit, const char* label)
			{
				if ((flags & bit) == 0)
				{
					return;
				}

				if (!first)
				{
					message << '|';
				}
				first = false;
				message << label;
			};

			appendFlag(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "DEVICE_LOCAL");
			appendFlag(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, "HOST_VISIBLE");
			appendFlag(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "HOST_COHERENT");
			appendFlag(VK_MEMORY_PROPERTY_HOST_CACHED_BIT, "HOST_CACHED");
			appendFlag(VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, "LAZILY_ALLOCATED");
#ifdef VK_MEMORY_PROPERTY_PROTECTED_BIT
			appendFlag(VK_MEMORY_PROPERTY_PROTECTED_BIT, "PROTECTED");
#endif
#ifdef VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD
			appendFlag(VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD, "DEVICE_COHERENT_AMD");
#endif
#ifdef VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD
			appendFlag(VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD, "DEVICE_UNCACHED_AMD");
#endif
			if (first)
			{
				message << '0';
			}
			return message.str();
		}

		std::string makeObjectIndexMessage(const char* operation, size_t index, size_t size)
		{
			std::ostringstream message;
			message << operation << " index " << index << " is out of range for "
			        << size << " objects";
			return message.str();
		}

	}

#define LVG_VK_CHECK(expr) checkVkResult((expr), #expr, __FILE__, __LINE__)
#define VK_CHECK(expr) LVG_VK_CHECK(expr)

	VkApp::VkApp()
	    : ownerThreadId_(std::this_thread::get_id())
	    , sceneGraph_(std::make_unique<SceneGraph>(*this))
	{
		LightSource defaultLight;
		defaultLight.type = LightType::Directional;
		defaultLight.direction = -glm::normalize(glm::vec3(0.4f, 0.7f, 0.2f));
		defaultLight.color = glm::vec3(1.0f);
		defaultLight.intensity = 1.0f;
		defaultLight.name = "Default Directional Light";
		lights_.push_back(defaultLight);
		lightTransformMatrixOverrides_.push_back(std::nullopt);

		const std::uint32_t slot = allocateHandleSlot(lightSlots_, freeLightSlots_, 0);
		lightSlotForIndex_.push_back(slot);
	}

	VkApp::~VkApp()
	{
		cleanup();
	}

	void VkApp::assertOwnerThread(const char* where) const
	{
		if (std::this_thread::get_id() != ownerThreadId_)
		{
			consoleErrorStream() << "[VkApp] " << where
			    << " was called from a different thread than the one that constructed "
			       "this VkApp. VkApp is not thread-safe: all scene mutation and "
			       "rendering must happen on a single thread." << std::endl;
			assert(false && "VkApp method called from a non-owning thread; see the "
			                 "class doc comment in VkApp.h");
		}
	}

	void VkApp::logMessage(LogLevel level, const std::string& message) const
	{
		if (logCallback_)
		{
			logCallback_(level, message);
			return;
		}

		if (!debugOutput)
		{
			return;
		}

		auto& stream = (level == LogLevel::Error || level == LogLevel::Warning)
		             ? consoleErrorStream()
		             : consoleInfoStream();
		stream << message << std::endl;
	}

	VKAPI_ATTR VkBool32 VKAPI_CALL VkApp::debugUtilsMessengerCallback(
	    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
	    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
	    void* userData)
	{
		(void) messageTypes;

		const VkApp* app = reinterpret_cast<const VkApp*>(userData);
		const char* messageText = (callbackData && callbackData->pMessage)
		                        ? callbackData->pMessage
		                        : "Validation message with no text";

		LogLevel level = LogLevel::Info;
		if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		{
			level = LogLevel::Error;
		}
		else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		{
			level = LogLevel::Warning;
		}
		else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
		{
			level = LogLevel::Info;
		}
		else
		{
			level = LogLevel::Debug;
		}

		// Routed exclusively through logMessage() so debugOutput (see setDebugOutput /
		// LightVulkanGraphicsCreateInfo::enableDebugOutput, both default false) is the
		// single switch for validation-layer chatter, independent of general console
		// output. A log callback still receives messages even when debugOutput is off.
		std::string message = std::string("[Validation] ") + messageText;
		if (app)
		{
			app->logMessage(level, message);
		}

		return VK_FALSE;
	}

	void VkApp::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const
	{
		createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity =
		    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType =
		    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback = &VkApp::debugUtilsMessengerCallback;
		createInfo.pUserData = const_cast<VkApp*>(this);
	}

	void VkApp::setupDebugMessenger()
	{
		if (!validationEnabled_ || inst == VK_NULL_HANDLE)
		{
			return;
		}

		VkDebugUtilsMessengerCreateInfoEXT createInfo{};
		populateDebugMessengerCreateInfo(createInfo);
		const VkResult result = createDebugUtilsMessenger(inst, &createInfo, nullptr, &debugMessenger_);
		if (result == VK_ERROR_EXTENSION_NOT_PRESENT)
		{
			logMessage(LogLevel::Warning, "VK_EXT_debug_utils is unavailable; validation messages will not be hooked");
			return;
		}

		VK_CHECK(result);
	}

	void VkApp::logSelectedPhysicalDeviceInfo(VkPhysicalDevice device) const
	{
		if (device == VK_NULL_HANDLE)
		{
			return;
		}

		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(device, &properties);

		VkPhysicalDeviceMemoryProperties memoryProperties{};
		vkGetPhysicalDeviceMemoryProperties(device, &memoryProperties);

		std::ostringstream summary;
		summary << "Selected GPU: " << properties.deviceName
		        << " (" << physicalDeviceTypeName(properties.deviceType) << ")"
		        << " [" << vendorName(properties.vendorID) << ", vendor=0x"
		        << std::hex << properties.vendorID << ", device=0x" << properties.deviceID
		        << std::dec << "], API " << formatVulkanVersion(properties.apiVersion)
		        << ", driver " << formatDriverVersion(properties.vendorID, properties.driverVersion)
		        << ", graphics queue family " << qFamGfx
		        << ", present queue family " << qFamPresent;
		if (logCallback_ || debugOutput)
		{
			logMessage(LogLevel::Info, summary.str());
		}
		else
		{
			consoleInfoStream() << summary.str() << std::endl;
		}

		if (!debugOutput)
		{
			return;
		}

		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
		{
			std::ostringstream message;
			message << "[GPU] memoryType[" << i << "] heap=" << memoryProperties.memoryTypes[i].heapIndex
			        << " flags=" << memoryPropertyFlagsToString(memoryProperties.memoryTypes[i].propertyFlags);
			logMessage(LogLevel::Debug, message.str());
		}
	}

	void VkApp::init(int width, int height, const char* title)
	{
		width_ = width;
		height_ = height;

		createWindow(width_, height_, title);
		initVulkan();
		createSwapChain();
		createImageViews();
		createRenderPass();
		createShadowRenderPass();
		createDescriptorSetLayout();
		createGraphicsPipeline();
		createCommandPool();
		createSyncObjects();
		createDepthResources();
		createShadowResources();
		createUniformBuffers();
		createDescriptorPool();
		createTextureDescriptorPool();
		createDescriptorSets();
		defaultTexture_ = createSolidColorTexture(255, 255, 255, 255);
		createSceneResources();

#ifdef LVG_WITH_UI
		initUi();
#endif

		// Initialize timing for keyboard movement
		prevTime_ = glfwGetTime();
	}

	void VkApp::finalizeScene()
	{
		// This should be called after all objects have been added
		createFramebuffers();
		createCommandBuffers();
		sceneFinalized_ = true;

		// Update instance data for all objects that were added before finalization
		sceneGraph_->updateWorldTransforms();
		sceneGraph_->syncToRenderer();
		updateInstanceData();
	}

	void VkApp::run()
	{
		if (!sceneFinalized_)
		{
			throw std::logic_error("Scene not finalized. Call finalizeScene() before run().");
		}
		mainLoop();
	}

	void VkApp::cleanup()
	{
		if (device_ != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(device_);
		}

#ifdef LVG_WITH_UI
		// Before cleanupSwapChain(), which destroys the render pass the UI pipeline
		// was created against.
		destroyUi();
#endif

		// Tear down swapchain-dependent resources
		cleanupSwapChain();

			// Scene resources teardown here
			destroyCustomResources();
			destroyRiggedInstances();
			destroyTextureResources();
			for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
			{
				if (instanceBufferMappedPerFrame_[frameIndex] != nullptr &&
				    instanceBufs_[frameIndex].memory != VK_NULL_HANDLE)
				{
					vkUnmapMemory(device_, instanceBufs_[frameIndex].memory);
					instanceBufferMappedPerFrame_[frameIndex] = nullptr;
				}
				destroyBuffer(device_, instanceBufs_[frameIndex]);
				instanceBufferSizes_[frameIndex] = 0;
			}
			destroyBuffer(device_, instanceBuf);
			destroyBuffer(device_, vbo);
			destroyBuffer(device_, ibo);
			vertexBuffer_ = VK_NULL_HANDLE;
			indexBuffer_ = VK_NULL_HANDLE;
			instanceBuffer_ = VK_NULL_HANDLE;
			vertexMemory_ = VK_NULL_HANDLE;
			indexMemory_ = VK_NULL_HANDLE;
			instanceMemory_ = VK_NULL_HANDLE;
			indexCount_ = 0;
			instanceCount_ = 0;
			sceneFinalized_ = false;

			// Sync objects
		for (size_t i = 0; i < imageAvailableSemaphores_.size(); i++)
		{
			if (renderFinishedSemaphores_[i] != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(device_, renderFinishedSemaphores_[i], nullptr);
			}
			if (imageAvailableSemaphores_[i] != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
			}
			if (inFlight_[i] != VK_NULL_HANDLE)
			{
				vkDestroyFence(device_, inFlight_[i], nullptr);
			}
		}
		imageAvailableSemaphores_.clear();
		renderFinishedSemaphores_.clear();
		inFlight_.clear();

		// Command pool
		if (commandPool_ != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(device_, commandPool_, nullptr);
			commandPool_ = VK_NULL_HANDLE;
		}

		// Pipeline and render pass
		if (graphicsPipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
			graphicsPipeline_ = VK_NULL_HANDLE;
		}
		if (flexibleShapePipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, flexibleShapePipeline_, nullptr);
			flexibleShapePipeline_ = VK_NULL_HANDLE;
		}
		if (flexibleShapeOverlayPipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, flexibleShapeOverlayPipeline_, nullptr);
			flexibleShapeOverlayPipeline_ = VK_NULL_HANDLE;
		}
		if (wireframePipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, wireframePipeline_, nullptr);
			wireframePipeline_ = VK_NULL_HANDLE;
		}
		if (unlitPipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, unlitPipeline_, nullptr);
			unlitPipeline_ = VK_NULL_HANDLE;
		}
		if (linePipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, linePipeline_, nullptr);
			linePipeline_ = VK_NULL_HANDLE;
		}
		if (volumePipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, volumePipeline_, nullptr);
			volumePipeline_ = VK_NULL_HANDLE;
		}
		if (volumePipelineLayout_ != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device_, volumePipelineLayout_, nullptr);
			volumePipelineLayout_ = VK_NULL_HANDLE;
		}
		for (MaterialResource& material : materials_)
		{
			if (material.pipeline != VK_NULL_HANDLE)
			{
				vkDestroyPipeline(device_, material.pipeline, nullptr);
				material.pipeline = VK_NULL_HANDLE;
			}
		}
		if (riggedPipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, riggedPipeline_, nullptr);
			riggedPipeline_ = VK_NULL_HANDLE;
		}
		if (shadowPipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, shadowPipeline_, nullptr);
			shadowPipeline_ = VK_NULL_HANDLE;
		}
		if (pipelineLayout_ != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
			pipelineLayout_ = VK_NULL_HANDLE;
		}
		if (shadowPipelineLayout_ != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device_, shadowPipelineLayout_, nullptr);
			shadowPipelineLayout_ = VK_NULL_HANDLE;
		}
		if (riggedPipelineLayout_ != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device_, riggedPipelineLayout_, nullptr);
			riggedPipelineLayout_ = VK_NULL_HANDLE;
		}
		if (flexibleShapePipelineLayout_ != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device_, flexibleShapePipelineLayout_, nullptr);
			flexibleShapePipelineLayout_ = VK_NULL_HANDLE;
		}
		for (VkFramebuffer framebuffer : shadowFramebuffers_)
		{
			vkDestroyFramebuffer(device_, framebuffer, nullptr);
		}
		shadowFramebuffers_.clear();
		for (VkImageView view : shadowLayerImageViews_)
		{
			vkDestroyImageView(device_, view, nullptr);
		}
		shadowLayerImageViews_.clear();
		if (shadowImageView_ != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device_, shadowImageView_, nullptr);
			shadowImageView_ = VK_NULL_HANDLE;
		}
		if (shadowSampler_ != VK_NULL_HANDLE)
		{
			vkDestroySampler(device_, shadowSampler_, nullptr);
			shadowSampler_ = VK_NULL_HANDLE;
		}
		if (shadowImage_ != VK_NULL_HANDLE)
		{
			vkDestroyImage(device_, shadowImage_, nullptr);
			shadowImage_ = VK_NULL_HANDLE;
		}
		if (shadowImageMemory_ != VK_NULL_HANDLE)
		{
			vkFreeMemory(device_, shadowImageMemory_, nullptr);
			shadowImageMemory_ = VK_NULL_HANDLE;
		}
		if (shadowRenderPass_ != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(device_, shadowRenderPass_, nullptr);
			shadowRenderPass_ = VK_NULL_HANDLE;
		}
		if (renderPass_ != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(device_, renderPass_, nullptr);
			renderPass_ = VK_NULL_HANDLE;
		}

		// Descriptors
		if (descriptorPool_ != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
			descriptorPool_ = VK_NULL_HANDLE;
		}
		if (textureSetLayout_ != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device_, textureSetLayout_, nullptr);
			textureSetLayout_ = VK_NULL_HANDLE;
		}
		if (volumeSetLayout_ != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device_, volumeSetLayout_, nullptr);
			volumeSetLayout_ = VK_NULL_HANDLE;
		}
		if (descriptorSetLayout_ != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
			descriptorSetLayout_ = VK_NULL_HANDLE;
		}

		// Device
		if (device_ != VK_NULL_HANDLE)
		{
			vkDestroyDevice(device_, nullptr);
			device_ = VK_NULL_HANDLE;
		}

		// Surface and instance
		if (surface_ != VK_NULL_HANDLE)
		{
			vkDestroySurfaceKHR(inst, surface_, nullptr);
			surface_ = VK_NULL_HANDLE;
		}

		if (debugMessenger_ != VK_NULL_HANDLE && inst != VK_NULL_HANDLE)
		{
			destroyDebugUtilsMessenger(inst, debugMessenger_, nullptr);
			debugMessenger_ = VK_NULL_HANDLE;
		}

		if (inst != VK_NULL_HANDLE)
		{
			vkDestroyInstance(inst, nullptr);
			inst = VK_NULL_HANDLE;
		}

		// Window
#ifdef LVG_WITH_UI
		// Must run while window_ is still valid: ~UiPlatformGlfw() restores whatever
		// GLFW callbacks were installed before it.
		uiPlatform_.reset();
#endif
		if (window_ != nullptr)
		{
			glfwDestroyWindow(window_);
			window_ = nullptr;
		}
		if (manageGlfwLifecycle_ && glfwLifecycleAcquired_)
		{
			std::lock_guard<std::mutex> lock(glfwLifecycleMutex);
			if (glfwLifecycleReferenceCount > 0)
			{
				--glfwLifecycleReferenceCount;
				if (glfwLifecycleReferenceCount == 0)
				{
					glfwTerminate();
				}
			}
			glfwLifecycleAcquired_ = false;
		}
	}

	// -----------------------------
	// Window + Input
	// -----------------------------

	void VkApp::createWindow(int w, int h, const char* title)
	{
		if (manageGlfwLifecycle_)
		{
			std::lock_guard<std::mutex> lock(glfwLifecycleMutex);
			if (glfwLifecycleReferenceCount == 0 && !glfwInit())
			{
				throw std::runtime_error("GLFW init failed");
			}
			++glfwLifecycleReferenceCount;
			glfwLifecycleAcquired_ = true;
		}
		else
		{
			logMessage(LogLevel::Debug,
			           "GLFW lifecycle is externally managed; assuming GLFW is already initialized");
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

		window_ = glfwCreateWindow(w, h, title, nullptr, nullptr);
		if (!window_)
		{
			throw std::runtime_error("GLFW window creation failed");
		}

		// Attach this to the window so static callbacks can forward to instance methods
		glfwSetWindowUserPointer(window_, this);

		// Register callbacks
		glfwSetFramebufferSizeCallback(window_, framebufferResizeCallback);
		glfwSetMouseButtonCallback(window_, mouseButtonCallback);
		glfwSetCursorPosCallback(window_, cursorPosCallback);
		glfwSetScrollCallback(window_, scrollCallback);

#ifdef LVG_WITH_UI
		// UiPlatformGlfw installs on top of the callbacks just registered above and
		// chains to them unconditionally (docs/gui/04, "GLFW plumbing"), so the camera
		// still sees every event even when the GUI also consumes it. It does not touch
		// glfwSetWindowUserPointer, which this class already owns for its own
		// trampolines.
		uiPlatform_ = std::make_unique<ui::UiPlatformGlfw>();
		uiPlatform_->installCallbacks(window_);
#endif
	}

	// -----------------------------
	// Init chain
	// -----------------------------

	void VkApp::initVulkan()
	{
		createInstance();
		setupDebugMessenger();
		createSurface();
		pickPhysicalDevice();
		createLogicalDevice();
	}


	// -----------------------------
	// Main loop + per-frame
	// -----------------------------

	void VkApp::mainLoop()
	{
		while (!glfwWindowShouldClose(window_))
		{
			glfwPollEvents();

			double now = glfwGetTime();
			float dt = static_cast<float>(now - prevTime_);
			prevTime_ = now;

#ifdef LVG_WITH_UI
			if (uiPlatform_)
			{
				int winW = 0, winH = 0;
				glfwGetWindowSize(window_, &winW, &winH);
				// GUI stays in logical pixels throughout (docs/gui/04); only the x scale
				// is used, since GLFW reports x == y on every platform this project
				// targets.
				float xscale = 1.0f, yscale = 1.0f;
				glfwGetWindowContentScale(window_, &xscale, &yscale);
				(void) yscale;
				const ui::Vec2 displaySize{ static_cast<float>(winW), static_cast<float>(winH) };
				uiPlatform_->beginFrame(displaySize, xscale, dt);

				if (guiContext_)
				{
					// docs/gui/01-architecture.md per-frame sequence, steps 1-3:
					// uiPlatform_ already turned this frame's raw GLFW callbacks into a
					// resolved InputState; forwardInputToGui() replays that into
					// guiContext_ (which cannot receive GLFW callbacks itself -- see
					// VkApp.h) before guiContext_ does its own beginFrame()/update().
					forwardInputToGui();
					uiLastDisplaySize_ = displaySize;
					guiContext_->beginFrame(displaySize, xscale, dt);
					guiContext_->update();

					if (uiRenderer_ && guiContext_->atlasNeedsRebuild())
					{
						uiRenderer_->rebuildAtlas(guiContext_->font());
						guiContext_->acknowledgeAtlasRebuild();
					}
				}
			}
#endif

			// docs/gui/04, "The camera hand-off": keyboard camera control is skipped
			// entirely whenever the GUI wants the keyboard (a focused TextBox, etc).
#ifdef LVG_WITH_UI
			const bool uiHasKeyboard = uiWantsKeyboard();
#else
			const bool uiHasKeyboard = false;
#endif
			if (keyboardCameraEnabled_ && !uiHasKeyboard)
			{
				updateCameraFromKeyboard(dt);
			}

			handleRiggedAnimationInput();

			// Call physics update callback if set
			if (updateCallback_)
			{
				updateCallback_(dt);
			}

			sceneGraph_->updateWorldTransforms();
			sceneGraph_->syncToRenderer();

#ifdef LVG_WITH_UI
			// docs/gui/01-architecture.md step 5: layout + build this frame's DrawList,
			// which recordUi() (called from inside drawFrame()'s recordCommandBuffer)
			// reads via guiContext_->drawList().
			if (guiContext_)
			{
				guiContext_->endFrame();
			}
#endif

			drawFrame();

#ifdef LVG_WITH_UI
			if (uiPlatform_)
			{
				uiPlatform_->endFrame();
			}
#endif
		}
	}

	void VkApp::drawFrame()
	{
		assertOwnerThread("drawFrame");

		// Basic sanity checks to catch mismatches early
		if (commandBuffers_.size() != swapChainImages_.size())
		{
			throw std::runtime_error("drawFrame: commandBuffers_ count does not match swapchain images");
		}

		if (currentFrame_ >= imageAvailableSemaphores_.size() ||
			currentFrame_ >= renderFinishedSemaphores_.size() ||
			currentFrame_ >= inFlight_.size())
		{
			throw std::runtime_error("drawFrame: currentFrame_ out of sync object bounds");
		}

		// Wait for GPU to finish with this frame
		VkResult resWait = vkWaitForFences(device_, 1, &inFlight_[currentFrame_], VK_TRUE, UINT64_MAX);
		if (resWait != VK_SUCCESS)
		{
			throw std::runtime_error("drawFrame: vkWaitForFences failed");
		}

		uint32_t imageIndex = 0;
		VkResult result = vkAcquireNextImageKHR(
			device_,
			swapChain_,
			UINT64_MAX,
			imageAvailableSemaphores_[currentFrame_],
			VK_NULL_HANDLE,
			&imageIndex
		);

		// Handle resize/out-of-date early
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			recreateSwapChain();
			return;
		}
		else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		{
			throw std::runtime_error("drawFrame: failed to acquire swap chain image");
		}

		// If this image is already being used by a previous frame, wait for it
		if (imagesInFlight_.size() != swapChainImages_.size())
		{
			imagesInFlight_.assign(swapChainImages_.size(), VK_NULL_HANDLE);
		}
		if (imagesInFlight_[imageIndex] != VK_NULL_HANDLE)
		{
			vkWaitForFences(device_, 1, &imagesInFlight_[imageIndex], VK_TRUE, UINT64_MAX);
		}
		imagesInFlight_[imageIndex] = inFlight_[currentFrame_];

		// All CPU writes to GPU-visible frame resources happen only after the
		// relevant frame/image fences have completed.
		updateInstanceDataOptimized();
		updateRiggedInstances();

		// We're going to submit work that uses this frame's fence, so reset it
		VkResult resReset = vkResetFences(device_, 1, &inFlight_[currentFrame_]);
		if (resReset != VK_SUCCESS)
		{
			throw std::runtime_error("drawFrame: vkResetFences failed");
		}

		// Update UBO for this image
		if (imageIndex >= swapChainImages_.size())
		{
			throw std::runtime_error("drawFrame: imageIndex out of range");
		}
		updateUniformBuffer(imageIndex);

		// If you record per-frame, (re)record here
		recordCommandBuffer(commandBuffers_[imageIndex], imageIndex);

		// Submit
		VkSemaphore waitSemaphores[] = { imageAvailableSemaphores_[currentFrame_] };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSemaphore signalSemaphores[] = { renderFinishedSemaphores_[currentFrame_] };

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers_[imageIndex];
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		VkResult resSubmit = vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlight_[currentFrame_]);
		if (resSubmit == VK_ERROR_DEVICE_LOST)
		{
			throw std::runtime_error("drawFrame: VK_ERROR_DEVICE_LOST on vkQueueSubmit");
		}
		else if (resSubmit != VK_SUCCESS)
		{
			throw std::runtime_error("drawFrame: vkQueueSubmit failed");
		}

		// Present
		VkSwapchainKHR swapChains[] = { swapChain_ };

		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapChains;
		presentInfo.pImageIndices = &imageIndex;

		result = vkQueuePresentKHR(presentQueue_, &presentInfo);

		// Handle window changes
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized_.load())
		{
			framebufferResized_.store(false);
			recreateSwapChain();
		}

		// Advance frame index
		currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	// -----------------------------
	// Swapchain lifecycle
	// -----------------------------
	// -----------------------------
	// Input + Camera
	// -----------------------------
	glm::vec3 VkApp::camForward() const
	{
		float cy = cos(glm::radians(camera_.yaw)),   sy = sin(glm::radians(camera_.yaw));
		float cp = cos(glm::radians(camera_.pitch)), sp = sin(glm::radians(camera_.pitch));
		return glm::normalize(glm::vec3(cy*cp, sp, sy*cp));
	}

	void VkApp::updateCameraFromKeyboard(float dtSeconds)
	{
		if (!window_)
		{
			return;
		}

		glm::vec3 dir;
		dir.x = cos(glm::radians(camera_.yaw)) * cos(glm::radians(camera_.pitch));
		dir.y = sin(glm::radians(camera_.pitch));
		dir.z = sin(glm::radians(camera_.yaw)) * cos(glm::radians(camera_.pitch));

		glm::vec3 front = glm::normalize(dir);
		glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0,1,0)));
		glm::vec3 up    = glm::normalize(glm::cross(right, front));

		float speed = (glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 4.0f : 1.0f;

		if (orbitEnabled_)
		{
			// Pan target with WASD/QE while orbiting
			glm::vec3 f = camForward();
			glm::vec3 r = glm::normalize(glm::cross(f, glm::vec3(0,1,0)));
			glm::vec3 u = glm::normalize(glm::cross(r, f));
			if (glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS) orbitTarget_ -= r*speed*dtSeconds;
			if (glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS) orbitTarget_ += r*speed*dtSeconds;
				if (glfwGetKey(window_, GLFW_KEY_Q) == GLFW_PRESS) orbitTarget_ -= u*speed*dtSeconds;
				if (glfwGetKey(window_, GLFW_KEY_E) == GLFW_PRESS) orbitTarget_ += u*speed*dtSeconds;

				float az = glm::radians(orbitAzimuthDeg_);
				float el = glm::radians(orbitElevationDeg_);
				glm::vec3 orbitDir(cos(az)*cos(el), sin(el), sin(az)*cos(el));
				camera_.position = orbitTarget_ - glm::normalize(orbitDir) * orbitRadius_;
			}
			else
			{
			if (glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS)
				camera_.position += camForward()*speed*dtSeconds;
			if (glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS)
				camera_.position -= camForward()*speed*dtSeconds;
			if (glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS)
				camera_.position -= right*speed*dtSeconds;
			if (glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS)
				camera_.position += right*speed*dtSeconds;
			if (glfwGetKey(window_, GLFW_KEY_Q) == GLFW_PRESS)
				camera_.position -= up*speed*dtSeconds;
			if (glfwGetKey(window_, GLFW_KEY_E) == GLFW_PRESS)
				camera_.position += up*speed*dtSeconds;
		}

		// Rendering mode controls
		if (glfwGetKey(window_, GLFW_KEY_1) == GLFW_PRESS)
			setRenderMode(RenderMode::FLEXIBLE_SHAPES);
		if (glfwGetKey(window_, GLFW_KEY_2) == GLFW_PRESS)
			setRenderMode(RenderMode::WIREFRAME);
		if (glfwGetKey(window_, GLFW_KEY_3) == GLFW_PRESS)
			setRenderMode(RenderMode::UNLIT);
		if (glfwGetKey(window_, GLFW_KEY_4) == GLFW_PRESS)
			setRenderMode(RenderMode::ORIGINAL_SPHERES);
	}

	void VkApp::updateUniformBuffer(uint32_t imageIndex)
	{
		//std::cout << "VkApp::updateUniformBuffer " << imageIndex << std::endl;
		// Guard against bad indices (can happen if descriptor/UBO counts mismatch swapchain images)
		if (imageIndex >= uniformBuffersMapped_.size())
		{
			throw std::runtime_error("updateUniformBuffer: imageIndex out of range");
		}

		// Guard against zero height during resize minimize
		if (swapChainExtent_.height == 0)
		{
			return;
		}

		UniformBufferObject ubo{};
		float aspect = swapChainExtent_.width / static_cast<float>(swapChainExtent_.height);

		ubo.model = glm::mat4(1.0f);
		ubo.view  = camera_.view();
		ubo.proj  = camera_.proj(aspect);

		// Persistently mapped write
		void* dst = uniformBuffersMapped_[imageIndex];
		if (!dst)
		{
			throw std::runtime_error("updateUniformBuffer: mapped pointer is null");
		}

		std::memcpy(dst, &ubo, sizeof(ubo));
		updateLightingBuffer(imageIndex);
		//std::cout << "END VkApp::updateUniformBuffer " << imageIndex << std::endl;
	}

	// -----------------------------
	// GLFW static callbacks -> instance
	// -----------------------------

	void VkApp::framebufferResizeCallback(GLFWwindow* window, int width, int height)
	{
		auto app = reinterpret_cast<VkApp*>(glfwGetWindowUserPointer(window));
		if (app)
		{
			app->onFramebufferResize(width, height);
		}
	}

	void VkApp::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
	{
		auto app = reinterpret_cast<VkApp*>(glfwGetWindowUserPointer(window));
		if (app)
		{
			app->onMouseButton(button, action, mods);
		}
	}

	void VkApp::cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
	{
		auto app = reinterpret_cast<VkApp*>(glfwGetWindowUserPointer(window));
		if (app)
		{
			app->onCursorPos(xpos, ypos);
		}
	}

	void VkApp::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
	{
		auto app = reinterpret_cast<VkApp*>(glfwGetWindowUserPointer(window));
		if (app)
		{
			app->onScroll(xoffset, yoffset);
		}
	}

	// -----------------------------
	// Instance-side handlers
	// -----------------------------

	void VkApp::onFramebufferResize(int width, int height)
	{
		(void) width;
		(void) height;
		framebufferResized_.store(true);
	}

	void VkApp::onMouseButton(int button, int action, int mods)
	{
		(void) mods;
		if (button == GLFW_MOUSE_BUTTON_RIGHT)
		{
			if (action == GLFW_PRESS)
			{
				// docs/gui/04, "The camera hand-off": do not begin a camera drag if the
				// GUI wants this press. A release is handled unconditionally below --
				// once a drag is in progress it must not get stuck in look mode just
				// because the cursor swept over a panel before releasing.
#ifdef LVG_WITH_UI
				if (uiWantsMouse())
				{
					return;
				}
#endif
				mouseLook_ = true;
				firstMouse_ = true;
				glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			}
			else if (action == GLFW_RELEASE)
			{
				mouseLook_ = false;
				glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
			}
		}
	}

	void VkApp::onCursorPos(double xpos, double ypos)
	{
		if (!mouseLook_)
		{
			firstMouse_ = true;
			return;
		}

		if (firstMouse_)
		{
			lastX_ = xpos;
			lastY_ = ypos;
			firstMouse_ = false;
			return;
		}

		float dx = static_cast<float>(xpos - lastX_);
		float dy = static_cast<float>(ypos - lastY_);

		lastX_ = xpos;
		lastY_ = ypos;

		if (orbitEnabled_)
		{
			// Orbit rotation around target with right mouse drag
			orbitAzimuthDeg_   += dx * orbitRotateSens_;
			orbitElevationDeg_ -= dy * orbitRotateSens_;
			orbitElevationDeg_ = glm::clamp(orbitElevationDeg_, -89.0f, 89.0f);

			float az = glm::radians(orbitAzimuthDeg_);
			float el = glm::radians(orbitElevationDeg_);
			glm::vec3 dir(cos(az)*cos(el), sin(el), sin(az)*cos(el));
			camera_.position = orbitTarget_ - glm::normalize(dir) * orbitRadius_;
			camera_.yaw = orbitAzimuthDeg_ - 90.0f;  // align yaw with azimuth
			camera_.pitch = orbitElevationDeg_;
		}
		else
		{
			camera_.addMouseDelta(dx, dy);
		}
	}

	void VkApp::onScroll(double xoffset, double yoffset)
	{
		(void) xoffset;
		// docs/gui/04, "The camera hand-off": if the cursor is over a scrollable panel
		// the wheel scrolls the panel, not the camera. wantsMouse() already covers this
		// since it is true whenever the cursor is over a panel.
#ifdef LVG_WITH_UI
		if (uiWantsMouse())
		{
			return;
		}
#endif
		if (orbitEnabled_)
		{
			// Dolly in/out
			orbitRadius_ -= static_cast<float>(yoffset) * orbitDollySens_;
			orbitRadius_ = std::max(0.1f, orbitRadius_);

			float az = glm::radians(orbitAzimuthDeg_);
			float el = glm::radians(orbitElevationDeg_);
			glm::vec3 dir(cos(az)*cos(el), sin(el), sin(az)*cos(el));
			camera_.position = orbitTarget_ - glm::normalize(dir) * orbitRadius_;
		}
		else
		{
			camera_.addScroll(static_cast<float>(yoffset));
		}
	}

	// -----------------------------
	// Sync objects
	// -----------------------------

	void VkApp::createSyncObjects()
	{
		imageAvailableSemaphores_.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		renderFinishedSemaphores_.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		inFlight_.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);

		imagesInFlight_.clear();
		imagesInFlight_.resize(swapChainImages_.size(), VK_NULL_HANDLE);

		VkSemaphoreCreateInfo semInfo{};
		semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			if (vkCreateSemaphore(device_, &semInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
				vkCreateSemaphore(device_, &semInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
				vkCreateFence(device_, &fenceInfo, nullptr, &inFlight_[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to create sync objects");
			}
		}
	}

	// -----------------------------
	// Vulkan creation chain stubs
	// Paste your existing code into these
	// -----------------------------

	void VkApp::createInstance()
	{
		uint32_t extCount = 0;
		const char** reqExt = glfwGetRequiredInstanceExtensions(&extCount);
		std::vector<const char*> exts(reqExt, reqExt+extCount);
		const auto availableLayers = enumerateInstanceLayers();
		const auto availableExtensions = enumerateInstanceExtensions();

		validationEnabled_ = false;
		if (shouldEnableValidationLayers())
		{
			const bool hasValidationLayer = hasInstanceLayer(availableLayers, kValidationLayers[0]);
			if (hasValidationLayer)
			{
				validationEnabled_ = true;
			}
			else
			{
				consoleErrorStream() << "Vulkan validation layer '" << kValidationLayers[0]
				                     << "' is unavailable; continuing without validation" << std::endl;
			}
		}

		const bool hasDebugUtils = hasInstanceExtension(availableExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#if defined(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) && \
    defined(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT) && \
    defined(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT)
		const bool hasValidationFeatures =
		    hasInstanceExtension(availableExtensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
#else
		const bool hasValidationFeatures = false;
#endif
		(void)hasValidationFeatures;

		if (validationEnabled_ && !hasDebugUtils)
		{
			consoleErrorStream() << "VK_EXT_debug_utils is unavailable; validation callback setup will be skipped"
			                     << std::endl;
		}
		if (validationEnabled_ && hasDebugUtils)
		{
			appendUniqueExtension(exts, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
		ai.apiVersion = VK_API_VERSION_1_1;

		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
		if (validationEnabled_ && hasDebugUtils)
		{
			populateDebugMessengerCreateInfo(debugCreateInfo);
		}

#if defined(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) && \
    defined(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT) && \
    defined(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT)
		VkValidationFeatureEnableEXT enabledValidationFeatures[] = {
			VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
			VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT
		};
		VkValidationFeaturesEXT validationFeatures{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
		if (validationEnabled_ && hasValidationFeatures)
		{
			appendUniqueExtension(exts, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
			validationFeatures.enabledValidationFeatureCount =
			    static_cast<uint32_t>(std::size(enabledValidationFeatures));
			validationFeatures.pEnabledValidationFeatures = enabledValidationFeatures;
			validationFeatures.pNext = (hasDebugUtils ? &debugCreateInfo : nullptr);
		}
#endif

		VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
		ii.pApplicationInfo = &ai;
		ii.enabledExtensionCount = (uint32_t)exts.size();
		ii.ppEnabledExtensionNames = exts.data();
		if (validationEnabled_)
		{
			ii.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
			ii.ppEnabledLayerNames = kValidationLayers.data();
#if defined(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) && \
    defined(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT) && \
    defined(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT)
			ii.pNext = hasValidationFeatures
			         ? static_cast<const void*>(&validationFeatures)
			         : (hasDebugUtils ? static_cast<const void*>(&debugCreateInfo) : nullptr);
#else
			ii.pNext = hasDebugUtils ? static_cast<const void*>(&debugCreateInfo) : nullptr;
#endif
		}

		VK_CHECK(vkCreateInstance(&ii, nullptr, &inst));
	}

	void VkApp::pickPhysicalDevice()
	{
		struct DeviceSupport
		{
			uint32_t graphicsFamily = 0;
			uint32_t presentFamily = 0;
			VkPhysicalDeviceFeatures features{};
			VkPhysicalDeviceProperties properties{};
			uint32_t surfaceFormatCount = 0;
			uint32_t presentModeCount = 0;
			int score = std::numeric_limits<int>::min();
		};

		uint32_t nPhys = 0;
		vkEnumeratePhysicalDevices(inst, &nPhys, nullptr);
		if (!nPhys)
			throw std::runtime_error("No Vulkan device");
		std::vector<VkPhysicalDevice> physList(nPhys); vkEnumeratePhysicalDevices(inst, &nPhys, physList.data());

		auto supports = [&](VkPhysicalDevice pd)->std::optional<DeviceSupport>
		{
			DeviceSupport support{};
			vkGetPhysicalDeviceProperties(pd, &support.properties);

			auto logCandidate = [&](const char* status, const std::string& reason, int score = std::numeric_limits<int>::min())
			{
				if (!debugOutput)
				{
					return;
				}

				std::ostringstream message;
				message << "[GPU] " << status << ": " << support.properties.deviceName
				        << " (" << physicalDeviceTypeName(support.properties.deviceType) << ")";
				if (!reason.empty())
				{
					message << " - " << reason;
				}
				if (score != std::numeric_limits<int>::min())
				{
					message << ", score=" << score;
				}
				logMessage(LogLevel::Debug, message.str());
			};

			uint32_t qCount=0; vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, nullptr);
			if (qCount == 0)
			{
				logCandidate("Rejected GPU", "no queue families");
				return std::nullopt;
			}

			std::vector<VkQueueFamilyProperties> qfp(qCount);
			vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, qfp.data());
			std::optional<uint32_t> g, p;
			for (uint32_t i = 0; i < qCount; ++i)
			{
				if (qfp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) g = i;
				VkBool32 present=VK_FALSE;
				vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface_, &present);
				if (present) p = i;
			}
			if (!g || !p)
			{
				logCandidate("Rejected GPU", "missing graphics or present queue");
				return std::nullopt;
			}

			const auto extensions = enumerateDeviceExtensions(pd);
			if (!hasDeviceExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
			{
				logCandidate("Rejected GPU", "missing VK_KHR_swapchain support");
				return std::nullopt;
			}

			vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface_, &support.surfaceFormatCount, nullptr);
			vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface_, &support.presentModeCount, nullptr);
			if (support.surfaceFormatCount == 0 || support.presentModeCount == 0)
			{
				std::ostringstream reason;
				reason << "insufficient surface support (formats=" << support.surfaceFormatCount
				       << ", presentModes=" << support.presentModeCount << ")";
				logCandidate("Rejected GPU", reason.str());
				return std::nullopt;
			}

			support.graphicsFamily = *g;
			support.presentFamily = *p;
			vkGetPhysicalDeviceFeatures(pd, &support.features);

			support.score = physicalDeviceTypeScore(support.properties.deviceType);
			if (support.graphicsFamily == support.presentFamily)
			{
				support.score += 100;
			}
			support.score += static_cast<int>(std::min<uint32_t>(
			    support.properties.limits.maxImageDimension2D / 1024u,
			    1024u));

			std::ostringstream acceptedReason;
			acceptedReason << "graphics queue=" << support.graphicsFamily
			               << ", present queue=" << support.presentFamily
			               << ", formats=" << support.surfaceFormatCount
			               << ", presentModes=" << support.presentModeCount;
			logCandidate("Candidate GPU", acceptedReason.str(), support.score);
			return support;
		};

		VkPhysicalDevice bestPhysicalDevice = VK_NULL_HANDLE;
		std::optional<DeviceSupport> bestSupport;
		for (auto pd : physList)
		{
			auto support = supports(pd);
			if (!support)
			{
				continue;
			}

			if (!bestSupport || support->score > bestSupport->score)
			{
				bestSupport = *support;
				bestPhysicalDevice = pd;
			}
		}
		if (!bestSupport || bestPhysicalDevice == VK_NULL_HANDLE)
			throw std::runtime_error("No suitable GPU with graphics, presentation, and swapchain support");

		physicalDevice_ = bestPhysicalDevice;
		physicalDeviceType_ = bestSupport->properties.deviceType;
		qFamGfx = bestSupport->graphicsFamily;
		qFamPresent = bestSupport->presentFamily;
		supportsNonSolidFill_ = bestSupport->features.fillModeNonSolid == VK_TRUE;
		supportsWideLines_ = bestSupport->features.wideLines == VK_TRUE;
		logSelectedPhysicalDeviceInfo(physicalDevice_);
	}

	void VkApp::createLogicalDevice()
	{
		float prio = 1.0f;
		std::vector<VkDeviceQueueCreateInfo> qcis;
		std::vector<uint32_t> uniqueFamilies = (qFamGfx==qFamPresent) ?
			std::vector<uint32_t>{qFamGfx} : std::vector<uint32_t>{qFamGfx,qFamPresent};
		for (uint32_t fam : uniqueFamilies)
		{
			VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
			qci.queueFamilyIndex = fam;
			qci.queueCount = 1;
			qci.pQueuePriorities = &prio;
			qcis.push_back(qci);
		}
		const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
		VkPhysicalDeviceFeatures enabledFeatures{};
		enabledFeatures.fillModeNonSolid = supportsNonSolidFill_ ? VK_TRUE : VK_FALSE;
		enabledFeatures.wideLines = supportsWideLines_ ? VK_TRUE : VK_FALSE;
		VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
		dci.queueCreateInfoCount = (uint32_t) qcis.size();
		dci.pQueueCreateInfos = qcis.data();
		dci.enabledExtensionCount = 1;
		dci.ppEnabledExtensionNames = devExts;
		dci.pEnabledFeatures = &enabledFeatures;
		if (validationEnabled_)
		{
			dci.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
			dci.ppEnabledLayerNames = kValidationLayers.data();
		}
		VK_CHECK(vkCreateDevice(physicalDevice_, &dci, nullptr, &device_));
		vkGetDeviceQueue(device_, qFamGfx, 0, &graphicsQueue_);
		vkGetDeviceQueue(device_, qFamPresent, 0, &presentQueue_);
	}

	void VkApp::make_sphere(float r, int stacks, int slices,
								std::vector<Vertex>& verts, std::vector<uint32_t>& idx)
	{
		verts.clear(); idx.clear();
		for (int y = 0; y <= stacks; ++y)
		{
			float v = float(y)/stacks;
			float phi = v * glm::pi<float>();
			float cp = cos(phi), sp = sin(phi);
			for (int x = 0; x <= slices; ++x)
			{
				float u = float(x)/slices;
				float th = u * glm::two_pi<float>();
				float ct = cos(th), st = sin(th);
				glm::vec3 p = r * glm::vec3(ct*sp, cp, st*sp);
				glm::vec3 n = glm::normalize(p);
				verts.push_back({p,n});
			}
		}
		auto id = [&](int y,int x){ return y*(slices+1)+x; };
		for (int y = 0; y < stacks; ++y)
		{
			for(int x = 0; x < slices; ++x)
			{
				uint32_t i0=id(y,x), i1=id(y+1,x), i2=id(y+1,x+1), i3=id(y,x+1);
				idx.push_back(i0); idx.push_back(i1); idx.push_back(i2);
				idx.push_back(i0); idx.push_back(i2); idx.push_back(i3);
			}
		}
	}

	MeshPtr VkApp::make_sphere(float r, int stacks, int slices)
	{
		auto mesh = std::make_shared<Mesh>();
		mesh->name = "sphere";
		mesh->shapeType = lightGraphics::ShapeType::SPHERE;
		make_sphere(r, stacks, slices, mesh->vertices, mesh->indices);
		return mesh;
	}

	void
	VkApp::makeLine(glm::vec3 const &a, glm::vec3 const &b, std::vector<Vertex> &verts, std::vector<uint32_t> &idx)
	{
		verts.clear();
		idx.clear();

		// Create a more visible line by adding some thickness and variation
		glm::vec3 direction = glm::normalize(b - a);
		glm::vec3 perpendicular = glm::cross(direction, glm::vec3(0, 1, 0));
		if (glm::length(perpendicular) < 0.1f) {
			perpendicular = glm::cross(direction, glm::vec3(1, 0, 0));
		}
		perpendicular = glm::normalize(perpendicular);

		float thickness = 0.05f;  // Small thickness for visibility

		// Create 4 vertices for a slightly thick line
		verts.push_back(Vertex{ a + perpendicular * thickness, direction });
		verts.push_back(Vertex{ a - perpendicular * thickness, direction });
		verts.push_back(Vertex{ b + perpendicular * thickness, direction });
		verts.push_back(Vertex{ b - perpendicular * thickness, direction });

		// Create line segments
		idx.push_back(0);
		idx.push_back(2);
		idx.push_back(1);
		idx.push_back(3);
		idx.push_back(0);
		idx.push_back(1);
		idx.push_back(2);
		idx.push_back(3);
	}

	MeshPtr VkApp::makeLine(glm::vec3 const &a, glm::vec3 const &b)
	{
		auto mesh = std::make_shared<Mesh>();
		mesh->name = "line";
		mesh->shapeType = lightGraphics::ShapeType::LINE;
		makeLine(a, b, mesh->vertices, mesh->indices);
		return mesh;
	}

	void
	VkApp::makeHexahedral(const glm::vec3& size, std::vector<Vertex> &verts, std::vector<uint32_t> &idx)
	{
		verts.clear();
		idx.clear();

		glm::vec3 const h = size * 0.5f;
		glm::vec3 const positions[8] =
		{
			{ -h.x, -h.y, -h.z }, { +h.x, -h.y, -h.z }, { +h.x, +h.y, -h.z }, { -h.x, +h.y, -h.z },
			{ -h.x, -h.y, +h.z }, { +h.x, -h.y, +h.z }, { +h.x, +h.y, +h.z }, { -h.x, +h.y, +h.z }
		};
		int const faces[6][4] =
		{
			{ 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 },
			{ 2, 3, 7, 6 }, { 0, 4, 7, 3 }, { 1, 2, 6, 5 }
		};

		for (int f = 0; f < 6; ++f)
		{
			glm::vec3 const v0 = positions[faces[f][0]];
			glm::vec3 const v1 = positions[faces[f][1]];
			glm::vec3 const v2 = positions[faces[f][2]];
			glm::vec3 const n = glm::normalize(glm::cross(v1 - v0, v2 - v0));
			uint32_t const base = static_cast<uint32_t>(verts.size());

			for (int i = 0; i < 4; ++i)
			{
				verts.push_back(Vertex{ positions[faces[f][i]], n });
			}

			idx.push_back(base + 0);
			idx.push_back(base + 1);
			idx.push_back(base + 2);

			idx.push_back(base + 0);
			idx.push_back(base + 2);
			idx.push_back(base + 3);
		}
	}

	void
	VkApp::makeHexahedral(float size, std::vector<Vertex> &verts, std::vector<uint32_t> &idx)
	{
		makeHexahedral(glm::vec3(size), verts, idx);
	}

	MeshPtr VkApp::makeHexahedral(float size)
	{
		auto mesh = std::make_shared<Mesh>();
		mesh->name = "hexahedral";
		mesh->shapeType = lightGraphics::ShapeType::HEX;
		makeHexahedral(size, mesh->vertices, mesh->indices);
		return mesh;
	}

	MeshPtr VkApp::makeHexahedral(const glm::vec3& size)
	{
		auto mesh = std::make_shared<Mesh>();
		mesh->name = "hexahedral";
		mesh->shapeType = lightGraphics::ShapeType::HEX;
		makeHexahedral(size, mesh->vertices, mesh->indices);
		return mesh;
	}

	void
	VkApp::makeCapsule(
		float radius,
		float halfHeight,
		int slices,
		int stacks,
		std::vector<Vertex> &verts,
		std::vector<uint32_t> &idx)
	{
		verts.clear();
		idx.clear();

		const int hemiStacks = std::max(1, stacks);
		const int ringVerts = slices + 1;
		const float twoPi = glm::two_pi<float>();

		auto appendRing = [&](float y, float r, const glm::vec3& center) -> uint32_t
		{
			uint32_t start = static_cast<uint32_t>(verts.size());
			for (int s = 0; s <= slices; ++s)
			{
				float theta = twoPi * static_cast<float>(s) / static_cast<float>(slices);
				float x = cos(theta) * r;
				float z = sin(theta) * r;
				glm::vec3 pos(x, y, z);
				glm::vec3 nrm = glm::normalize(pos - center);
				verts.push_back(Vertex{ pos, nrm });
			}
			return start;
		};

		// --- Top hemisphere (pole -> equator)
		glm::vec3 topCenter(0.0f, halfHeight, 0.0f);
		uint32_t topBase = static_cast<uint32_t>(verts.size());
		for (int stack = 0; stack <= hemiStacks; ++stack)
		{
			float v = static_cast<float>(stack) / static_cast<float>(hemiStacks);
			float phi = v * glm::half_pi<float>(); // 0 at pole, pi/2 at equator
			float r = sin(phi) * radius;
			float y = topCenter.y + cos(phi) * radius;
			appendRing(y, r, topCenter);
		}

		// Indices for the top hemisphere
		for (int stack = 0; stack < hemiStacks; ++stack)
		{
			uint32_t ring0 = topBase + stack * ringVerts;
			uint32_t ring1 = topBase + (stack + 1) * ringVerts;
			for (int s = 0; s < slices; ++s)
			{
				uint32_t i0 = ring0 + s;
				uint32_t i1 = ring1 + s;
				uint32_t i2 = ring1 + s + 1;
				uint32_t i3 = ring0 + s + 1;
				idx.push_back(i0); idx.push_back(i1); idx.push_back(i2);
				idx.push_back(i0); idx.push_back(i2); idx.push_back(i3);
			}
		}

		// --- Cylinder band connecting the hemispheres
		uint32_t topEquatorStart = topBase + hemiStacks * ringVerts;
		uint32_t bottomRingStart = appendRing(-halfHeight, radius, glm::vec3(0.0f, -halfHeight, 0.0f));
		for (int s = 0; s < slices; ++s)
		{
			uint32_t t0 = topEquatorStart + s;
			uint32_t t1 = topEquatorStart + s + 1;
			uint32_t b0 = bottomRingStart + s;
			uint32_t b1 = bottomRingStart + s + 1;
			idx.push_back(t0); idx.push_back(b0); idx.push_back(b1);
			idx.push_back(t0); idx.push_back(b1); idx.push_back(t1);
		}

		// --- Bottom hemisphere (equator -> pole)
		glm::vec3 bottomCenter(0.0f, -halfHeight, 0.0f);
		uint32_t prevRing = bottomRingStart;
		for (int stack = 1; stack <= hemiStacks; ++stack)
		{
			float v = static_cast<float>(stack) / static_cast<float>(hemiStacks);
			float phi = glm::half_pi<float>() + v * glm::half_pi<float>(); // pi/2 -> pi
			float r = sin(phi) * radius;
			float y = bottomCenter.y + cos(phi) * radius;
			uint32_t currRing = appendRing(y, r, bottomCenter);

			for (int s = 0; s < slices; ++s)
			{
				uint32_t i0 = prevRing + s;
				uint32_t i1 = currRing + s;
				uint32_t i2 = currRing + s + 1;
				uint32_t i3 = prevRing + s + 1;
				idx.push_back(i0); idx.push_back(i1); idx.push_back(i2);
				idx.push_back(i0); idx.push_back(i2); idx.push_back(i3);
			}

			prevRing = currRing;
		}
	}

	void
	VkApp::make_cone(float radius, float height, int slices,
					std::vector<Vertex>& verts, std::vector<uint32_t>& idx)
	{
		verts.clear();
		idx.clear();

		// Tip of the cone
		glm::vec3 tip(0.0f, height * 0.5f, 0.0f);

		// Center of base
		glm::vec3 baseCenter(0.0f, -height * 0.5f, 0.0f);

		// Base circle vertices
		for (int i = 0; i < slices; ++i)
		{
			float theta = glm::two_pi<float>() * i / slices;
			float x = radius * cos(theta);
			float z = radius * sin(theta);
			glm::vec3 pos(x, -height * 0.5f, z);
			glm::vec3 nrm = glm::normalize(glm::vec3(x, radius / height, z));
			verts.push_back(Vertex{ pos, nrm });
		}

		// Add tip vertex
		uint32_t tipIndex = static_cast<uint32_t>(verts.size());
		verts.push_back(Vertex{ tip, glm::vec3(0.0f, 1.0f, 0.0f) });

		// Add base center vertex
		uint32_t baseCenterIndex = static_cast<uint32_t>(verts.size());
		verts.push_back(Vertex{ baseCenter, glm::vec3(0.0f, -1.0f, 0.0f) });

		// Side faces
		for (int i = 0; i < slices; ++i)
		{
			uint32_t next = (i + 1) % slices;
			idx.push_back(tipIndex);
			idx.push_back(i);
			idx.push_back(next);
		}

		// Base faces
		for (int i = 0; i < slices; ++i)
		{
			uint32_t next = (i + 1) % slices;
			idx.push_back(baseCenterIndex);
			idx.push_back(next);
			idx.push_back(i);
		}
	}

	MeshPtr VkApp::make_cone(float radius, float height, int slices)
	{
		auto mesh = std::make_shared<Mesh>();
		mesh->name = "cone";
		mesh->shapeType = lightGraphics::ShapeType::CONE;
		make_cone(radius, height, slices, mesh->vertices, mesh->indices);
		return mesh;
	}

	void
	VkApp::makeArrow(float shaftRadius, float shaftLength, float headRadius, float headLength, int slices,
					std::vector<Vertex>& verts, std::vector<uint32_t>& idx)
	{
		verts.clear();
		idx.clear();

		// Arrow head (cone) - positioned at the top
		std::vector<Vertex> headVerts;
		std::vector<uint32_t> headIdx;
		make_cone(headRadius, headLength, slices, headVerts, headIdx);

		// Translate head so its base is at y = 0 and tip is at y = headLength
		for (Vertex& v : headVerts)
		{
			v.pos.y += headLength / 2.0f;
		}

		// Arrow shaft (cylinder) - positioned below the head
		std::vector<Vertex> shaftVerts;
		std::vector<uint32_t> shaftIdx;
		make_cylinder(shaftRadius, shaftLength, slices, shaftVerts, shaftIdx);

		// Translate shaft so its top is at y = 0 and bottom is at y = -shaftLength
		for (Vertex& v : shaftVerts)
		{
			v.pos.y -= shaftLength / 2.0f;
		}

		// Combine head first
		uint32_t headVertOffset = 0;
		verts.insert(verts.end(), headVerts.begin(), headVerts.end());
		for (uint32_t i : headIdx)
		{
			idx.push_back(i + headVertOffset);
		}

		// Combine shaft
		uint32_t shaftVertOffset = static_cast<uint32_t>(verts.size());
		verts.insert(verts.end(), shaftVerts.begin(), shaftVerts.end());
		for (uint32_t i : shaftIdx)
		{
			idx.push_back(i + shaftVertOffset);
		}
	}

	MeshPtr VkApp::makeArrow(float shaftRadius, float shaftLength, float headRadius, float headLength, int slices)
	{
		auto mesh = std::make_shared<Mesh>();
		mesh->name = "arrow";
		mesh->shapeType = lightGraphics::ShapeType::ARROW;
		makeArrow(shaftRadius, shaftLength, headRadius, headLength, slices, mesh->vertices, mesh->indices);
		return mesh;
	}

	void
	VkApp::make_cylinder(float radius, float height, int slices,
						std::vector<Vertex>& verts, std::vector<uint32_t>& idx)
	{
		verts.clear();
		idx.clear();

		float halfH = height * 0.5f;

		// Side vertices
		for (int i = 0; i <= slices; ++i)
		{
			float theta = glm::two_pi<float>() * i / slices;
			float x = cos(theta);
			float z = sin(theta);
			glm::vec3 nrm(x, 0.0f, z);

			glm::vec3 topPos = glm::vec3(x * radius, +halfH, z * radius);
			glm::vec3 botPos = glm::vec3(x * radius, -halfH, z * radius);

			verts.push_back(Vertex{ topPos, nrm });
			verts.push_back(Vertex{ botPos, nrm });
		}

		// Side indices (two triangles per quad)
		for (int i = 0; i < slices; ++i)
		{
			uint32_t top0 = i * 2;
			uint32_t bot0 = i * 2 + 1;
			uint32_t top1 = (i + 1) * 2;
			uint32_t bot1 = (i + 1) * 2 + 1;

			idx.push_back(top0);
			idx.push_back(top1);
			idx.push_back(bot0);

			idx.push_back(top1);
			idx.push_back(bot1);
			idx.push_back(bot0);
		}

		// Center vertices for caps
		uint32_t topCenterIdx = static_cast<uint32_t>(verts.size());
		verts.push_back(Vertex{ glm::vec3(0.0f, +halfH, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f) });

		uint32_t botCenterIdx = static_cast<uint32_t>(verts.size());
		verts.push_back(Vertex{ glm::vec3(0.0f, -halfH, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f) });

		// Top cap
		for (int i = 0; i < slices; ++i)
		{
			uint32_t top0 = i * 2;
			uint32_t top1 = ((i + 1) % (slices + 1)) * 2;
			idx.push_back(topCenterIdx);
			idx.push_back(top1);
			idx.push_back(top0);
		}

		// Bottom cap
		for (int i = 0; i < slices; ++i)
		{
			uint32_t bot0 = i * 2 + 1;
			uint32_t bot1 = ((i + 1) % (slices + 1)) * 2 + 1;
			idx.push_back(botCenterIdx);
			idx.push_back(bot0);
			idx.push_back(bot1);
		}
	}

	MeshPtr VkApp::make_cylinder(float radius, float height, int slices)
	{
		auto mesh = std::make_shared<Mesh>();
		mesh->name = "cylinder";
		mesh->shapeType = lightGraphics::ShapeType::CYLINDER;
		make_cylinder(radius, height, slices, mesh->vertices, mesh->indices);
		return mesh;
	}
	// ---------------- Object Management Functions ----------------

	// Physics update callback
	void VkApp::setUpdateCallback(std::function<void(float)> callback)
	{
		updateCallback_ = callback;
	}

	// ---------------- Camera control API ----------------
	void VkApp::setKeyboardCameraEnabled(bool enabled) { keyboardCameraEnabled_ = enabled; }
	void VkApp::setCameraPosition(const glm::vec3& pos) { camera_.position = pos; }
	void VkApp::moveCameraForward(float distance) { camera_.position += camForward()*distance; }
	void VkApp::moveCameraRight(float distance) {
		glm::vec3 f = camForward();
		glm::vec3 right = glm::normalize(glm::cross(f, glm::vec3(0,1,0)));
		camera_.position += right*distance;
	}
	void VkApp::moveCameraUp(float distance) { camera_.position += glm::vec3(0,1,0)*distance; }
	void VkApp::setCameraYawPitch(float yawDeg, float pitchDeg) { camera_.yaw = yawDeg; camera_.pitch = glm::clamp(pitchDeg, -89.0f, 89.0f); }
	void VkApp::addCameraYawPitch(float yawDeltaDeg, float pitchDeltaDeg) { camera_.addMouseDelta(yawDeltaDeg, -pitchDeltaDeg); }
	void VkApp::setCameraLookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up)
	{
		camera_.position = eye;
		glm::vec3 f = glm::normalize(target - eye);
		glm::vec3 r = glm::normalize(glm::cross(f, glm::normalize(up)));
		(void) r;

		float pitchRad = asinf(glm::clamp(f.y, -1.0f, 1.0f));
		float yawRad = atan2f(f.z, f.x); // yaw=0 points +X, positive toward +Z

		camera_.yaw = glm::degrees(yawRad);
		camera_.pitch = glm::degrees(pitchRad);
	}
	void VkApp::setCameraLookAtLevel(const glm::vec3& eye, const glm::vec3& target,
	                                 const glm::vec3& up)
	{
		glm::vec3 upDir = up;
		if (glm::length(upDir) < 0.001f)
		{
			upDir = glm::vec3(0.0f, 1.0f, 0.0f);
		}
		upDir = glm::normalize(upDir);

		float eyeAlongUp = glm::dot(eye, upDir);
		float targetAlongUp = glm::dot(target, upDir);
		glm::vec3 leveledEye = eye + upDir * (targetAlongUp - eyeAlongUp);

		setCameraLookAt(leveledEye, target, upDir);
	}

	void VkApp::setCameraFov(float fovDeg) { camera_.fov = glm::clamp(fovDeg, 20.0f, 90.0f); }
	void VkApp::setCameraPlanes(float zNear, float zFar) { camera_.zNear = zNear; camera_.zFar = zFar; }
	void VkApp::setCameraSensitivity(float sens) { camera_.sensitivity = sens; }
	glm::vec3 VkApp::getCameraForward() const { return camForward(); }
	glm::vec3 VkApp::getCameraRight() const { return glm::normalize(glm::cross(camForward(), glm::vec3(0,1,0))); }
	glm::vec3 VkApp::getCameraUp() const { return glm::normalize(glm::cross(getCameraRight(), getCameraForward())); }

	// Orbit camera API
	void VkApp::setOrbitEnabled(bool enabled) { orbitEnabled_ = enabled; }
	void VkApp::setOrbitTarget(const glm::vec3& target) { orbitTarget_ = target; }
	void VkApp::setOrbitRadius(float radius) { orbitRadius_ = std::max(0.1f, radius); }
	void VkApp::setOrbitAngles(float azimuthDeg, float elevationDeg) {
		orbitAzimuthDeg_ = azimuthDeg;
		orbitElevationDeg_ = glm::clamp(elevationDeg, -89.0f, 89.0f);
	}
	void VkApp::addOrbitAngles(float deltaAzimuthDeg, float deltaElevationDeg) {
		orbitAzimuthDeg_ += deltaAzimuthDeg;
		orbitElevationDeg_ = glm::clamp(orbitElevationDeg_ + deltaElevationDeg, -89.0f, 89.0f);
	}
	void VkApp::panOrbitTarget(float deltaRight, float deltaUp) {
		glm::vec3 r = getCameraRight();
		glm::vec3 u = getCameraUp();
		orbitTarget_ += r*deltaRight*orbitPanSens_ + u*deltaUp*orbitPanSens_;
	}
	void VkApp::dollyOrbitRadius(float deltaRadius) { setOrbitRadius(orbitRadius_ - deltaRadius*orbitDollySens_); }
	void VkApp::setOrbitSensitivities(float rotate, float pan, float dolly) {
		orbitRotateSens_ = rotate; orbitPanSens_ = pan; orbitDollySens_ = dolly;
	}

	// Helper function to create rotation quaternion from direction vector
	glm::quat VkApp::rotationFromDirection(const glm::vec3& direction, const glm::vec3& up)
	{
		(void) up;
		glm::vec3 normalizedDir = glm::normalize(direction);

		// Default cylinder orientation is along Y-axis (0, 1, 0)
		glm::vec3 defaultDir = glm::vec3(0, 1, 0);

		// If direction is already along Y-axis, no rotation needed
		if (glm::abs(glm::dot(normalizedDir, defaultDir)) > 0.99f)
		{
			return glm::quat(1, 0, 0, 0); // Identity quaternion
		}

		// If direction is opposite to Y-axis, rotate 180 degrees around X-axis
		if (glm::abs(glm::dot(normalizedDir, -defaultDir)) > 0.99f)
		{
			return glm::quat(0, 1, 0, 0); // 180 degrees around X-axis
		}

		// Calculate rotation axis (perpendicular to both vectors)
		glm::vec3 rotationAxis = glm::cross(defaultDir, normalizedDir);
		rotationAxis = glm::normalize(rotationAxis);

		// Calculate rotation angle
		float cosAngle = glm::dot(defaultDir, normalizedDir);
		float angle = glm::acos(glm::clamp(cosAngle, -1.0f, 1.0f));


		// Create quaternion from axis and angle
		return glm::angleAxis(angle, rotationAxis);
	}

	// Create cylinder connecting two points
	void VkApp::addCylinderBetweenPoints(const glm::vec3& pointA, const glm::vec3& pointB,
										float radius, const glm::vec4& color,
										const std::string& name, float mass)
	{
		glm::vec3 direction = pointB - pointA;
		float length = glm::length(direction);

		if (length < 0.001f) // Avoid division by zero
		{
			logMessage(LogLevel::Warning, "Points are too close for cylinder connection");
			return;
		}

		// Center point between the two spheres
		glm::vec3 center = (pointA + pointB) * 0.5f;

		// Create rotation to align cylinder with the direction
		glm::quat rotation = rotationFromDirection(direction, glm::vec3(0, 1, 0));

		// Scale: radius in X/Z, length in Y
		glm::vec3 size(radius, length, radius);

		addObject(lightGraphics::ShapeType::CYLINDER, center, size, color, rotation, name, mass);
	}

	// Create cylinder along a specific axis
	void VkApp::addCylinderAlongAxis(const glm::vec3& center, const glm::vec3& axis,
									float length, float radius, const glm::vec4& color,
									const std::string& name, float mass)
	{
		glm::vec3 normalizedAxis = glm::normalize(axis);

		// Create rotation to align cylinder with the axis
		glm::quat rotation = rotationFromDirection(normalizedAxis, glm::vec3(0, 1, 0));

		// Scale: radius in X/Z, length in Y
		glm::vec3 size(radius, length, radius);

		addObject(lightGraphics::ShapeType::CYLINDER, center, size, color, rotation, name, mass);
	}

	// Create cylinder connecting two spheres with automatic radius calculation
	void VkApp::addCylinderConnectingSpheres(const glm::vec3& sphereA, const glm::vec3& sphereB,
											float sphereRadiusA, float sphereRadiusB,
											const glm::vec4& color, const std::string& name, float mass)
	{
		glm::vec3 direction = sphereB - sphereA;
		float distance = glm::length(direction);

		if (distance < 0.001f)
		{
			logMessage(LogLevel::Warning, "Spheres are too close for cylinder connection");
			return;
		}

		// Calculate cylinder parameters
		float cylinderLength = distance - sphereRadiusA - sphereRadiusB;
		if (cylinderLength <= 0.0f)
		{
			logMessage(LogLevel::Warning, "Spheres overlap, cannot create connecting cylinder");
			return;
		}

		// Use average radius or smaller radius for the cylinder
		float cylinderRadius = std::min(sphereRadiusA, sphereRadiusB) * 0.3f;

		// Calculate cylinder center (midpoint between sphere surfaces)
		glm::vec3 normalizedDir = glm::normalize(direction);
		// Calculate the correct cylinder center as midpoint between sphere surfaces
		glm::vec3 sphereASurface = sphereA + normalizedDir * sphereRadiusA;
		glm::vec3 sphereBSurface = sphereB - normalizedDir * sphereRadiusB;
		glm::vec3 cylinderCenter = (sphereASurface + sphereBSurface) * 0.5f;

		// Create rotation to align cylinder with the direction
		glm::quat rotation = rotationFromDirection(direction, glm::vec3(0, 1, 0));

		// Scale: radius in X/Z, length in Y
		glm::vec3 size(cylinderRadius, cylinderLength, cylinderRadius);

		addObject(lightGraphics::ShapeType::CYLINDER, cylinderCenter, size, color, rotation, name, mass);
	}

	void VkApp::computeArrowTransform(const glm::vec3& tail, const glm::vec3& tip, float shaftRadius,
	                                   glm::vec3& outPosition, glm::quat& outRotation, glm::vec3& outScale)
	{
		// Matches the head/shaft proportions baked into generateAllShapeGeometry's
		// ARROW mesh (headRadius 0.3, headLength 0.6, shaftRadius 0.1, shaftLength 0.4
		// along +Y), so a caller-chosen shaftRadius and tail/tip pair reproduce it exactly.
		constexpr float kArrowMeshShaftRadius = 0.1f;
		constexpr float kArrowMeshShaftFraction = 0.4f;

		glm::vec3 direction = tip - tail;
		float length = glm::length(direction);
		if (length < 0.001f)
		{
			outPosition = tail;
			outRotation = glm::quat(1, 0, 0, 0);
			outScale = glm::vec3(0.0f);
			return;
		}

		glm::vec3 normalizedDir = direction / length;
		outRotation = rotationFromDirection(normalizedDir, glm::vec3(0, 1, 0));

		float radiusScale = shaftRadius / kArrowMeshShaftRadius;
		outScale = glm::vec3(radiusScale, length, radiusScale);

		// The mesh's local origin sits at the shaft/head boundary, not the tail.
		outPosition = tail + normalizedDir * (kArrowMeshShaftFraction * length);
	}

	// ==================== PERFORMANCE OPTIMIZATION METHODS ====================

	void VkApp::markObjectDirty(size_t index)
	{
		assertOwnerThread("markObjectDirty (via an object setter)");
		if (index >= dirtyObjects_.size())
		{
			dirtyObjects_.resize(_objects_.size(), false);
		}
		dirtyObjects_[index] = true;
		instanceDataDirty_ = true;
	}

	Instance VkApp::makeInstanceForObject(size_t index) const
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("makeInstanceForObject", index, _objects_.size()));
		}

		const auto& obj = _objects_[index];
		Instance instance{};
		instance.model = getObjectModelMatrix(index);
		instance.color = glm::vec3(obj.getColour());
		instance.shapeType = static_cast<float>(obj.getType());
		return instance;
	}

	void VkApp::clearObjectModelMatrixOverrideInternal(size_t index)
	{
		if (index < objectModelMatrixOverrides_.size())
		{
			objectModelMatrixOverrides_[index].reset();
		}
	}

	// ==================== ORIGINAL METHODS (NOW OPTIMIZED) ====================

	// ---------------- Additional Geometry Generation Functions ----------------

	void VkApp::make_cube(std::vector<Vertex>& verts, std::vector<uint32_t>& idx)
	{
		verts.clear();
		idx.clear();

		// Cube vertices (centered at origin, size 1x1x1)
		float vertices[] = {
			// Front face
			-0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
			0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
			0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
			-0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,

			// Back face
			-0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
			0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
			0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
			-0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,

			// Left face
			-0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
			-0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
			-0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
			-0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,

			// Right face
			0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
			0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
			0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
			0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,

			// Top face
			-0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
			0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
			0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
			-0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,

			// Bottom face
			-0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
			0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
			0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
			-0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f
		};

		uint32_t indices[] =
		{
			0, 1, 2,  2, 3, 0,    // Front
			4, 6, 5,  4, 7, 6,    // Back
			8, 9, 10, 10, 11, 8,  // Left
			12, 14, 13, 12, 15, 14, // Right
			16, 18, 17, 16, 19, 18, // Top
			20, 21, 22, 22, 23, 20  // Bottom
		};

		for (int i = 0; i < 24; ++i)
		{
			Vertex vertex;
			vertex.pos = glm::vec3(vertices[i*6], vertices[i*6+1], vertices[i*6+2]);
			vertex.nrm = glm::vec3(vertices[i*6+3], vertices[i*6+4], vertices[i*6+5]);
			verts.push_back(vertex);
		}

		for (int i = 0; i < 36; ++i)
		{
			idx.push_back(indices[i]);
		}
	}

	MeshPtr VkApp::make_cube()
	{
		auto mesh = std::make_shared<Mesh>();
		mesh->name = "cube";
		mesh->shapeType = lightGraphics::ShapeType::CUBE;
		make_cube(mesh->vertices, mesh->indices);
		return mesh;
	}

	void VkApp::make_arrow(float headRadius, float headLength, float shaftRadius, float shaftLength, int slices, std::vector<Vertex>& verts, std::vector<uint32_t>& idx)
	{
		verts.clear();
		idx.clear();

		// Arrow head (cone) - positioned at the top
		make_cone(headRadius, headLength, slices, verts, idx);

		// Translate head so its base is at y = 0 and tip is at y = headLength
		for (auto& vertex : verts)
		{
			vertex.pos.y += headLength / 2.0f;
		}

		// Arrow shaft (cylinder) - positioned below the head
		std::vector<Vertex> shaftVerts;
		std::vector<uint32_t> shaftIdx;
		make_cylinder(shaftRadius, shaftLength, slices, shaftVerts, shaftIdx);

		// Translate shaft so its top is at y = 0 and bottom is at y = -shaftLength
		for (auto& vertex : shaftVerts)
		{
			vertex.pos.y -= shaftLength / 2.0f;
		}

		// Offset indices for shaft
		uint32_t offset = static_cast<uint32_t>(verts.size());
		for (auto index : shaftIdx)
		{
			idx.push_back(index + offset);
		}

		// Add shaft vertices
		verts.insert(verts.end(), shaftVerts.begin(), shaftVerts.end());
	}

	MeshPtr VkApp::make_arrow(float headRadius, float headLength, float shaftRadius, float shaftLength, int slices)
	{
		auto mesh = std::make_shared<Mesh>();
		mesh->name = "arrow";
		mesh->shapeType = lightGraphics::ShapeType::ARROW;
		make_arrow(headRadius, headLength, shaftRadius, shaftLength, slices, mesh->vertices, mesh->indices);
		return mesh;
	}

	void VkApp::make_line(std::vector<Vertex>& verts, std::vector<uint32_t>& idx)
	{
		verts.clear();
		idx.clear();

		// Create a more visible line with multiple segments for better visibility
		// Line from -1 to 1 on X axis with some Y variation for visibility
		std::vector<glm::vec3> linePoints = {
			glm::vec3(-1.0f, -0.1f, 0.0f),  // Start point
			glm::vec3(-0.5f, 0.0f, 0.0f),   // Mid point 1
			glm::vec3(0.0f, 0.1f, 0.0f),    // Mid point 2
			glm::vec3(0.5f, 0.0f, 0.0f),    // Mid point 3
			glm::vec3(1.0f, -0.1f, 0.0f)    // End point
		};

		// Add vertices for each point
		for (const auto& point : linePoints)
		{
			Vertex vertex;
			vertex.pos = point;
			vertex.nrm = glm::vec3(0, 0, 1);  // Normal pointing up
			verts.push_back(vertex);
		}

		// Create line segments connecting consecutive points
		for (size_t i = 0; i < linePoints.size() - 1; ++i)
		{
			idx.push_back(static_cast<uint32_t>(i));
			idx.push_back(static_cast<uint32_t>(i + 1));
		}
	}

	MeshPtr VkApp::make_line()
	{
		auto mesh = std::make_shared<Mesh>();
		mesh->name = "line";
		mesh->shapeType = lightGraphics::ShapeType::LINE;
		make_line(mesh->vertices, mesh->indices);
		return mesh;
	}

	MeshPtr VkApp::make_line(const glm::vec3& a, const glm::vec3& b)
	{
		auto mesh = std::make_shared<Mesh>();
		mesh->name = "lineAB";
		mesh->shapeType = lightGraphics::ShapeType::LINE;
		makeLine(a, b, mesh->vertices, mesh->indices);
		return mesh;
	}

	void VkApp::make_hexahedral(std::vector<Vertex>& verts, std::vector<uint32_t>& idx)
	{
		makeHexahedral(glm::vec3(1.0f), verts, idx);
	}

	void VkApp::make_hexahedral(const glm::vec3& size, std::vector<Vertex>& verts, std::vector<uint32_t>& idx)
	{
		makeHexahedral(size, verts, idx);
	}

	MeshPtr VkApp::make_hexahedral(float size)
	{
		auto mesh = std::make_shared<Mesh>();
		mesh->name = "hexahedral";
		mesh->shapeType = lightGraphics::ShapeType::HEX;
		make_hexahedral(glm::vec3(size), mesh->vertices, mesh->indices);
		return mesh;
	}

	MeshPtr VkApp::make_hexahedral(const glm::vec3& size)
	{
		auto mesh = std::make_shared<Mesh>();
		mesh->name = "hexahedral";
		mesh->shapeType = lightGraphics::ShapeType::HEX;
		make_hexahedral(size, mesh->vertices, mesh->indices);
		return mesh;
	}

	void VkApp::generateAllShapeGeometry(std::vector<Vertex>& allVertices, std::vector<uint32_t>& allIndices)
	{
		allVertices.clear();
		allIndices.clear();

		// Generate geometry for each shape type
		std::vector<std::vector<Vertex>> shapeVertices(8); // 8 shape types
		std::vector<std::vector<uint32_t>> shapeIndices(8);

		// Generate sphere geometry
		make_sphere(0.5f, 16, 24, shapeVertices[0], shapeIndices[0]);

		// Generate cube geometry
		make_cube(shapeVertices[1], shapeIndices[1]);

		// Generate cone geometry
		make_cone(0.5f, 1.0f, 16, shapeVertices[2], shapeIndices[2]);

		// Generate cylinder geometry
		make_cylinder(0.5f, 1.0f, 16, shapeVertices[3], shapeIndices[3]);

		// Generate capsule geometry
		makeCapsule(0.5f, 0.5f, 16, 16, shapeVertices[4], shapeIndices[4]);

		// Generate arrow geometry
		make_arrow(0.3f, 0.6f, 0.1f, 0.4f, 16, shapeVertices[5], shapeIndices[5]);

		// Generate line geometry
		make_line(shapeVertices[6], shapeIndices[6]);

		// Generate hexahedral geometry
		make_hexahedral(shapeVertices[7], shapeIndices[7]);

		// Combine all geometries
		uint32_t vertexOffset = 0;
		for (int shapeType = 0; shapeType < 8; ++shapeType)
		{
			// Add vertices
			allVertices.insert(allVertices.end(), shapeVertices[shapeType].begin(), shapeVertices[shapeType].end());

			// Add indices with offset
			for (uint32_t index : shapeIndices[shapeType])
			{
				allIndices.push_back(index + vertexOffset);
			}

			vertexOffset += static_cast<uint32_t>(shapeVertices[shapeType].size());
		}

		if (debugOutput)
		{
			std::ostringstream message;
			message << "Generated geometry for all shapes. Total vertices: " << allVertices.size()
			        << ", Total indices: " << allIndices.size();
			logMessage(LogLevel::Debug, message.str());
		}
	}

	void VkApp::storeShapeGeometryOffsets()
	{
		shapeGeometries_.clear();
		shapeGeometries_.resize(8); // 8 shape types

		// Generate geometry for each shape type to calculate offsets
		std::vector<std::vector<Vertex>> tempVertices(8);
		std::vector<std::vector<uint32_t>> tempIndices(8);

		// Generate sphere geometry
		make_sphere(0.5f, 16, 24, tempVertices[0], tempIndices[0]);

		// Generate cube geometry
		make_cube(tempVertices[1], tempIndices[1]);

		// Generate cone geometry
		make_cone(0.5f, 1.0f, 16, tempVertices[2], tempIndices[2]);

		// Generate cylinder geometry
		make_cylinder(0.5f, 1.0f, 16, tempVertices[3], tempIndices[3]);

		// Generate capsule geometry
		makeCapsule(0.5f, 0.5f, 16, 16, tempVertices[4], tempIndices[4]);

		// Generate arrow geometry
		make_arrow(0.3f, 0.6f, 0.1f, 0.4f, 16, tempVertices[5], tempIndices[5]);

		// Generate line geometry
		make_line(tempVertices[6], tempIndices[6]);

		// Generate hexahedral geometry
		make_hexahedral(tempVertices[7], tempIndices[7]);

		// Calculate offsets
		uint32_t vertexOffset = 0;
		uint32_t indexOffset = 0;

		for (int i = 0; i < 8; ++i)
		{
			shapeGeometries_[i].vertexOffset = vertexOffset;
			shapeGeometries_[i].vertexCount = static_cast<uint32_t>(tempVertices[i].size());
			shapeGeometries_[i].indexOffset = indexOffset;
			shapeGeometries_[i].indexCount = static_cast<uint32_t>(tempIndices[i].size());

			vertexOffset += static_cast<uint32_t>(tempVertices[i].size());
			indexOffset += static_cast<uint32_t>(tempIndices[i].size());

			if (debugOutput)
			{
				std::ostringstream message;
				message << "Shape " << i << ": " << shapeGeometries_[i].vertexCount
				        << " vertices, " << shapeGeometries_[i].indexCount << " indices";
				logMessage(LogLevel::Debug, message.str());
			}
		}
	}


	void VkApp::recordShadowPass(VkCommandBuffer cmd)
	{
		if (!shadowRenderingEnabled_)
		{
			return;
		}

		if (shadowPipeline_ == VK_NULL_HANDLE ||
		    shadowPipelineLayout_ == VK_NULL_HANDLE ||
		    shadowFramebuffers_.empty())
		{
			return;
		}

		const size_t lightCount = std::min(lights_.size(), lightGraphics::MaxForwardLights);
		std::vector<size_t> shadowLightIndices;
		shadowLightIndices.reserve(lightCount);
		for (size_t lightIndex = 0; lightIndex < lightCount; ++lightIndex)
		{
			const LightSource light = lightForUpload(lightIndex);
			if (light.enabled &&
			    light.castsShadow &&
			    (light.type == LightType::Directional || light.type == LightType::Spot) &&
			    lightIndex < shadowFramebuffers_.size())
			{
				shadowLightIndices.push_back(lightIndex);
			}
		}

		if (shadowLightIndices.empty())
		{
			return;
		}

		const bool hasRegularObjects = !_objects_.empty() &&
		                               instanceCount_ > 0 &&
		                               vertexBuffer_ != VK_NULL_HANDLE &&
		                               indexBuffer_ != VK_NULL_HANDLE &&
		                               indexCount_ > 0 &&
		                               shapeGeometries_.size() >= 8;
		const bool hasRiggedObjects = !riggedInstances_.empty();
		if (!hasRegularObjects && !hasRiggedObjects)
		{
			return;
		}

		VkBuffer perFrameInstBuf = instanceBufs_[currentFrame_].buffer;
		VkBuffer regularInstanceBuffer = perFrameInstBuf != VK_NULL_HANDLE ? perFrameInstBuf : instanceBuf.buffer;
		const bool useRegularInstancing = hasRegularObjects && regularInstanceBuffer != VK_NULL_HANDLE;

		auto writeInstancesToBuffer = [&](VkDeviceSize offsetBytes, const Instance* instances, VkDeviceSize bytes)
		{
			if (bytes == 0 || instances == nullptr)
			{
				return;
			}
			if (perFrameInstBuf != VK_NULL_HANDLE)
			{
				void* mapped = instanceBufferMappedPerFrame_[currentFrame_];
				if (mapped)
				{
					std::memcpy(static_cast<char*>(mapped) + offsetBytes, instances, static_cast<size_t>(bytes));
					return;
				}

				void* ptrWrite = nullptr;
				VK_CHECK(vkMapMemory(device_, instanceBufs_[currentFrame_].memory, offsetBytes, bytes, 0, &ptrWrite));
				std::memcpy(ptrWrite, instances, static_cast<size_t>(bytes));
				vkUnmapMemory(device_, instanceBufs_[currentFrame_].memory);
				return;
			}

			void* ptrWrite = nullptr;
			VK_CHECK(vkMapMemory(device_, instanceBuf.memory, offsetBytes, bytes, 0, &ptrWrite));
			std::memcpy(ptrWrite, instances, static_cast<size_t>(bytes));
			vkUnmapMemory(device_, instanceBuf.memory);
		};

		std::vector<std::vector<size_t>> shapeGroups(8);
		if (useRegularInstancing)
		{
			for (size_t i = 0; i < _objects_.size(); ++i)
			{
				const int shapeType = static_cast<int>(_objects_[i].getType());
				if (shapeType < 0 || shapeType >= 8)
				{
					continue;
				}
				const std::string name = _objects_[i].getName();
				if (isDebugOverlayObjectName(name))
				{
					continue;
				}
				shapeGroups[shapeType].push_back(i);
			}
		}

		for (size_t lightIndex : shadowLightIndices)
		{
			VkClearValue clearValue{};
			clearValue.depthStencil = {1.0f, 0};

			VkRenderPassBeginInfo rpBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
			rpBegin.renderPass = shadowRenderPass_;
			rpBegin.framebuffer = shadowFramebuffers_[lightIndex];
			rpBegin.renderArea.offset = {0, 0};
			rpBegin.renderArea.extent = {shadowMapSize_, shadowMapSize_};
			rpBegin.clearValueCount = 1;
			rpBegin.pClearValues = &clearValue;

			vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);

			detail::ShadowPushConstants push{};
			// Reuse the matrix buildLightingBufferObject() already computed this
			// frame (it always runs earlier in the same frame — see drawFrame())
			// instead of recomputing it here too. Falls back to a direct call if
			// the cache is unexpectedly empty/undersized for this light.
			push.lightViewProj = lightIndex < cachedShadowMatrices_.size()
			    ? cachedShadowMatrices_[lightIndex]
			    : shadowMatrixForLight(lightIndex);
			vkCmdPushConstants(cmd,
			                   shadowPipelineLayout_,
			                   VK_SHADER_STAGE_VERTEX_BIT,
			                   0,
			                   sizeof(push),
			                   &push);

			if (useRegularInstancing)
			{
				std::array<VkBuffer, 2> buffers{vertexBuffer_, regularInstanceBuffer};
				std::array<VkDeviceSize, 2> offsets{0, 0};
				vkCmdBindVertexBuffers(cmd, 0, 2, buffers.data(), offsets.data());
				vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);

				uint32_t runningFirst = 0;
				for (int shapeType = 0; shapeType < 8; ++shapeType)
				{
					if (shapeGroups[shapeType].empty())
					{
						continue;
					}

					const auto& shapeGeo = shapeGeometries_[shapeType];
					const uint32_t countForShape = static_cast<uint32_t>(shapeGroups[shapeType].size());
					std::vector<Instance> tmp(countForShape);
					for (uint32_t k = 0; k < countForShape; ++k)
					{
						tmp[k] = makeInstanceForObject(shapeGroups[shapeType][k]);
					}
					const VkDeviceSize offsetBytes = sizeof(Instance) * runningFirst;
					const VkDeviceSize bytes = sizeof(Instance) * countForShape;
					writeInstancesToBuffer(offsetBytes, tmp.data(), bytes);

					vkCmdDrawIndexed(cmd,
					                 shapeGeo.indexCount,
					                 countForShape,
					                 shapeGeo.indexOffset,
					                 0,
					                 runningFirst);
					runningFirst += countForShape;
				}
			}

			for (const auto& riggedInstance : riggedInstances_)
			{
				const detail::Buffer& frameInstanceBuffer = riggedInstance.instanceBuffers[currentFrame_];
				if (frameInstanceBuffer.buffer == VK_NULL_HANDLE)
				{
					continue;
				}

				for (const auto& meshData : riggedInstance.meshes)
				{
					const detail::Buffer& frameVertexBuffer = meshData.vertexBuffers[currentFrame_];
					if (frameVertexBuffer.buffer == VK_NULL_HANDLE ||
					    meshData.indexBuffer.buffer == VK_NULL_HANDLE ||
					    meshData.indexCount == 0)
					{
						continue;
					}

					std::array<VkBuffer, 2> buffers{frameVertexBuffer.buffer, frameInstanceBuffer.buffer};
					std::array<VkDeviceSize, 2> offsets{0, 0};
					vkCmdBindVertexBuffers(cmd, 0, 2, buffers.data(), offsets.data());
					vkCmdBindIndexBuffer(cmd, meshData.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
					vkCmdDrawIndexed(cmd, meshData.indexCount, 1, 0, 0, 0);
				}
			}

			vkCmdEndRenderPass(cmd);
		}
	}

	void VkApp::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex)
	{
		if (imageIndex >= swapChainFramebuffers_.size())
		{
			throw std::runtime_error("recordCommandBuffer: imageIndex out of range");
		}

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
		{
			throw std::runtime_error("recordCommandBuffer: vkBeginCommandBuffer failed");
		}

		recordShadowPass(cmd);

		// Clear values: color + depth
		std::array<VkClearValue, 2> clearValues{};
		clearValues[0].color = { { 0.0f, 0.05f, 0.08f, 1.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo rpBegin{};
		rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass = renderPass_;
		rpBegin.framebuffer = swapChainFramebuffers_[imageIndex];
		rpBegin.renderArea.offset = { 0, 0 };
		rpBegin.renderArea.extent = swapChainExtent_;
		rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
		rpBegin.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

		// If your pipeline uses dynamic viewport/scissor, set them here.
		// Make sure createGraphicsPipeline() enabled VK_DYNAMIC_STATE_VIEWPORT/SCISSOR.
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(swapChainExtent_.width);
		viewport.height = static_cast<float>(swapChainExtent_.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = swapChainExtent_;
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		// Bind pipeline (use current rendering mode)
		VkPipeline currentPipeline = getCurrentPipeline();
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline);

		// Bind descriptor set for this swapchain image (UBO with view/proj)
		if (!descriptorSets_.empty())
		{
			if (imageIndex >= descriptorSets_.size())
			{
				throw std::runtime_error("recordCommandBuffer: descriptorSets_ size mismatch");
			}
			vkCmdBindDescriptorSets(
				cmd,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipelineLayout_,
				0,
				1,
				&descriptorSets_[imageIndex],
				0,
				nullptr
			);
		}

		// Bind vertex and (optional) instance buffers
		std::array<VkBuffer, 2> vbs{};
		std::array<VkDeviceSize, 2> offs{};
		uint32_t vbCount = 0;

		if (vertexBuffer_ != VK_NULL_HANDLE)
		{
			vbs[vbCount] = vertexBuffer_;
			offs[vbCount] = 0;
			vbCount++;
		}

		// Bind per-frame instance buffer if available
		VkBuffer perFrameInstBuf = instanceBufs_[currentFrame_].buffer;
		bool useInstancing = ((perFrameInstBuf != VK_NULL_HANDLE || instanceBuffer_ != VK_NULL_HANDLE) && instanceCount_ > 0);
		const bool hasRegularObjects = !_objects_.empty() && instanceCount_ > 0;
		if (useInstancing)
		{
			vbs[vbCount] = (perFrameInstBuf != VK_NULL_HANDLE) ? perFrameInstBuf : instanceBuffer_;
			offs[vbCount] = 0;
			vbCount++;
		}

		bool hasVisibleVolume = false;
		for (const VolumeResource& volume : volumes_)
		{
			hasVisibleVolume = hasVisibleVolume || (volume.alive && volume.visible);
		}
		if (!hasRegularObjects && riggedInstances_.empty() &&
			meshDrawRequests_.empty() && !hasVisibleVolume)
		{
			// Nothing in the scene to draw -- but the GUI still has to be recorded, or
			// it would vanish exactly when the 3D scene is empty.
#ifdef LVG_WITH_UI
			recordUi(cmd);
#endif
			vkCmdEndRenderPass(cmd);
			if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
			{
				throw std::runtime_error("recordCommandBuffer: vkEndCommandBuffer failed");
			}
			return;
		}

		if (vbCount > 0)
		{
			vkCmdBindVertexBuffers(cmd, 0, vbCount, vbs.data(), offs.data());
		}

		// Bind index buffer if present
		bool indexed = (indexBuffer_ != VK_NULL_HANDLE && indexCount_ > 0);
		if (indexed)
		{
			vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);
		}

		// Draw each object with its specific geometry
		std::vector<std::vector<size_t>> overlayShapeGroups(8); // debug overlay shapes (drawn on top)
		uint32_t overlayBaseFirstInstance = 0;
		bool hasOverlayObjects = false;

		auto writeInstancesToBuffer = [&](VkDeviceSize offsetBytes, const Instance* instances, VkDeviceSize bytes)
		{
			if (perFrameInstBuf != VK_NULL_HANDLE)
			{
				void* mapped = instanceBufferMappedPerFrame_[currentFrame_];
				if (mapped)
				{
					std::memcpy(static_cast<char*>(mapped) + offsetBytes, instances, static_cast<size_t>(bytes));
					return;
				}

				void* ptrWrite = nullptr;
				VK_CHECK(vkMapMemory(device_, instanceBufs_[currentFrame_].memory, offsetBytes, bytes, 0, &ptrWrite));
				std::memcpy(ptrWrite, instances, static_cast<size_t>(bytes));
				vkUnmapMemory(device_, instanceBufs_[currentFrame_].memory);
				return;
			}

			void* ptrWrite = nullptr;
			VK_CHECK(vkMapMemory(device_, instanceBuf.memory, offsetBytes, bytes, 0, &ptrWrite));
			std::memcpy(ptrWrite, instances, static_cast<size_t>(bytes));
			vkUnmapMemory(device_, instanceBuf.memory);
		};

		if (indexed && useInstancing && hasRegularObjects)
		{
			// Group objects by shape type for efficient rendering
			std::vector<std::vector<size_t>> shapeGroups(8); // 8 shape types

			for (size_t i = 0; i < _objects_.size(); ++i)
			{
				int shapeType = static_cast<int>(_objects_[i].getType());
				if (shapeType >= 0 && shapeType < 8)
				{
					// Objects matching isDebugOverlayObjectName are treated as debug
					// overlays and rendered in a second pass with depth testing disabled.
					std::string name = _objects_[i].getName();
					bool isOverlay = isDebugOverlayObjectName(name);
					if (isOverlay)
					{
						overlayShapeGroups[shapeType].push_back(i);
						hasOverlayObjects = true;
					}
					else
					{
						shapeGroups[shapeType].push_back(i);
					}
				}
			}

			// This pass has its own pipeline layout (adds the per-batch shape
			// texture set and tiling push constant that pipelineLayout_
			// doesn't have) -- rebind set 0 against it explicitly rather than
			// relying on the bind at the top of this function, which used
			// pipelineLayout_. Mirrors how the rigged-mesh pass below rebinds
			// set 0 against its own layout for the same reason.
			if (!descriptorSets_.empty() && flexibleShapePipelineLayout_ != VK_NULL_HANDLE)
			{
				vkCmdBindDescriptorSets(
					cmd,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					flexibleShapePipelineLayout_,
					0,
					1,
					&descriptorSets_[imageIndex],
					0,
					nullptr
				);
			}

			// Draw each shape group separately using correct firstInstance ranges
			// Our instance buffer is in original object order. Build remap to contiguous
			// instance ranges per shape so that firstInstance selects right models.
			uint32_t runningFirst = 0;
			for (int shapeType = 0; shapeType < 8; ++shapeType)
			{
				if (shapeGroups[shapeType].empty()) continue;

				const auto& shapeGeo = shapeGeometries_[shapeType];
				uint32_t countForShape = static_cast<uint32_t>(shapeGroups[shapeType].size());

				// Build a small contiguous staging of per-shape instances into a temp vector
				// then upload into instance buffer at [runningFirst, runningFirst + countForShape)
				std::vector<Instance> tmp(countForShape);
				for (uint32_t k = 0; k < countForShape; ++k)
				{
					size_t objIndex = shapeGroups[shapeType][k];
					tmp[k] = makeInstanceForObject(objIndex);
				}
				VkDeviceSize offsetBytes = sizeof(Instance) * runningFirst;
				VkDeviceSize bytes = sizeof(Instance) * countForShape;
				writeInstancesToBuffer(offsetBytes, tmp.data(), bytes);

				// Sub-group this shape type's objects by (texture path, tiling)
				// so each distinct texture gets its own draw call with the
				// right descriptor and tiling push-constant bound. Only
				// consecutive runs are merged (not a full re-sort), so most
				// scenes -- where a texture is set on a coherent batch of
				// objects, like one floor -- still get one draw call per
				// texture; interleaved textures within a shape type cost
				// extra draw calls rather than extra complexity here.
				struct TextureBatch
				{
					std::string texturePath;
					glm::vec2 tiling{1.0f, 1.0f};
					uint32_t first = 0;
					uint32_t count = 0;
				};
				std::vector<TextureBatch> batches;
				for (uint32_t k = 0; k < countForShape; ++k)
				{
					size_t objIndex = shapeGroups[shapeType][k];
					std::string const texturePath = _objects_[objIndex].getTexturePath();
					glm::vec2 const tiling = _objects_[objIndex].getTextureTiling();
					if (!batches.empty() &&
						batches.back().texturePath == texturePath &&
						batches.back().tiling == tiling)
					{
						batches.back().count++;
						continue;
					}
					batches.push_back({texturePath, tiling, runningFirst + k, 1});
				}

				for (const TextureBatch& batch : batches)
				{
					std::shared_ptr<detail::Texture> const texture =
						batch.texturePath.empty() ? nullptr : getOrCreateTexture(batch.texturePath);
					VkDescriptorSet textureSet = (texture && texture->descriptor != VK_NULL_HANDLE)
						? texture->descriptor
						: (defaultTexture_ ? defaultTexture_->descriptor : VK_NULL_HANDLE);
					if (textureSet != VK_NULL_HANDLE)
					{
						vkCmdBindDescriptorSets(
							cmd,
							VK_PIPELINE_BIND_POINT_GRAPHICS,
							flexibleShapePipelineLayout_,
							1,
							1,
							&textureSet,
							0,
							nullptr
						);
					}

					detail::FlexibleShapeTexturePushConstants push{};
					push.tiling = glm::vec4(batch.tiling, 0.0f, 0.0f);
					vkCmdPushConstants(cmd, flexibleShapePipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
						0, sizeof(push), &push);

					// Issue indexed instanced draw for this contiguous sub-range
					vkCmdDrawIndexed(cmd,
						shapeGeo.indexCount,
						batch.count,
						shapeGeo.indexOffset,
						0,
						batch.first);
				}

				runningFirst += countForShape;
			}

			overlayBaseFirstInstance = runningFirst;
		}
		else if (indexed && hasRegularObjects)
		{
			// Fallback: draw all as spheres
			vkCmdDrawIndexed(cmd, indexCount_, 1, 0, 0, 0);
		}
		else
		{
			logMessage(LogLevel::Debug,
			           "Draw fallback invoked with " +
			           std::to_string(useInstancing ? instanceCount_ : 1) + " instances");
			// Fallback if you don't have an index buffer
			uint32_t vertexCount = 0; // TODO set actual count if you use non-indexed draw
			if (useInstancing)
			{
				vkCmdDraw(cmd, vertexCount, instanceCount_, 0, 0);
			}
			else
			{
				vkCmdDraw(cmd, vertexCount, 1, 0, 0);
			}
		}

		// Draw rigged meshes
		if (!riggedInstances_.empty() && riggedPipeline_ != VK_NULL_HANDLE)
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, riggedPipeline_);

			if (!descriptorSets_.empty())
			{
				if (imageIndex >= descriptorSets_.size())
				{
					throw std::runtime_error("recordCommandBuffer: descriptorSets_ size mismatch");
				}
				vkCmdBindDescriptorSets(
					cmd,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					riggedPipelineLayout_,
					0,
					1,
					&descriptorSets_[imageIndex],
					0,
					nullptr
				);
			}

			for (const auto& riggedInstance : riggedInstances_)
			{
				const detail::Buffer& frameInstanceBuffer = riggedInstance.instanceBuffers[currentFrame_];
				for (const auto& meshData : riggedInstance.meshes)
				{
					const detail::Buffer& frameVertexBuffer = meshData.vertexBuffers[currentFrame_];
					if (frameVertexBuffer.buffer == VK_NULL_HANDLE ||
					    frameInstanceBuffer.buffer == VK_NULL_HANDLE ||
					    meshData.indexBuffer.buffer == VK_NULL_HANDLE ||
					    meshData.indexCount == 0)
					{
						continue;
					}

					std::array<VkBuffer, 2> riggedBuffers{
						frameVertexBuffer.buffer,
						frameInstanceBuffer.buffer
					};
					std::array<VkDeviceSize, 2> riggedOffsets{0, 0};

					vkCmdBindVertexBuffers(cmd, 0, 2, riggedBuffers.data(), riggedOffsets.data());
					vkCmdBindIndexBuffer(cmd, meshData.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

					VkDescriptorSet textureSet = VK_NULL_HANDLE;
					if (meshData.texture && meshData.texture->descriptor != VK_NULL_HANDLE)
					{
						textureSet = meshData.texture->descriptor;
					}
					else if (defaultTexture_ && defaultTexture_->descriptor != VK_NULL_HANDLE)
					{
						textureSet = defaultTexture_->descriptor;
					}

					if (textureSet == VK_NULL_HANDLE)
					{
						continue;
					}

					vkCmdBindDescriptorSets(
						cmd,
						VK_PIPELINE_BIND_POINT_GRAPHICS,
						riggedPipelineLayout_,
						1,
						1,
						&textureSet,
						0,
						nullptr
					);

					vkCmdDrawIndexed(cmd, meshData.indexCount, 1, 0, 0, 0);
				}
			}
		}
		else if (!riggedInstances_.empty())
		{
			// No rigged pipeline available; skip rendering rigged meshes
		}

		// Draw overlay debug shapes (e.g. collision capsules) through scene
		// meshes -- flexibleShapeOverlayPipeline_ has depth testing off, so
		// no depth manipulation is needed here to guarantee they're visible.
		if (indexed && useInstancing && hasOverlayObjects && flexibleShapeOverlayPipeline_ != VK_NULL_HANDLE)
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flexibleShapeOverlayPipeline_);

			// flexibleShapeOverlayPipeline_ shares flexibleShapePipelineLayout_
			// (set 1 = shape texture, plus the tiling push constant) -- bind
			// against that, not pipelineLayout_, for the same reason as the
			// main flexible-shape pass above.
			if (!descriptorSets_.empty() && flexibleShapePipelineLayout_ != VK_NULL_HANDLE)
			{
				if (imageIndex >= descriptorSets_.size())
				{
					throw std::runtime_error("recordCommandBuffer: descriptorSets_ size mismatch");
				}
				vkCmdBindDescriptorSets(
					cmd,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					flexibleShapePipelineLayout_,
					0,
					1,
					&descriptorSets_[imageIndex],
					0,
					nullptr
				);
			}

			if (vbCount > 0)
			{
				vkCmdBindVertexBuffers(cmd, 0, vbCount, vbs.data(), offs.data());
			}
			vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);

			uint32_t runningFirst = overlayBaseFirstInstance;
			for (int shapeType = 0; shapeType < 8; ++shapeType)
			{
				if (overlayShapeGroups[shapeType].empty()) continue;

				const auto& shapeGeo = shapeGeometries_[shapeType];
				uint32_t countForShape = static_cast<uint32_t>(overlayShapeGroups[shapeType].size());

				std::vector<Instance> tmp(countForShape);
				for (uint32_t k = 0; k < countForShape; ++k)
				{
					size_t objIndex = overlayShapeGroups[shapeType][k];
					tmp[k] = makeInstanceForObject(objIndex);
				}

				VkDeviceSize offsetBytes = sizeof(Instance) * runningFirst;
				VkDeviceSize bytes = sizeof(Instance) * countForShape;
				writeInstancesToBuffer(offsetBytes, tmp.data(), bytes);

				// Overlay shapes essentially never carry a texture, but the
				// pipeline layout still declares set 1 and the tiling push
				// constant, so both must be bound before drawing -- see the
				// main flexible-shape pass above for the full explanation.
				struct TextureBatch
				{
					std::string texturePath;
					glm::vec2 tiling{1.0f, 1.0f};
					uint32_t first = 0;
					uint32_t count = 0;
				};
				std::vector<TextureBatch> batches;
				for (uint32_t k = 0; k < countForShape; ++k)
				{
					size_t objIndex = overlayShapeGroups[shapeType][k];
					std::string const texturePath = _objects_[objIndex].getTexturePath();
					glm::vec2 const tiling = _objects_[objIndex].getTextureTiling();
					if (!batches.empty() &&
						batches.back().texturePath == texturePath &&
						batches.back().tiling == tiling)
					{
						batches.back().count++;
						continue;
					}
					batches.push_back({texturePath, tiling, runningFirst + k, 1});
				}

				for (const TextureBatch& batch : batches)
				{
					std::shared_ptr<detail::Texture> const texture =
						batch.texturePath.empty() ? nullptr : getOrCreateTexture(batch.texturePath);
					VkDescriptorSet textureSet = (texture && texture->descriptor != VK_NULL_HANDLE)
						? texture->descriptor
						: (defaultTexture_ ? defaultTexture_->descriptor : VK_NULL_HANDLE);
					if (textureSet != VK_NULL_HANDLE)
					{
						vkCmdBindDescriptorSets(
							cmd,
							VK_PIPELINE_BIND_POINT_GRAPHICS,
							flexibleShapePipelineLayout_,
							1,
							1,
							&textureSet,
							0,
							nullptr
						);
					}

					detail::FlexibleShapeTexturePushConstants push{};
					push.tiling = glm::vec4(batch.tiling, 0.0f, 0.0f);
					vkCmdPushConstants(cmd, flexibleShapePipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
						0, sizeof(push), &push);

					vkCmdDrawIndexed(cmd,
						shapeGeo.indexCount,
						batch.count,
						shapeGeo.indexOffset,
						0,
						batch.first);
				}

				runningFirst += countForShape;
			}
		}

		// Custom meshes and volumes share a stable application-controlled order.
		drawOrderedCustomResources(cmd, imageIndex);

		// The UI draws after the scene, inside the same render pass, immediately
		// before it ends -- opening a second pass would cost a full attachment
		// load/store on tiled GPUs for nothing.
#ifdef LVG_WITH_UI
		recordUi(cmd);
#endif

		vkCmdEndRenderPass(cmd);

		if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
		{
			throw std::runtime_error("recordCommandBuffer: vkEndCommandBuffer failed");
		}
	}

} // namespace lightGraphics
