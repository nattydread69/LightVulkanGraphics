// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Light Vulkan Graphics
// Copyright (C) 2025 Dr. Nathanael John Inkson
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
#include "SceneGraph.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <GLFW/glfw3.h>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244)
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cstring>
#include <cstdlib>
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

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <dlfcn.h>
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <dlfcn.h>
#include <unistd.h>
#endif

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
	static_assert(std::is_standard_layout_v<GpuLight>, "GpuLight must use a stable standard layout");
	static_assert(sizeof(GpuLight) == sizeof(glm::vec4) * 4, "GpuLight size must match shader layout");
	static_assert(std::is_standard_layout_v<LightingBufferObject>,
	              "LightingBufferObject must use a stable standard layout");

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

		void shaderSearchModuleAnchor()
		{
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

		bool isFiniteVec2(const glm::vec2& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y);
		}

		bool isFiniteVec3(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
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

		std::optional<std::filesystem::path> weaklyCanonicalPath(const std::filesystem::path& path)
		{
			std::error_code error;
			const auto canonicalPath = std::filesystem::weakly_canonical(path, error);
			if (error)
			{
				return path.lexically_normal();
			}
			return canonicalPath;
		}

		std::optional<std::string> getEnvironmentVariable(const char* name)
		{
#if defined(_WIN32)
			char* value = nullptr;
			size_t length = 0;
			if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
			{
				return std::nullopt;
			}

			std::string result(value);
			free(value);
			return result;
#else
			if (const char* value = std::getenv(name))
			{
				return std::string(value);
			}
			return std::nullopt;
#endif
		}

#if defined(_WIN32)
		std::optional<std::filesystem::path> getWindowsModulePath(HMODULE module)
		{
			if (module == nullptr)
			{
				return std::nullopt;
			}

			std::wstring buffer(512, L'\0');
			for (;;)
			{
				const DWORD length = GetModuleFileNameW(module, buffer.data(),
				                                        static_cast<DWORD>(buffer.size()));
				if (length == 0)
				{
					return std::nullopt;
				}

				if (length < buffer.size())
				{
					buffer.resize(length);
					return std::filesystem::path(buffer);
				}

				buffer.resize(buffer.size() * 2);
			}
		}
#endif

		std::optional<std::filesystem::path> getExecutablePath()
		{
#if defined(_WIN32)
			return getWindowsModulePath(nullptr);
#elif defined(__APPLE__)
			uint32_t size = 0;
			_NSGetExecutablePath(nullptr, &size);
			if (size == 0)
			{
				return std::nullopt;
			}

			std::string buffer(size, '\0');
			if (_NSGetExecutablePath(buffer.data(), &size) != 0)
			{
				return std::nullopt;
			}

			return weaklyCanonicalPath(std::filesystem::path(buffer.c_str()));
#elif defined(__linux__)
			std::array<char, 4096> buffer{};
			const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
			if (length <= 0)
			{
				return std::nullopt;
			}

			buffer[static_cast<size_t>(length)] = '\0';
			return weaklyCanonicalPath(std::filesystem::path(buffer.data()));
#else
			return std::nullopt;
#endif
		}

		std::optional<std::filesystem::path> getLibraryPath()
		{
#if defined(_WIN32)
			HMODULE module = nullptr;
			const BOOL loaded = GetModuleHandleExA(
			    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			    reinterpret_cast<LPCSTR>(reinterpret_cast<void*>(&shaderSearchModuleAnchor)),
			    &module);
			if (!loaded)
			{
				return std::nullopt;
			}
			return getWindowsModulePath(module);
#elif defined(__APPLE__) || defined(__linux__)
			Dl_info info{};
			if (dladdr(reinterpret_cast<void*>(&shaderSearchModuleAnchor), &info) == 0 ||
			    info.dli_fname == nullptr || info.dli_fname[0] == '\0')
			{
				return std::nullopt;
			}
			return weaklyCanonicalPath(std::filesystem::path(info.dli_fname));
#else
			return std::nullopt;
#endif
		}

		void appendShaderSearchRoots(std::vector<std::filesystem::path>& roots,
		                             const std::filesystem::path& baseDir)
		{
			if (baseDir.empty())
			{
				return;
			}

			roots.push_back(baseDir / "spv");
			roots.push_back(baseDir / "../spv");
			roots.push_back(baseDir / "../../spv");
			roots.push_back(baseDir / "../share/LightVulkanGraphics/spv");
			roots.push_back(baseDir / "../../share/LightVulkanGraphics/spv");
#if defined(__APPLE__)
			roots.push_back(baseDir / "../Resources/LightVulkanGraphics/spv");
			roots.push_back(baseDir / "../../Resources/LightVulkanGraphics/spv");
#endif
		}

		std::string makeObjectIndexMessage(const char* operation, size_t index, size_t size)
		{
			std::ostringstream message;
			message << operation << " index " << index << " is out of range for "
			        << size << " objects";
			return message.str();
		}

		std::string makeLightIndexMessage(const char* operation, size_t index, size_t size)
		{
			std::ostringstream message;
			message << operation << " index " << index << " is out of range for "
			        << size << " lights";
			return message.str();
		}
	}

#define LVG_VK_CHECK(expr) checkVkResult((expr), #expr, __FILE__, __LINE__)
#define VK_CHECK(expr) LVG_VK_CHECK(expr)

	VkApp::VkApp()
	    : sceneGraph_(std::make_unique<SceneGraph>(*this))
	{
		LightSource defaultLight;
		defaultLight.type = LightType::Directional;
		defaultLight.direction = -glm::normalize(glm::vec3(0.4f, 0.7f, 0.2f));
		defaultLight.color = glm::vec3(1.0f);
		defaultLight.intensity = 1.0f;
		defaultLight.name = "Default Directional Light";
		lights_.push_back(defaultLight);
		lightTransformMatrixOverrides_.push_back(std::nullopt);
	}

	VkApp::~VkApp()
	{
		cleanup();
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

		std::string message = std::string("[Validation] ") + messageText;
		if (app && (app->debugOutput || app->logCallback_))
		{
			app->logMessage(level, message);
		}
		else
		{
			auto& stream = (level == LogLevel::Error || level == LogLevel::Warning)
			             ? consoleErrorStream()
			             : consoleInfoStream();
			stream << message << std::endl;
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

	bool VkApp::validateRiggedMesh(const RiggedMesh& mesh) const
	{
		const std::string meshLabel = mesh.materialName.empty() ? std::string("<unnamed>") : mesh.materialName;
		size_t unnormalizedWeightVertices = 0;
		for (size_t index = 0; index < mesh.indices.size(); ++index)
		{
			if (mesh.indices[index] >= mesh.vertices.size())
			{
				logMessage(LogLevel::Error,
				           "[RiggedMesh] '" + meshLabel + "' has out-of-range index " +
				           std::to_string(mesh.indices[index]) + " at element " + std::to_string(index));
				return false;
			}
		}

		for (size_t vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex)
		{
			const RiggedVertex& vertex = mesh.vertices[vertexIndex];
			if (!isFiniteVec3(vertex.position) || !isFiniteVec3(vertex.normal) || !isFiniteVec2(vertex.texCoords))
			{
				logMessage(LogLevel::Error,
				           "[RiggedMesh] '" + meshLabel + "' has non-finite source vertex data at vertex " +
				           std::to_string(vertexIndex));
				return false;
			}

			float weightSum = 0.0f;
			for (int k = 0; k < 4; ++k)
			{
				const int boneIndex = vertex.boneIndices[k];
				const float weight = vertex.boneWeights[k];
				if (!std::isfinite(weight) || weight < 0.0f)
				{
					logMessage(LogLevel::Error,
					           "[RiggedMesh] '" + meshLabel + "' has invalid bone weight at vertex " +
					           std::to_string(vertexIndex));
					return false;
				}

				if (boneIndex < -1 || boneIndex >= static_cast<int>(mesh.bones.size()))
				{
					logMessage(LogLevel::Error,
					           "[RiggedMesh] '" + meshLabel + "' has invalid bone index " +
					           std::to_string(boneIndex) + " at vertex " + std::to_string(vertexIndex));
					return false;
				}

				weightSum += weight;
			}

			if (weightSum > 0.0f && std::abs(weightSum - 1.0f) > 1.0e-3f)
			{
				++unnormalizedWeightVertices;
			}
		}

		if (unnormalizedWeightVertices > 0)
		{
			logMessage(LogLevel::Warning,
			           "[RiggedMesh] '" + meshLabel + "' has " +
			           std::to_string(unnormalizedWeightVertices) +
			           " vertices whose bone weights are not normalized");
		}

		if (debugOutput)
		{
			std::ostringstream message;
			message << "[RiggedMesh] '" << meshLabel << "' validated: vertices=" << mesh.vertices.size()
			        << ", indices=" << mesh.indices.size() << ", bones=" << mesh.bones.size();
			logMessage(LogLevel::Debug, message.str());
		}

		return true;
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
		createDescriptorSetLayout();
		createGraphicsPipeline();
		createCommandPool();
		createSyncObjects();
		createDepthResources();
		createUniformBuffers();
		createDescriptorPool();
		createTextureDescriptorPool();
		createDescriptorSets();
		defaultTexture_ = createSolidColorTexture(255, 255, 255, 255);
		createSceneResources();

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

		// Tear down swapchain-dependent resources
		cleanupSwapChain();

			// Scene resources teardown here
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
		if (riggedPipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, riggedPipeline_, nullptr);
			riggedPipeline_ = VK_NULL_HANDLE;
		}
		if (pipelineLayout_ != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
			pipelineLayout_ = VK_NULL_HANDLE;
		}
		if (riggedPipelineLayout_ != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device_, riggedPipelineLayout_, nullptr);
			riggedPipelineLayout_ = VK_NULL_HANDLE;
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

			if (keyboardCameraEnabled_)
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
			drawFrame();
		}
	}

	void VkApp::drawFrame()
	{
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
	void VkApp::recreateSwapChain()
	{
		int w = 0, h = 0;
		glfwGetFramebufferSize(window_, &w, &h);
		while (w == 0 || h == 0)
		{
			glfwGetFramebufferSize(window_, &w, &h);
			glfwWaitEvents();
		}

		vkDeviceWaitIdle(device_);

		cleanupSwapChain();

		createSwapChain();
		createImageViews();
		createRenderPass();
		createGraphicsPipeline();
		createDepthResources();
		createFramebuffers();
		createUniformBuffers();
		createDescriptorPool();
		createDescriptorSets();
		createCommandBuffers();
	}

	void VkApp::cleanupSwapChain()
	{
		// Depth
		if (depthImageView_ != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device_, depthImageView_, nullptr);
			depthImageView_ = VK_NULL_HANDLE;
		}
		if (depthImage_ != VK_NULL_HANDLE)
		{
			vkDestroyImage(device_, depthImage_, nullptr);
			depthImage_ = VK_NULL_HANDLE;
		}
		if (depthImageMemory_ != VK_NULL_HANDLE)
		{
			vkFreeMemory(device_, depthImageMemory_, nullptr);
			depthImageMemory_ = VK_NULL_HANDLE;
		}

		// Framebuffers
		for (auto fb : swapChainFramebuffers_)
		{
			vkDestroyFramebuffer(device_, fb, nullptr);
		}
		swapChainFramebuffers_.clear();

		// Command buffers
		if (!commandBuffers_.empty())
		{
			vkFreeCommandBuffers(
				device_,
				commandPool_,
				static_cast<uint32_t>(commandBuffers_.size()),
				commandBuffers_.data()
			);
			commandBuffers_.clear();
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
		if (pipelineLayout_ != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
			pipelineLayout_ = VK_NULL_HANDLE;
		}
		if (renderPass_ != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(device_, renderPass_, nullptr);
			renderPass_ = VK_NULL_HANDLE;
		}

		// Image views
		for (auto view : swapChainImageViews_)
		{
			vkDestroyImageView(device_, view, nullptr);
		}
		swapChainImageViews_.clear();

		// Swapchain
		if (swapChain_ != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(device_, swapChain_, nullptr);
			swapChain_ = VK_NULL_HANDLE;
		}


		// Unmap UBO memory before freeing
		for (size_t i = 0; i < uniformBuffersMemory_.size(); i++)
		{
			if (uniformBuffersMemory_[i] != VK_NULL_HANDLE && uniformBuffersMapped_.size() > i)
			{
				if (uniformBuffersMapped_[i] != nullptr)
				{
					vkUnmapMemory(device_, uniformBuffersMemory_[i]);
					uniformBuffersMapped_[i] = nullptr;
				}
			}
		}

		for (size_t i = 0; i < uniformBuffers_.size(); i++)
		{
			if (uniformBuffers_[i] != VK_NULL_HANDLE)
			{
				vkDestroyBuffer(device_, uniformBuffers_[i], nullptr);
				uniformBuffers_[i] = VK_NULL_HANDLE;
			}
			if (uniformBuffersMemory_[i] != VK_NULL_HANDLE)
			{
				vkFreeMemory(device_, uniformBuffersMemory_[i], nullptr);
				uniformBuffersMemory_[i] = VK_NULL_HANDLE;
			}
		}

		for (size_t i = 0; i < uniformBuffers2_.size(); i++)
		{
			if (uniformBuffers2_[i].memory != VK_NULL_HANDLE && uniformBuffersMapped_.size() > i)
			{
				if (uniformBuffersMapped_[i] != nullptr)
				{
					vkUnmapMemory(device_, uniformBuffers2_[i].memory);
					uniformBuffersMapped_[i] = nullptr;
				}
			}
			destroyBuffer(device_, uniformBuffers2_[i]);
		}
		for (size_t i = 0; i < lightingBuffers_.size(); i++)
		{
			if (lightingBuffers_[i].memory != VK_NULL_HANDLE && lightingBuffersMapped_.size() > i)
			{
				if (lightingBuffersMapped_[i] != nullptr)
				{
					vkUnmapMemory(device_, lightingBuffers_[i].memory);
					lightingBuffersMapped_[i] = nullptr;
				}
			}
			destroyBuffer(device_, lightingBuffers_[i]);
		}
		uniformBuffers2_.clear();
		uniformBuffers_.clear();
		uniformBuffersMemory_.clear();
		uniformBuffersMapped_.clear();
		lightingBuffers_.clear();
		lightingBuffersMapped_.clear();

		if (descriptorPool_ != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
			descriptorPool_ = VK_NULL_HANDLE;
		}
		descriptorSets_.clear();
	}

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

	void VkApp::handleRiggedAnimationInput()
	{
		if (!window_ || riggedInstances_.empty())
		{
			return;
		}

		bool nextDown = (glfwGetKey(window_, GLFW_KEY_N) == GLFW_PRESS) ||
						(glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS);
		if (nextDown && !riggedNextKeyDown_)
		{
			for (auto& instance : riggedInstances_)
			{
				auto riggedObject = instance.object;
				if (!riggedObject)
				{
					continue;
				}

				int animationCount = riggedObject->getAnimationCount();
				if (animationCount <= 0)
				{
					continue;
				}

				const int currentIndex = riggedObject->getCurrentAnimationIndex();
				instance.activeAnimationIndex = currentIndex;
				instance.animationLoop = riggedObject->getAnimationLooping();

				int nextIndex = currentIndex;
				if (nextIndex < 0 || nextIndex >= animationCount)
				{
					nextIndex = 0;
				}
				else
				{
					nextIndex = (nextIndex + 1) % animationCount;
				}

				riggedObject->playAnimation(nextIndex, instance.animationLoop);
				instance.activeAnimationIndex = nextIndex;
			}
		}
		riggedNextKeyDown_ = nextDown;

		bool prevDown = (glfwGetKey(window_, GLFW_KEY_P) == GLFW_PRESS) ||
						(glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS);
		if (prevDown && !riggedPrevKeyDown_)
		{
			for (auto& instance : riggedInstances_)
			{
				auto riggedObject = instance.object;
				if (!riggedObject)
				{
					continue;
				}

				int animationCount = riggedObject->getAnimationCount();
				if (animationCount <= 0)
				{
					continue;
				}

				const int currentIndex = riggedObject->getCurrentAnimationIndex();
				instance.activeAnimationIndex = currentIndex;
				instance.animationLoop = riggedObject->getAnimationLooping();

				int prevIndex = currentIndex;
				if (prevIndex < 0 || prevIndex >= animationCount)
				{
					prevIndex = animationCount - 1;
				}
				else
				{
					prevIndex = (prevIndex - 1 + animationCount) % animationCount;
				}

				riggedObject->playAnimation(prevIndex, instance.animationLoop);
				instance.activeAnimationIndex = prevIndex;
			}
		}
		riggedPrevKeyDown_ = prevDown;

		bool stopDown = (glfwGetKey(window_, GLFW_KEY_O) == GLFW_PRESS);
		if (stopDown && !riggedStopKeyDown_)
		{
			for (auto& instance : riggedInstances_)
			{
				auto riggedObject = instance.object;
				if (!riggedObject)
				{
					continue;
				}

				riggedObject->stopAnimation();
				instance.activeAnimationIndex = -1;
			}
		}
		riggedStopKeyDown_ = stopDown;
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

	void VkApp::updateLightingBuffer(uint32_t imageIndex)
	{
		if (imageIndex >= lightingBuffersMapped_.size())
		{
			throw std::runtime_error("updateLightingBuffer: imageIndex out of range");
		}

		void* dst = lightingBuffersMapped_[imageIndex];
		if (!dst)
		{
			throw std::runtime_error("updateLightingBuffer: mapped pointer is null");
		}

		LightingBufferObject lighting = buildLightingBufferObject();
		std::memcpy(dst, &lighting, sizeof(lighting));
		lightingDataDirty_ = false;
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

	void VkApp::createSurface()
	{
		VK_CHECK(glfwCreateWindowSurface(inst, window_, nullptr, &surface_));
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

	void VkApp::createSwapChain()
	{
		VkSurfaceCapabilitiesKHR caps{};
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);
		uint32_t fmtCount=0; vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, nullptr);
		std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, fmts.data());
		VkSurfaceFormatKHR chosen = fmts[0];
		for (auto& f : fmts)
		{
			if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format==VK_FORMAT_B8G8R8A8_SRGB)
			{
				chosen = f;
				break;
			}
		}
		swapChainImageFormat_ = chosen.format;
		const uint32_t WIDTH = width_, HEIGHT = height_;
		swapChainExtent_ = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : VkExtent2D{WIDTH, HEIGHT};
		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // vsync
		uint32_t desiredImageCount = std::max(2u, caps.minImageCount);
		if (caps.maxImageCount > 0)
		{
			desiredImageCount = std::min(desiredImageCount, caps.maxImageCount);
		}

		VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
		sci.surface = surface_;
		sci.minImageCount = desiredImageCount;
		sci.imageFormat = swapChainImageFormat_;
		sci.imageColorSpace = chosen.colorSpace;
		sci.imageExtent = swapChainExtent_;
		sci.imageArrayLayers = 1;
		sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		if (qFamGfx != qFamPresent)
		{
			uint32_t qidx[2] = { qFamGfx, qFamPresent };
			sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			sci.queueFamilyIndexCount = 2;
			sci.pQueueFamilyIndices = qidx;
		}
		else
		{
			sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}
		sci.preTransform = caps.currentTransform;
		sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		sci.presentMode = presentMode;
		sci.clipped = VK_TRUE;
		VK_CHECK(vkCreateSwapchainKHR(device_, &sci, nullptr, &swapChain_));
		uint32_t nImgs=0; vkGetSwapchainImagesKHR(device_, swapChain_, &nImgs, nullptr);
		swapChainImages_.resize(nImgs);
		vkGetSwapchainImagesKHR(device_, swapChain_, &nImgs, swapChainImages_.data());

		if (debugOutput)
		{
			std::ostringstream message;
			message << "Swapchain: format=" << swapChainImageFormat_
			        << ", colorSpace=" << chosen.colorSpace
			        << ", images=" << nImgs
			        << ", extent=" << swapChainExtent_.width << 'x' << swapChainExtent_.height;
			logMessage(LogLevel::Debug, message.str());
		}
	}

	void VkApp::createImageViews()
	{
		for (auto img : swapChainImages_)
		{
			VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
			vi.image = img;
			vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
			vi.format = swapChainImageFormat_;
			vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			vi.subresourceRange.levelCount = 1;
			vi.subresourceRange.layerCount = 1;
			VkImageView view{};
			VK_CHECK(vkCreateImageView(device_, &vi, nullptr, &view));
			swapChainImageViews_.push_back(view);
		}
	}

	void VkApp::createRenderPass()
	{
		VkAttachmentDescription color{};
		color.format = swapChainImageFormat_;
		color.samples = VK_SAMPLE_COUNT_1_BIT;
		color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color.storeOp= VK_ATTACHMENT_STORE_OP_STORE;
		color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		color.finalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentDescription depth{};
		depth.format = findDepthFormat();
		depth.samples = VK_SAMPLE_COUNT_1_BIT;
		depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
		VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

		VkSubpassDescription sub{};
		sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		sub.colorAttachmentCount = 1;
		sub.pColorAttachments = &colorRef;
		sub.pDepthStencilAttachment = &depthRef;

		VkAttachmentDescription attachments[] = {color, depth};
		VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
		rpci.attachmentCount = 2; rpci.pAttachments = attachments;
		rpci.subpassCount = 1; rpci.pSubpasses = &sub;
		VK_CHECK(vkCreateRenderPass(device_, &rpci, nullptr, &renderPass_));
	}

	void VkApp::createDescriptorSetLayout()
	{
		VkDescriptorSetLayoutBinding uboB{};
		uboB.binding = 0;
		uboB.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboB.descriptorCount = 1;
		uboB.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutBinding lightingB{};
		lightingB.binding = 1;
		lightingB.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		lightingB.descriptorCount = 1;
		lightingB.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		std::array<VkDescriptorSetLayoutBinding, 2> bindings{uboB, lightingB};
		VkDescriptorSetLayoutCreateInfo dsli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		dsli.bindingCount = static_cast<uint32_t>(bindings.size());
		dsli.pBindings = bindings.data();
		VK_CHECK(vkCreateDescriptorSetLayout(device_, &dsli, nullptr, &descriptorSetLayout_));

		VkDescriptorSetLayoutBinding samplerBinding{};
		samplerBinding.binding = 0;
		samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerBinding.descriptorCount = 1;
		samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo samplerLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		samplerLayoutInfo.bindingCount = 1;
		samplerLayoutInfo.pBindings = &samplerBinding;
		VK_CHECK(vkCreateDescriptorSetLayout(device_, &samplerLayoutInfo, nullptr, &textureSetLayout_));
	}

	void VkApp::createGraphicsPipeline()
	{
		VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
		if (descriptorSetLayout_ != VK_NULL_HANDLE)
		{
			plci.setLayoutCount = 1;
			plci.pSetLayouts = &descriptorSetLayout_;
		}
		VK_CHECK(vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_));

		// Create all pipeline variants
		createFlexibleShapePipeline();
		createWireframePipeline();
		createUnlitPipeline();
		createLinePipeline();

		// Keep the original sphere pipeline for backward compatibility
		createOriginalSpherePipeline();

		// Create rigged rendering pipeline (needs sampler descriptor set)
		createRiggedPipeline();
	}

	void VkApp::createOriginalSpherePipeline()
	{
		// --- Shaders
		auto vsCode = readFile(findShaderPath("instanced_sphere.vert.spv"));
		auto fsCode = readFile(findShaderPath("instanced_sphere.frag.spv"));
		auto mkModule = [&](const std::vector<char>& code)->VkShaderModule{
			VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			smi.codeSize = code.size();
			smi.pCode = reinterpret_cast<const uint32_t*>(code.data());
			VkShaderModule m{}; VK_CHECK(vkCreateShaderModule(device_, &smi, nullptr, &m)); return m;
		};
		VkShaderModule vs = mkModule(vsCode), fs = mkModule(fsCode);

		// --- Vertex input: binding 0 = sphere verts, binding 1 = instance data
		VkVertexInputBindingDescription binds[2]{};
		binds[0].binding = 0; binds[0].stride = sizeof(Vertex);   binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		binds[1].binding = 1; binds[1].stride = sizeof(Instance); binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		std::array<VkVertexInputAttributeDescription, 7> attrs{};
		// pos (0) + nrm(1)
		attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
		attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, nrm)};
		// model matrix columns (2..5) from binding 1
		attrs[2] = {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*0};
		attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*1};
		attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*2};
		attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*3};
		// color (6)
		attrs[6] = {6, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, color)};

		VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds;
		vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();

		// Input assembly
		VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		// Viewport/scissor (fixed; no resize handling for brevity)
		VkViewport vp{0,0,(float)swapChainExtent_.width,(float)swapChainExtent_.height, 0.0f, 1.0f};
		VkRect2D sc{{0,0}, swapChainExtent_};
		VkPipelineViewportStateCreateInfo vpci{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		vpci.viewportCount=1; vpci.pViewports=&vp; vpci.scissorCount=1; vpci.pScissors=&sc;

		// Rasterization
		VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_BACK_BIT;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		// Multisample
		VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Colour blend
		VkPipelineColorBlendAttachmentState cba{};
		cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		cb.attachmentCount=1; cb.pAttachments=&cba;

		// Depth testing
		VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS;

		// Shaders stages
		VkPipelineShaderStageCreateInfo sVS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sVS.stage = VK_SHADER_STAGE_VERTEX_BIT; sVS.module=vs; sVS.pName="main";
		VkPipelineShaderStageCreateInfo sFS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sFS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; sFS.module=fs; sFS.pName="main";
		VkPipelineShaderStageCreateInfo stages[2]={sVS,sFS};

		// Pipeline
		VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		gp.stageCount=2; gp.pStages=stages;
		gp.pVertexInputState=&vi;
		gp.pInputAssemblyState=&ia;
		gp.pViewportState=&vpci;
		gp.pRasterizationState=&rs;
		gp.pMultisampleState=&ms;
		gp.pColorBlendState=&cb;
		gp.pDepthStencilState = &ds;
		gp.layout=pipelineLayout_;
		gp.renderPass=renderPass_; gp.subpass=0;
		VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &graphicsPipeline_));
		vkDestroyShaderModule(device_, vs, nullptr);
		vkDestroyShaderModule(device_, fs, nullptr);
	}

	void VkApp::createFlexibleShapePipeline()
	{
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

		// --- Shaders
		auto vsCode = readFile(findShaderPath("flexible_shape.vert.spv"));
		auto fsCode = readFile(findShaderPath("flexible_shape.frag.spv"));
		auto mkModule = [&](const std::vector<char>& code)->VkShaderModule{
			VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			smi.codeSize = code.size();
			smi.pCode = reinterpret_cast<const uint32_t*>(code.data());
			VkShaderModule m{}; VK_CHECK(vkCreateShaderModule(device_, &smi, nullptr, &m)); return m;
		};
		VkShaderModule vs = mkModule(vsCode), fs = mkModule(fsCode);

		// --- Vertex input: binding 0 = verts, binding 1 = instance data (with shape type)
		VkVertexInputBindingDescription binds[2]{};
		binds[0].binding = 0; binds[0].stride = sizeof(Vertex);   binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		binds[1].binding = 1; binds[1].stride = sizeof(Instance); binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		std::array<VkVertexInputAttributeDescription, 9> attrs{};
		// pos (0) + nrm(1) + uv(2)
		attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
		attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, nrm)};
		attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};
		// model matrix columns (3..6) from binding 1
		attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*0};
		attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*1};
		attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*2};
		attrs[6] = {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*3};
		// color (7) + shapeType (8)
		attrs[7] = {7, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, color)};
		attrs[8] = {8, 1, VK_FORMAT_R32_SFLOAT, offsetof(Instance, shapeType)};

		VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds;
		vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();

		// Input assembly
		VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		// Viewport/scissor
		VkViewport vp{0,0,(float)swapChainExtent_.width,(float)swapChainExtent_.height, 0.0f, 1.0f};
		VkRect2D sc{{0,0}, swapChainExtent_};
		VkPipelineViewportStateCreateInfo vpci{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		vpci.viewportCount=1; vpci.pViewports=&vp; vpci.scissorCount=1; vpci.pScissors=&sc;

		// Rasterization
		VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_BACK_BIT;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		// Multisample
		VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Colour blend
		VkPipelineColorBlendAttachmentState cba{};
		cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		cb.attachmentCount=1; cb.pAttachments=&cba;

		// Depth testing
		VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS;

		// Shaders stages
		VkPipelineShaderStageCreateInfo sVS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sVS.stage = VK_SHADER_STAGE_VERTEX_BIT; sVS.module=vs; sVS.pName="main";
		VkPipelineShaderStageCreateInfo sFS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sFS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; sFS.module=fs; sFS.pName="main";
		VkPipelineShaderStageCreateInfo stages[2]={sVS,sFS};

		// Pipeline
		VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		gp.stageCount=2; gp.pStages=stages;
		gp.pVertexInputState=&vi;
		gp.pInputAssemblyState=&ia;
		gp.pViewportState=&vpci;
		gp.pRasterizationState=&rs;
		gp.pMultisampleState=&ms;
		gp.pColorBlendState=&cb;
		gp.pDepthStencilState = &ds;
		gp.layout=pipelineLayout_;
		gp.renderPass=renderPass_; gp.subpass=0;
		VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &flexibleShapePipeline_));

		// Create a depth-disabled overlay variant for debug drawing (render "through" meshes)
		VkPipelineDepthStencilStateCreateInfo dsOverlay = ds;
		dsOverlay.depthTestEnable = VK_FALSE;
		dsOverlay.depthWriteEnable = VK_FALSE;
		dsOverlay.depthCompareOp = VK_COMPARE_OP_ALWAYS;
		gp.pDepthStencilState = &dsOverlay;
		VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &flexibleShapeOverlayPipeline_));

		vkDestroyShaderModule(device_, vs, nullptr);
		vkDestroyShaderModule(device_, fs, nullptr);
	}

	void VkApp::createRiggedPipeline()
	{
		if (riggedPipeline_ != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device_, riggedPipeline_, nullptr);
			riggedPipeline_ = VK_NULL_HANDLE;
		}
		if (riggedPipelineLayout_ != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device_, riggedPipelineLayout_, nullptr);
			riggedPipelineLayout_ = VK_NULL_HANDLE;
		}

		if (descriptorSetLayout_ == VK_NULL_HANDLE || textureSetLayout_ == VK_NULL_HANDLE)
		{
			return;
		}

		auto vsCode = readFile(findShaderPath("rigged_mesh.vert.spv"));
		auto fsCode = readFile(findShaderPath("rigged_mesh.frag.spv"));
		auto mkModule = [&](const std::vector<char>& code)->VkShaderModule{
			VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			smi.codeSize = code.size();
			smi.pCode = reinterpret_cast<const uint32_t*>(code.data());
			VkShaderModule m{}; VK_CHECK(vkCreateShaderModule(device_, &smi, nullptr, &m)); return m;
		};
		VkShaderModule vs = mkModule(vsCode), fs = mkModule(fsCode);

		VkDescriptorSetLayout setLayouts[2] = { descriptorSetLayout_, textureSetLayout_ };
		VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
		plci.setLayoutCount = 2;
		plci.pSetLayouts = setLayouts;
		VK_CHECK(vkCreatePipelineLayout(device_, &plci, nullptr, &riggedPipelineLayout_));

		VkVertexInputBindingDescription binds[2]{};
		binds[0].binding = 0; binds[0].stride = sizeof(Vertex);   binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		binds[1].binding = 1; binds[1].stride = sizeof(Instance); binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		std::array<VkVertexInputAttributeDescription, 9> attrs{};
		attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
		attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, nrm)};
		attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};
		attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*0};
		attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*1};
		attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*2};
		attrs[6] = {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*3};
		attrs[7] = {7, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, color)};
		attrs[8] = {8, 1, VK_FORMAT_R32_SFLOAT, offsetof(Instance, shapeType)};

		VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		vi.vertexBindingDescriptionCount = 2;
		vi.pVertexBindingDescriptions = binds;
		vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
		vi.pVertexAttributeDescriptions = attrs.data();

		VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkViewport vp{0,0,(float)swapChainExtent_.width,(float)swapChainExtent_.height, 0.0f, 1.0f};
		VkRect2D sc{{0,0}, swapChainExtent_};
		VkPipelineViewportStateCreateInfo vpci{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		vpci.viewportCount = 1; vpci.pViewports = &vp;
		vpci.scissorCount = 1; vpci.pScissors = &sc;

		VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_BACK_BIT;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS;

		VkPipelineColorBlendAttachmentState cba{};
		cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		cb.attachmentCount = 1;
		cb.pAttachments = &cba;

		VkPipelineShaderStageCreateInfo sVS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sVS.stage = VK_SHADER_STAGE_VERTEX_BIT; sVS.module = vs; sVS.pName = "main";
		VkPipelineShaderStageCreateInfo sFS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sFS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; sFS.module = fs; sFS.pName = "main";
		VkPipelineShaderStageCreateInfo stages[2]{sVS, sFS};

		VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		gp.stageCount = 2; gp.pStages = stages;
		gp.pVertexInputState = &vi;
		gp.pInputAssemblyState = &ia;
		gp.pViewportState = &vpci;
		gp.pRasterizationState = &rs;
		gp.pMultisampleState = &ms;
		gp.pDepthStencilState = &ds;
		gp.pColorBlendState = &cb;
		gp.layout = riggedPipelineLayout_;
		gp.renderPass = renderPass_;

		if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &riggedPipeline_) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create rigged graphics pipeline");
		}

		vkDestroyShaderModule(device_, vs, nullptr);
		vkDestroyShaderModule(device_, fs, nullptr);
	}

	void VkApp::createWireframePipeline()
	{
		if (!supportsNonSolidFill_)
		{
			wireframePipeline_ = VK_NULL_HANDLE;
			return;
		}

		// --- Shaders
		auto vsCode = readFile(findShaderPath("wireframe.vert.spv"));
		auto fsCode = readFile(findShaderPath("wireframe.frag.spv"));
		auto mkModule = [&](const std::vector<char>& code)->VkShaderModule{
			VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			smi.codeSize = code.size();
			smi.pCode = reinterpret_cast<const uint32_t*>(code.data());
			VkShaderModule m{}; VK_CHECK(vkCreateShaderModule(device_, &smi, nullptr, &m)); return m;
		};
		VkShaderModule vs = mkModule(vsCode), fs = mkModule(fsCode);

		// --- Vertex input: binding 0 = verts, binding 1 = instance data
		VkVertexInputBindingDescription binds[2]{};
		binds[0].binding = 0; binds[0].stride = sizeof(Vertex);   binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		binds[1].binding = 1; binds[1].stride = sizeof(Instance); binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		std::array<VkVertexInputAttributeDescription, 7> attrs{};
		// pos (0) + nrm(1)
		attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
		attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, nrm)};
		// model matrix columns (2..5) from binding 1
		attrs[2] = {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*0};
		attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*1};
		attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*2};
		attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*3};
		// color (6)
		attrs[6] = {6, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, color)};

		VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds;
		vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();

		// Input assembly - use triangle list for wireframe (edges will be drawn as lines)
		VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		// Viewport/scissor
		VkViewport vp{0,0,(float)swapChainExtent_.width,(float)swapChainExtent_.height, 0.0f, 1.0f};
		VkRect2D sc{{0,0}, swapChainExtent_};
		VkPipelineViewportStateCreateInfo vpci{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		vpci.viewportCount=1; vpci.pViewports=&vp; vpci.scissorCount=1; vpci.pScissors=&sc;

		// Rasterization - wireframe mode
		VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rs.polygonMode = VK_POLYGON_MODE_LINE;
		rs.cullMode = VK_CULL_MODE_NONE;  // Don't cull for wireframe
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = supportsWideLines_ ? 2.0f : 1.0f;

		// Multisample
		VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Colour blend
		VkPipelineColorBlendAttachmentState cba{};
		cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		cb.attachmentCount=1; cb.pAttachments=&cba;

		// Depth testing
		VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS;

		// Shaders stages
		VkPipelineShaderStageCreateInfo sVS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sVS.stage = VK_SHADER_STAGE_VERTEX_BIT; sVS.module=vs; sVS.pName="main";
		VkPipelineShaderStageCreateInfo sFS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sFS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; sFS.module=fs; sFS.pName="main";
		VkPipelineShaderStageCreateInfo stages[2]={sVS,sFS};

		// Pipeline
		VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		gp.stageCount=2; gp.pStages=stages;
		gp.pVertexInputState=&vi;
		gp.pInputAssemblyState=&ia;
		gp.pViewportState=&vpci;
		gp.pRasterizationState=&rs;
		gp.pMultisampleState=&ms;
		gp.pColorBlendState=&cb;
		gp.pDepthStencilState = &ds;
		gp.layout=pipelineLayout_;
		gp.renderPass=renderPass_; gp.subpass=0;
		VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &wireframePipeline_));
		vkDestroyShaderModule(device_, vs, nullptr);
		vkDestroyShaderModule(device_, fs, nullptr);
	}

	void VkApp::createUnlitPipeline()
	{
		// --- Shaders
		auto vsCode = readFile(findShaderPath("unlit.vert.spv"));
		auto fsCode = readFile(findShaderPath("unlit.frag.spv"));
		auto mkModule = [&](const std::vector<char>& code)->VkShaderModule{
			VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			smi.codeSize = code.size();
			smi.pCode = reinterpret_cast<const uint32_t*>(code.data());
			VkShaderModule m{}; VK_CHECK(vkCreateShaderModule(device_, &smi, nullptr, &m)); return m;
		};
		VkShaderModule vs = mkModule(vsCode), fs = mkModule(fsCode);

		// --- Vertex input: binding 0 = verts, binding 1 = instance data
		VkVertexInputBindingDescription binds[2]{};
		binds[0].binding = 0; binds[0].stride = sizeof(Vertex);   binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		binds[1].binding = 1; binds[1].stride = sizeof(Instance); binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		std::array<VkVertexInputAttributeDescription, 7> attrs{};
		// pos (0) + nrm(1)
		attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
		attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, nrm)};
		// model matrix columns (2..5) from binding 1
		attrs[2] = {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*0};
		attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*1};
		attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*2};
		attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*3};
		// color (6)
		attrs[6] = {6, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, color)};

		VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds;
		vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();

		// Input assembly
		VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		// Viewport/scissor
		VkViewport vp{0,0,(float)swapChainExtent_.width,(float)swapChainExtent_.height, 0.0f, 1.0f};
		VkRect2D sc{{0,0}, swapChainExtent_};
		VkPipelineViewportStateCreateInfo vpci{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		vpci.viewportCount=1; vpci.pViewports=&vp; vpci.scissorCount=1; vpci.pScissors=&sc;

		// Rasterization
		VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_BACK_BIT;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		// Multisample
		VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Colour blend
		VkPipelineColorBlendAttachmentState cba{};
		cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		cb.attachmentCount=1; cb.pAttachments=&cba;

		// Depth testing
		VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS;

		// Shaders stages
		VkPipelineShaderStageCreateInfo sVS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sVS.stage = VK_SHADER_STAGE_VERTEX_BIT; sVS.module=vs; sVS.pName="main";
		VkPipelineShaderStageCreateInfo sFS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		sFS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; sFS.module=fs; sFS.pName="main";
		VkPipelineShaderStageCreateInfo stages[2]={sVS,sFS};

		// Pipeline
		VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		gp.stageCount=2; gp.pStages=stages;
		gp.pVertexInputState=&vi;
		gp.pInputAssemblyState=&ia;
		gp.pViewportState=&vpci;
		gp.pRasterizationState=&rs;
		gp.pMultisampleState=&ms;
		gp.pColorBlendState=&cb;
		gp.pDepthStencilState = &ds;
		gp.layout=pipelineLayout_;
		gp.renderPass=renderPass_; gp.subpass=0;
		VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &unlitPipeline_));
		vkDestroyShaderModule(device_, vs, nullptr);
		vkDestroyShaderModule(device_, fs, nullptr);
	}

	void VkApp::createLinePipeline()
	{
		// --- Shaders
		auto vsCode = readFile(findShaderPath("unlit.vert.spv"));  // Use unlit shader for lines
		auto fsCode = readFile(findShaderPath("unlit.frag.spv"));
		auto mkModule = [&](const std::vector<char>& code)->VkShaderModule{
			VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			smi.codeSize = code.size();
			smi.pCode = reinterpret_cast<const uint32_t*>(code.data());
			VkShaderModule m{}; VK_CHECK(vkCreateShaderModule(device_, &smi, nullptr, &m)); return m;
		};
		VkShaderModule vs = mkModule(vsCode), fs = mkModule(fsCode);

		// --- Vertex input: binding 0 = verts, binding 1 = instance data
		VkVertexInputBindingDescription binds[2]{};
		binds[0].binding = 0; binds[0].stride = sizeof(Vertex);   binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		binds[1].binding = 1; binds[1].stride = sizeof(Instance); binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		std::array<VkVertexInputAttributeDescription, 7> attrs{};
		// pos (0) + nrm(1)
		attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
		attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, nrm)};
		// model matrix columns (2..5) from binding 1
		attrs[2] = {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*0};
		attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*1};
		attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*2};
		attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*3};
		// color (6)
		attrs[6] = {6, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, color)};

		VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds;
		vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();

		// Input assembly - use line list for proper line rendering
		VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

		// Viewport/scissor
		VkViewport vp{0,0,(float)swapChainExtent_.width,(float)swapChainExtent_.height, 0.0f, 1.0f};
		VkRect2D sc{{0,0}, swapChainExtent_};
		VkPipelineViewportStateCreateInfo vpci{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		vpci.viewportCount=1; vpci.pViewports=&vp; vpci.scissorCount=1; vpci.pScissors=&sc;

		// Rasterization - line mode
		VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rs.polygonMode = VK_POLYGON_MODE_LINE;
		rs.cullMode = VK_CULL_MODE_NONE;  // Don't cull for lines
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = supportsWideLines_ ? 5.0f : 1.0f;

		// Multisample
		VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Colour blend
		VkPipelineColorBlendAttachmentState cba{};
		cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		cb.attachmentCount=1; cb.pAttachments=&cba;

		// Depth testing
		VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS;

		// Dynamic state
		VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dsi{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
		dsi.dynamicStateCount = 2; dsi.pDynamicStates = dynStates;

		// Shader stages
		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vs; stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fs; stages[1].pName = "main";

		VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		gp.stageCount = 2; gp.pStages = stages;
		gp.pVertexInputState = &vi;
		gp.pInputAssemblyState = &ia;
		gp.pViewportState = &vpci;
		gp.pRasterizationState = &rs;
		gp.pMultisampleState=&ms;
		gp.pColorBlendState=&cb;
		gp.pDepthStencilState = &ds;
		gp.pDynamicState = &dsi;
		gp.layout=pipelineLayout_;
		gp.renderPass=renderPass_; gp.subpass=0;
		VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &linePipeline_));
		vkDestroyShaderModule(device_, vs, nullptr);
		vkDestroyShaderModule(device_, fs, nullptr);
	}

	void VkApp::createCommandPool()
	{
		VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		cpci.queueFamilyIndex = qFamGfx;
		cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK(vkCreateCommandPool(device_, &cpci, nullptr, &commandPool_));
	}

	void VkApp::createDepthResources()
	{
		// Pick a supported depth format
		VkFormat depthFormat = findDepthFormat();

		// Create depth image and allocate device-local memory
		createImage(
			swapChainExtent_.width,
			swapChainExtent_.height,
			depthFormat,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			depthImage_,
			depthImageMemory_,
			VK_SAMPLE_COUNT_1_BIT
		);

		// Create a view for the depth image
		VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (hasStencilComponent(depthFormat))
		{
			aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		depthImageView_ = createImageView(depthImage_, depthFormat, aspect);

		// Transition to a layout suitable for using as a depth attachment
		transitionImageLayout(
			depthImage_,
			depthFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		);
	}

	void VkApp::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
	{
		VkCommandBuffer cmd = beginSingleTimeCommands();

		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;

		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (hasStencilComponent(format))
			{
				barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
		}
		else
		{
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}

		VkPipelineStageFlags srcStage{};
		VkPipelineStageFlags dstStage{};

		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		}
		else
		{
			throw std::runtime_error("unsupported layout transition");
		}

		vkCmdPipelineBarrier(
			cmd,
			srcStage,
			dstStage,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		endSingleTimeCommands(cmd);
	}

	VkFormat VkApp::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
	{
		for (VkFormat format : candidates)
		{
			VkFormatProperties props{};
			vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &props);

			if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
			{
				return format;
			}
			else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
			{
				return format;
			}
		}

		throw std::runtime_error("failed to find supported image format");
	}

	VkFormat VkApp::findDepthFormat()
	{
		// Prefer higher precision if available
		return findSupportedFormat(
			{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
			VK_IMAGE_TILING_OPTIMAL,
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
		);
	}

	bool VkApp::hasStencilComponent(VkFormat format)
	{
		return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
	}

	void VkApp::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
		VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory,
		VkSampleCountFlagBits samples)
	{
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = width;
		imageInfo.extent.height = height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = format;
		imageInfo.tiling = tiling;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = usage;
		imageInfo.samples = samples;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateImage(device_, &imageInfo, nullptr, &image) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create image");
		}

		VkMemoryRequirements memReq{};
		vkGetImageMemoryRequirements(device_, image, &memReq);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);

		if (vkAllocateMemory(device_, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to allocate image memory");
		}

		vkBindImageMemory(device_, image, imageMemory, 0);
	}

VkImageView VkApp::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask)
{
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;

		viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		viewInfo.subresourceRange.aspectMask = aspectMask;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		VkImageView view = VK_NULL_HANDLE;
	if (vkCreateImageView(device_, &viewInfo, nullptr, &view) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create image view");
	}
	return view;
}

void VkApp::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
	VkCommandBuffer cmd = beginSingleTimeCommands();

	VkBufferImageCopy region{};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = {0, 0, 0};
	region.imageExtent = {width, height, 1};

	vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	endSingleTimeCommands(cmd);
}

	VkCommandBuffer VkApp::beginSingleTimeCommands()
	{
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = commandPool_;
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer cmd{};
		if (vkAllocateCommandBuffers(device_, &allocInfo, &cmd) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to allocate command buffer");
		}

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to begin command buffer");
		}

		return cmd;
	}

	void VkApp::endSingleTimeCommands(VkCommandBuffer cmd)
	{
		if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to record command buffer");
		}

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmd;

		if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to submit single-time command buffer");
		}
		vkQueueWaitIdle(graphicsQueue_);

		vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
	}

	void VkApp::createFramebuffers()
	{
		logMessage(LogLevel::Debug,
		           "Creating framebuffers for " + std::to_string(swapChainImageViews_.size()) +
		           " swapchain views");
		for (auto view : swapChainImageViews_)
		{
			VkImageView atts[] = { view, depthImageView_ };
			VkFramebufferCreateInfo fbi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
			fbi.renderPass = renderPass_;
			fbi.attachmentCount=2; fbi.pAttachments=atts;
			fbi.width=swapChainExtent_.width; fbi.height=swapChainExtent_.height; fbi.layers=1;
			VkFramebuffer fb{};
			VK_CHECK(vkCreateFramebuffer(device_, &fbi, nullptr, &fb));
			swapChainFramebuffers_.push_back(fb);
		}

		destroyBuffer(device_, vbo);
		destroyBuffer(device_, ibo);
		destroyBuffer(device_, instanceBuf);
		vertexBuffer_ = VK_NULL_HANDLE;
		indexBuffer_ = VK_NULL_HANDLE;
		instanceBuffer_ = VK_NULL_HANDLE;
		indexCount_ = 0;
		instanceCount_ = 0;

		// --- Geometry buffers - Generate separate geometry for each shape type
		std::vector<Vertex> allVertices;
		std::vector<uint32_t> allIndices;

		// Generate geometry for each shape type
		generateAllShapeGeometry(allVertices, allIndices);

		indexCount = (uint32_t) allIndices.size();
		logMessage(LogLevel::Debug, "Generated indexed geometry with " + std::to_string(indexCount) + " indices");

		VkDeviceSize vBytes = sizeof(Vertex) * allVertices.size();
		VkDeviceSize iBytes = sizeof(uint32_t) * allIndices.size();
		createBuffer(vBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vbo);
		createBuffer(iBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ibo);

		void* ptr=nullptr;
		VK_CHECK(vkMapMemory(device_, vbo.memory, 0, vBytes, 0, &ptr));
		std::memcpy(ptr, allVertices.data(), (size_t)vBytes);
		vkUnmapMemory(device_, vbo.memory);
		VK_CHECK(vkMapMemory(device_, ibo.memory, 0, iBytes, 0, &ptr));
		std::memcpy(ptr, allIndices.data(), (size_t)iBytes);
		vkUnmapMemory(device_, ibo.memory);

		// Store shape geometry offsets and counts for rendering
		storeShapeGeometryOffsets();

		// --- Assign geometry buffers to the pipeline-used handles
		vertexBuffer_ = vbo.buffer;
		indexBuffer_ = ibo.buffer;
		// Assign counts to member variables for draw calls
		indexCount_ = indexCount;
		instanceCount = static_cast<uint32_t>(_objects_.size());
		instanceCount_ = instanceCount;
		if (instanceCount > 0)
		{
			VkDeviceSize instBytes = sizeof(Instance) * instanceCount;
			createBuffer(instBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, instanceBuf);
			instanceBuffer_ = instanceBuf.buffer;
			VK_CHECK(vkMapMemory(device_, instanceBuf.memory, 0, instBytes, 0, &ptr));

			std::vector<Instance> instances(instanceCount);
			for (size_t i = 0; i < _objects_.size() && i < instanceCount; ++i)
			{
				const auto& obj = _objects_[i];
				glm::mat4 translation = glm::translate(glm::mat4(1.0f), obj.getPosition());
				glm::mat4 rotation = glm::mat4_cast(obj.getRotation());
				glm::mat4 scale = glm::scale(glm::mat4(1.0f), obj.getSize());

				instances[i].model = translation * rotation * scale;
				instances[i].color = glm::vec3(obj.getColour());
				instances[i].shapeType = static_cast<float>(obj._type);
			}

			std::memcpy(ptr, instances.data(), static_cast<size_t>(instBytes));
			vkUnmapMemory(device_, instanceBuf.memory);
		}
		logMessage(LogLevel::Debug,
		           "Finished framebuffer setup with index count " + std::to_string(indexCount));
	}

	// Old signature preserved for compatibility with existing call sites
	void VkApp::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
		Buffer& out)
	{
		// Create using the raw helper
		createBufferRaw(size, usage, properties, out.buffer, out.memory);
		out.size = size;
	}

	// New raw helper that the rest of this file can use
	void VkApp::createBufferRaw(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
								VkBuffer& buffer, VkDeviceMemory& memory)
	{
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create buffer");
		}

		VkMemoryRequirements memReq{};
		vkGetBufferMemoryRequirements(device_, buffer, &memReq);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);

		if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to allocate buffer memory");
		}

		vkBindBufferMemory(device_, buffer, memory, 0);
	}


	void VkApp::createUniformBuffers()
	{
		VkDeviceSize bufferSize = sizeof(UniformBufferObject);
		VkDeviceSize lightingBufferSize = sizeof(LightingBufferObject);
		size_t count = swapChainImages_.size();

		uniformBuffers2_.resize(count);
		uniformBuffersMapped_.resize(count, nullptr);
		lightingBuffers_.resize(count);
		lightingBuffersMapped_.resize(count, nullptr);

		for (size_t i = 0; i < count; i++)
		{
			createBuffer(
				bufferSize,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				uniformBuffers2_[i]
			);

			void* data = nullptr;
			VkResult mapRes = vkMapMemory(
				device_,
				uniformBuffers2_[i].memory,
				0,
				bufferSize,
				0,
				&data
			);
			if (mapRes != VK_SUCCESS || data == nullptr)
			{
				throw std::runtime_error("failed to map uniform buffer memory");
			}
			uniformBuffersMapped_[i] = data;

			createBuffer(
				lightingBufferSize,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				lightingBuffers_[i]
			);

			void* lightingData = nullptr;
			mapRes = vkMapMemory(
				device_,
				lightingBuffers_[i].memory,
				0,
				lightingBufferSize,
				0,
				&lightingData
			);
			if (mapRes != VK_SUCCESS || lightingData == nullptr)
			{
				throw std::runtime_error("failed to map lighting uniform buffer memory");
			}
			lightingBuffersMapped_[i] = lightingData;
		}
	}

	void VkApp::destroyBuffer(VkDevice device, Buffer& buf)
	{
		if (buf.buffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, buf.buffer, nullptr);
			buf.buffer = VK_NULL_HANDLE;
		}
		if (buf.memory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, buf.memory, nullptr);
			buf.memory = VK_NULL_HANDLE;
		}
		buf.size = 0;
	}

	void VkApp::createTextureDescriptorPool()
	{
		if (textureDescriptorPool_ != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device_, textureDescriptorPool_, nullptr);
			textureDescriptorPool_ = VK_NULL_HANDLE;
		}

		VkDescriptorPoolSize poolSize{};
		poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSize.descriptorCount = 256;

		VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		poolInfo.maxSets = 256;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

		VK_CHECK(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &textureDescriptorPool_));
	}

	void VkApp::destroyTextureResources()
	{
		for (auto& entry : textureCache_)
		{
			if (entry.second)
			{
				destroyTexture(*entry.second);
			}
		}
		textureCache_.clear();

		if (defaultTexture_)
		{
			destroyTexture(*defaultTexture_);
			defaultTexture_.reset();
		}

		if (textureDescriptorPool_ != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device_, textureDescriptorPool_, nullptr);
			textureDescriptorPool_ = VK_NULL_HANDLE;
		}
	}

	std::shared_ptr<Texture> VkApp::createTextureFromPixels(const void* pixels, uint32_t width, uint32_t height)
	{
		if (!pixels || width == 0 || height == 0)
		{
			return nullptr;
		}

		Buffer staging{};
		VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;
		createBuffer(
			imageSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			staging
		);

		void* data = nullptr;
		VK_CHECK(vkMapMemory(device_, staging.memory, 0, imageSize, 0, &data));
		std::memcpy(data, pixels, static_cast<size_t>(imageSize));
		vkUnmapMemory(device_, staging.memory);

		auto texture = std::make_shared<Texture>();
		createImage(
			width,
			height,
			VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			texture->image,
			texture->memory
		);

		transitionImageLayout(
			texture->image,
			VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
		);
		copyBufferToImage(staging.buffer, texture->image, width, height);
		transitionImageLayout(
			texture->image,
			VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		);

		destroyBuffer(device_, staging);

		texture->view = createImageView(texture->image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

		VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

		VK_CHECK(vkCreateSampler(device_, &samplerInfo, nullptr, &texture->sampler));

		if (textureDescriptorPool_ == VK_NULL_HANDLE || textureSetLayout_ == VK_NULL_HANDLE)
		{
			throw std::runtime_error("Texture descriptor resources not initialized");
		}

		VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
		allocInfo.descriptorPool = textureDescriptorPool_;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &textureSetLayout_;

		VK_CHECK(vkAllocateDescriptorSets(device_, &allocInfo, &texture->descriptor));

		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = texture->view;
		imageInfo.sampler = texture->sampler;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = texture->descriptor;
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

		texture->width = width;
		texture->height = height;

		return texture;
	}

	std::shared_ptr<Texture> VkApp::createSolidColorTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
	{
		const uint8_t pixel[4] = {r, g, b, a};
		auto texture = createTextureFromPixels(pixel, 1, 1);
		if (texture)
		{
			texture->path = "solid_color";
		}
		return texture;
	}

	std::shared_ptr<Texture> VkApp::getOrCreateSolidColorTexture(const glm::vec4& color)
	{
		auto toChannel = [](float component) -> uint8_t
		{
			const float clamped = std::clamp(component, 0.0f, 1.0f);
			return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
		};

		const uint8_t r = toChannel(color.r);
		const uint8_t g = toChannel(color.g);
		const uint8_t b = toChannel(color.b);
		const uint8_t a = toChannel(color.a);

		std::ostringstream keyBuilder;
		keyBuilder << "solid_color:" << static_cast<int>(r) << ','
		           << static_cast<int>(g) << ','
		           << static_cast<int>(b) << ','
		           << static_cast<int>(a);
		const std::string cacheKey = keyBuilder.str();

		auto iter = textureCache_.find(cacheKey);
		if (iter != textureCache_.end() && iter->second)
		{
			return iter->second;
		}

		auto texture = createSolidColorTexture(r, g, b, a);
		if (texture)
		{
			texture->path = cacheKey;
			textureCache_[cacheKey] = texture;
		}
		return texture;
	}

std::shared_ptr<Texture> VkApp::createTextureFromFile(const std::string& path)
{
    int texWidth = 0;
    int texHeight = 0;
    int texChannels = 0;
		stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		if (!pixels)
		{
			logMessage(LogLevel::Warning, "Failed to load texture: " + path);
			return nullptr;
		}

		if (debugOutput)
		{
			std::ostringstream message;
			message << "[Texture] Loaded '" << path << "' (" << texWidth << "x" << texHeight
			        << ", channels=" << texChannels << ")";
			logMessage(LogLevel::Debug, message.str());
		}
		auto texture = createTextureFromPixels(pixels, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
		stbi_image_free(pixels);
		if (texture)
		{
			texture->path = path;
		}
    return texture;
}

std::shared_ptr<Texture> VkApp::createTextureFromEmbedded(const EmbeddedTextureData& source, const std::string& cacheKey)
{
    if (source.isRawPixels)
    {
        if (source.data.empty() || source.width == 0 || source.height == 0)
        {
            return nullptr;
        }
        auto texture = createTextureFromPixels(source.data.data(), source.width, source.height);
        if (texture)
        {
            texture->path = cacheKey;
        }
        return texture;
    }

    if (source.data.empty())
    {
        return nullptr;
    }

    int texWidth = 0;
    int texHeight = 0;
    int texChannels = 0;
    stbi_uc* pixels = stbi_load_from_memory(
        source.data.data(),
        static_cast<int>(source.data.size()),
        &texWidth,
        &texHeight,
        &texChannels,
        STBI_rgb_alpha
    );

	if (!pixels)
	{
		logMessage(LogLevel::Warning, "Failed to decode embedded texture data");
		return nullptr;
	}

    auto texture = createTextureFromPixels(pixels, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
    stbi_image_free(pixels);
    if (texture)
    {
        texture->path = cacheKey;
    }
    return texture;
}

	std::shared_ptr<Texture> VkApp::getOrCreateTexture(const std::string& path)
	{
		if (path.empty())
		{
			return nullptr;
		}

		if (path[0] == '*')
		{
			// Embedded texture marker – handled by createTextureFromEmbedded.
			return nullptr;
		}

		auto iter = textureCache_.find(path);
		if (iter != textureCache_.end() && iter->second)
		{
			return iter->second;
		}

		auto texture = createTextureFromFile(path);
		if (!texture)
		{
			return nullptr;
		}
		textureCache_[path] = texture;
		return texture;
	}

	void VkApp::destroyTexture(Texture& texture)
	{
		if (texture.descriptor != VK_NULL_HANDLE && textureDescriptorPool_ != VK_NULL_HANDLE)
		{
			vkFreeDescriptorSets(device_, textureDescriptorPool_, 1, &texture.descriptor);
			texture.descriptor = VK_NULL_HANDLE;
		}
		if (texture.sampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device_, texture.sampler, nullptr);
			texture.sampler = VK_NULL_HANDLE;
		}
		if (texture.view != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device_, texture.view, nullptr);
			texture.view = VK_NULL_HANDLE;
		}
		if (texture.image != VK_NULL_HANDLE)
		{
			vkDestroyImage(device_, texture.image, nullptr);
			texture.image = VK_NULL_HANDLE;
		}
		if (texture.memory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device_, texture.memory, nullptr);
			texture.memory = VK_NULL_HANDLE;
		}
		texture.width = texture.height = 0;
	}

	void VkApp::createDescriptorPool()
	{
		// --- Descriptors
		VkDescriptorPoolSize poolSize{
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			static_cast<uint32_t>(uniformBuffers2_.size() * 2)
		};
		VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
		dpci.maxSets = (uint32_t)uniformBuffers2_.size();
		dpci.poolSizeCount=1; dpci.pPoolSizes=&poolSize;
		VK_CHECK(vkCreateDescriptorPool(device_, &dpci, nullptr, &descriptorPool_));
	}

	void VkApp::createDescriptorSets()
	{
		std::vector<VkDescriptorSetLayout> layouts(uniformBuffers2_.size(), descriptorSetLayout_);
		VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
		dsai.descriptorPool = descriptorPool_;
		dsai.descriptorSetCount = (uint32_t)layouts.size();
		dsai.pSetLayouts = layouts.data();
		descriptorSets_.resize(layouts.size());
		VK_CHECK(vkAllocateDescriptorSets(device_, &dsai, descriptorSets_.data()));

		for (size_t i=0;i < uniformBuffers2_.size();++i)
		{
			VkDescriptorBufferInfo bi{uniformBuffers2_[i].buffer, 0, sizeof(UniformBufferObject)};
			VkDescriptorBufferInfo lightBi{lightingBuffers_[i].buffer, 0, sizeof(LightingBufferObject)};

			std::array<VkWriteDescriptorSet, 2> writes{};
			writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[0].dstSet = descriptorSets_[i];
			writes[0].dstBinding = 0;
			writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writes[0].descriptorCount = 1;
			writes[0].pBufferInfo = &bi;

			writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[1].dstSet = descriptorSets_[i];
			writes[1].dstBinding = 1;
			writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writes[1].descriptorCount = 1;
			writes[1].pBufferInfo = &lightBi;

			vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		}
	}

	void VkApp::createSceneResources()
	{
		// TODO: Create sphere mesh/instance buffers and any textures needed
		// No throw here so you can skip if you hook into your existing scene code elsewhere
	}

	void VkApp::setRenderMode(RenderMode mode)
	{
		currentRenderMode_ = mode;
	}

	VkPipeline VkApp::getCurrentPipeline()
	{
		switch (currentRenderMode_)
		{
			case RenderMode::FLEXIBLE_SHAPES:
				return (flexibleShapePipeline_ != VK_NULL_HANDLE) ? flexibleShapePipeline_ : graphicsPipeline_;
			case RenderMode::WIREFRAME:
				return (wireframePipeline_ != VK_NULL_HANDLE) ? wireframePipeline_ : flexibleShapePipeline_;
			case RenderMode::UNLIT:
				return (unlitPipeline_ != VK_NULL_HANDLE) ? unlitPipeline_ : flexibleShapePipeline_;
			case RenderMode::LINE:
				return (linePipeline_ != VK_NULL_HANDLE) ? linePipeline_ : unlitPipeline_;
			case RenderMode::ORIGINAL_SPHERES:
			default:
				return graphicsPipeline_;
		}
	}

	void VkApp::setShaderPath(const std::string& path)
	{
		customShaderPath_ = path;
	}

	void VkApp::createCommandBuffers()
	{
		// Free any old buffers first (safe if empty)
		if (!commandBuffers_.empty())
		{
			vkFreeCommandBuffers(
				device_,
				commandPool_,
				static_cast<uint32_t>(commandBuffers_.size()),
				commandBuffers_.data()
			);
			commandBuffers_.clear();
		}

		// Sanity: these must match
		if (swapChainFramebuffers_.size() != swapChainImages_.size())
		{
			throw std::runtime_error("createCommandBuffers: framebuffer count != swapchain images");
		}

		// Allocate one primary CB per swapchain image
		commandBuffers_.resize(swapChainImages_.size(), VK_NULL_HANDLE);

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = commandPool_;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());

		if (vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()) != VK_SUCCESS)
		{
			throw std::runtime_error("createCommandBuffers: allocation failed");
		}

		// Record each command buffer
		for (uint32_t i = 0; i < static_cast<uint32_t>(commandBuffers_.size()); i++)
		{
			recordCommandBuffer(commandBuffers_[i], i);
		}
	}


	uint32_t VkApp::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props)
	{
		VkPhysicalDeviceMemoryProperties mp{};
		vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mp);
		for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
		{
			if ((typeFilter & (1<<i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
				return i;
		}
		throw std::runtime_error("No suitable memory type");
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
			{ 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 },
			{ 2, 3, 7, 6 }, { 0, 3, 7, 4 }, { 1, 2, 6, 5 }
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

	// ---------------- Vulkan helpers ----------------
	std::vector<char> VkApp::readFile(const std::string& path)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
			throw std::runtime_error("Failed to open file: " + path);
		return std::vector<char>(std::istreambuf_iterator<char>(f), {});
	}

	std::string VkApp::findShaderPath(const std::string& shaderName)
	{
		std::vector<std::filesystem::path> searchRoots;
		searchRoots.reserve(16);

		auto addRoot = [&](const std::filesystem::path& root)
		{
			if (!root.empty())
			{
				searchRoots.push_back(root);
			}
		};

		if (!customShaderPath_.empty())
		{
			addRoot(customShaderPath_);
		}

		if (const auto envPath = getEnvironmentVariable("LIGHT_VULKAN_GRAPHICS_SHADER_PATH"))
		{
			addRoot(*envPath);
		}

#ifdef LVG_BUILD_SHADER_DIR
		addRoot(LVG_BUILD_SHADER_DIR);
#endif

		if (const auto libraryPath = getLibraryPath())
		{
			appendShaderSearchRoots(searchRoots, libraryPath->parent_path());
		}

		if (const auto executablePath = getExecutablePath())
		{
			appendShaderSearchRoots(searchRoots, executablePath->parent_path());
		}

		std::error_code currentPathError;
		const auto currentPath = std::filesystem::current_path(currentPathError);
		if (!currentPathError)
		{
			appendShaderSearchRoots(searchRoots, currentPath);
		}

#ifdef LVG_DEFAULT_SHADER_INSTALL_DIR
		addRoot(LVG_DEFAULT_SHADER_INSTALL_DIR);
#endif

		std::vector<std::string> searchedPaths;
		searchedPaths.reserve(searchRoots.size());
		std::unordered_set<std::string> visitedPaths;

		for (const auto& root : searchRoots)
		{
			const auto candidatePath = weaklyCanonicalPath(root / shaderName).value_or((root / shaderName).lexically_normal());
			const std::string canonicalKey = candidatePath.generic_string();
			if (!visitedPaths.insert(canonicalKey).second)
			{
				continue;
			}

			searchedPaths.push_back(candidatePath.string());
			if (std::ifstream(candidatePath, std::ios::binary).good())
			{
				if (debugOutput)
				{
					logMessage(LogLevel::Debug, "Found shader: " + candidatePath.string());
				}
				return candidatePath.string();
			}
		}

		std::ostringstream message;
		message << "Failed to locate shader '" << shaderName << "'. Searched:";
		for (const auto& path : searchedPaths)
		{
			message << "\n  - " << path;
		}

		throw std::runtime_error(message.str());
	}


	// ---------------- Object Management Functions ----------------

	size_t VkApp::addLight(const lightGraphics::LightSource& light)
	{
		LightSource normalizedLight = light;
		if (glm::length(normalizedLight.direction) > 0.0f)
		{
			normalizedLight.direction = glm::normalize(normalizedLight.direction);
		}
		else
		{
			normalizedLight.direction = glm::vec3(0.0f, -1.0f, 0.0f);
		}
		if (normalizedLight.outerConeAngleRadians < normalizedLight.innerConeAngleRadians)
		{
			std::swap(normalizedLight.innerConeAngleRadians, normalizedLight.outerConeAngleRadians);
		}

		lights_.push_back(normalizedLight);
		lightTransformMatrixOverrides_.push_back(std::nullopt);
		if (lights_.size() == lightGraphics::MaxForwardLights + 1)
		{
			logMessage(LogLevel::Warning,
			           "Only the first " + std::to_string(lightGraphics::MaxForwardLights) +
			           " lights are uploaded by the current forward renderer");
		}
		markLightingDirty();
		return lights_.size() - 1;
	}

	size_t VkApp::addDirectionalLight(const glm::vec3& direction,
	                                  const glm::vec3& color,
	                                  float intensity,
	                                  const std::string& name)
	{
		LightSource light;
		light.type = LightType::Directional;
		light.direction = direction;
		light.color = color;
		light.intensity = intensity;
		light.name = name;
		return addLight(light);
	}

	size_t VkApp::addPointLight(const glm::vec3& position,
	                            const glm::vec3& color,
	                            float intensity,
	                            float range,
	                            const std::string& name)
	{
		LightSource light;
		light.type = LightType::Point;
		light.position = position;
		light.color = color;
		light.intensity = intensity;
		light.range = range;
		light.name = name;
		return addLight(light);
	}

	size_t VkApp::addSpotLight(const glm::vec3& position,
	                           const glm::vec3& direction,
	                           const glm::vec3& color,
	                           float intensity,
	                           float range,
	                           float innerConeAngleRadians,
	                           float outerConeAngleRadians,
	                           const std::string& name)
	{
		LightSource light;
		light.type = LightType::Spot;
		light.position = position;
		light.direction = direction;
		light.color = color;
		light.intensity = intensity;
		light.range = range;
		light.innerConeAngleRadians = innerConeAngleRadians;
		light.outerConeAngleRadians = outerConeAngleRadians;
		light.name = name;
		return addLight(light);
	}

	void VkApp::removeLight(size_t index)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("removeLight", index, lights_.size()));
		}

		lights_.erase(lights_.begin() + static_cast<std::ptrdiff_t>(index));
		if (index < lightTransformMatrixOverrides_.size())
		{
			lightTransformMatrixOverrides_.erase(lightTransformMatrixOverrides_.begin() + static_cast<std::ptrdiff_t>(index));
		}
		sceneGraph_->onLightRemoved(index);
		markLightingDirty();
	}

	void VkApp::clearLights()
	{
		const size_t removedCount = lights_.size();
		lights_.clear();
		lightTransformMatrixOverrides_.clear();
		for (size_t i = 0; i < removedCount; ++i)
		{
			sceneGraph_->onLightRemoved(0);
		}
		markLightingDirty();
	}

	void VkApp::updateLight(size_t index, const lightGraphics::LightSource& light)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("updateLight", index, lights_.size()));
		}

		LightSource normalizedLight = light;
		if (glm::length(normalizedLight.direction) > 0.0f)
		{
			normalizedLight.direction = glm::normalize(normalizedLight.direction);
		}
		else
		{
			normalizedLight.direction = glm::vec3(0.0f, -1.0f, 0.0f);
		}
		if (normalizedLight.outerConeAngleRadians < normalizedLight.innerConeAngleRadians)
		{
			std::swap(normalizedLight.innerConeAngleRadians, normalizedLight.outerConeAngleRadians);
		}

		lights_[index] = normalizedLight;
		sceneGraph_->onLightChanged(index);
		markLightingDirty();
	}

	void VkApp::setLightPosition(size_t index, const glm::vec3& position)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("setLightPosition", index, lights_.size()));
		}
		lights_[index].position = position;
		sceneGraph_->onLightChanged(index);
		markLightingDirty();
	}

	void VkApp::setLightDirection(size_t index, const glm::vec3& direction)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("setLightDirection", index, lights_.size()));
		}
		lights_[index].direction = glm::length(direction) > 0.0f
			? glm::normalize(direction)
			: glm::vec3(0.0f, -1.0f, 0.0f);
		sceneGraph_->onLightChanged(index);
		markLightingDirty();
	}

	void VkApp::setLightColor(size_t index, const glm::vec3& color)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("setLightColor", index, lights_.size()));
		}
		lights_[index].color = color;
		markLightingDirty();
	}

	void VkApp::setLightIntensity(size_t index, float intensity)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("setLightIntensity", index, lights_.size()));
		}
		lights_[index].intensity = intensity;
		markLightingDirty();
	}

	void VkApp::setLightRange(size_t index, float range)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("setLightRange", index, lights_.size()));
		}
		lights_[index].range = range;
		markLightingDirty();
	}

	void VkApp::setLightEnabled(size_t index, bool enabled)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("setLightEnabled", index, lights_.size()));
		}
		lights_[index].enabled = enabled;
		markLightingDirty();
	}

	void VkApp::setAmbientLight(const glm::vec3& ambientColor)
	{
		ambientLight_ = ambientColor;
		markLightingDirty();
	}

	void VkApp::addObject(lightGraphics::pObject *newObject)
	{
		if (!newObject)
		{
			throw std::invalid_argument("addObject: object pointer is null");
		}

		_objects_.push_back(*newObject);

		// Initialize dirty tracking for new object
		dirtyObjects_.push_back(true);
		objectModelMatrixOverrides_.push_back(std::nullopt);
		instanceDataDirty_ = true;

		if (sceneFinalized_)
		{
			updateInstanceData(); // Update rendering data only if scene is finalized
		}
	}

	void VkApp::addObject(const lightGraphics::pObject& obj)
	{
		_objects_.push_back(obj);

		// Initialize dirty tracking for new object
		dirtyObjects_.push_back(true);
		objectModelMatrixOverrides_.push_back(std::nullopt);
		instanceDataDirty_ = true;

		if (sceneFinalized_)
		{
			updateInstanceData();
		}
	}

	void VkApp::addObject(lightGraphics::ShapeType type, const glm::vec3& position,
						const glm::vec3& size, const glm::vec4& color,
						const glm::quat& rotation, const std::string& name, float mass)
	{
		_objects_.emplace_back(type, position, size, color, rotation, name, mass);

		// Initialize dirty tracking for new object
		dirtyObjects_.push_back(true);
		objectModelMatrixOverrides_.push_back(std::nullopt);
		instanceDataDirty_ = true;

		if (sceneFinalized_)
		{
			updateInstanceData();
		}
	}

	size_t VkApp::addRiggedObject(const std::shared_ptr<RiggedObject>& riggedObject)
	{
		if (!riggedObject)
		{
			throw std::runtime_error("addRiggedObject: rigged object pointer is null");
		}

			auto model = riggedObject->getModel();
			if (!model)
			{
				throw std::runtime_error("addRiggedObject: rigged object has no loaded model");
			}

			RiggedInstanceRenderData instanceData;
			instanceData.object = riggedObject;
			instanceData.activeAnimationIndex = riggedObject->getCurrentAnimationIndex();
			instanceData.animationLoop = riggedObject->getAnimationLooping();

			instanceData.uprightCorrection = model->axisCorrection;

		for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
		{
			createBuffer(sizeof(Instance),
			             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			             instanceData.instanceBuffers[frameIndex]);
			VK_CHECK(vkMapMemory(device_,
			                     instanceData.instanceBuffers[frameIndex].memory,
			                     0,
			                     sizeof(Instance),
			                     0,
			                     &instanceData.instanceBufferMapped[frameIndex]));
		}

		for (const auto& mesh : model->meshes)
		{
			if (mesh.vertices.empty() || mesh.indices.empty())
			{
				continue;
			}
			if (!validateRiggedMesh(mesh))
			{
				logMessage(LogLevel::Warning,
				           "[RiggedMesh] Skipping mesh because imported data failed validation");
				continue;
			}

			RiggedMeshRenderData meshData;
			meshData.mesh = &mesh;
			meshData.indexCount = static_cast<uint32_t>(mesh.indices.size());
			meshData.skinnedVertices.resize(mesh.vertices.size());

			VkDeviceSize vbSize = sizeof(Vertex) * mesh.vertices.size();
			VkDeviceSize ibSize = sizeof(uint32_t) * mesh.indices.size();

			for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
			{
				createBuffer(vbSize,
				             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				             meshData.vertexBuffers[frameIndex]);
				VK_CHECK(vkMapMemory(device_,
				                     meshData.vertexBuffers[frameIndex].memory,
				                     0,
				                     vbSize,
				                     0,
				                     &meshData.vertexBufferMapped[frameIndex]));
			}
			createBuffer(ibSize,
			             VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			             meshData.indexBuffer);

			void* mapped = nullptr;
			VK_CHECK(vkMapMemory(device_, meshData.indexBuffer.memory, 0, ibSize, 0, &mapped));
			std::memcpy(mapped, mesh.indices.data(), static_cast<size_t>(ibSize));
			vkUnmapMemory(device_, meshData.indexBuffer.memory);

			if (!mesh.diffuseTexturePath.empty())
			{
				if (debugOutput)
				{
					logMessage(LogLevel::Debug, "[RiggedMesh] Loading texture: " + mesh.diffuseTexturePath);
				}
				meshData.texture = getOrCreateTexture(mesh.diffuseTexturePath);
			}

			if (!meshData.texture && mesh.embeddedTexture)
			{
				const std::string cacheKey = !mesh.embeddedTextureKey.empty()
					? mesh.embeddedTextureKey
					: mesh.diffuseTexturePath;

				if (!cacheKey.empty())
				{
					auto cacheIt = textureCache_.find(cacheKey);
					if (cacheIt != textureCache_.end())
					{
						meshData.texture = cacheIt->second;
					}
					else
					{
						meshData.texture = createTextureFromEmbedded(*mesh.embeddedTexture, cacheKey);
						if (meshData.texture)
						{
							textureCache_[cacheKey] = meshData.texture;
						}
					}
				}
				else
				{
					meshData.texture = createTextureFromEmbedded(*mesh.embeddedTexture, "embedded_texture");
				}
			}

			if (!meshData.texture)
			{
				meshData.texture = getOrCreateSolidColorTexture(mesh.diffuseColor);
				if (debugOutput)
				{
					std::ostringstream message;
					message << "[RiggedMesh] Using material color fallback for '" << mesh.materialName
					        << "' (" << mesh.diffuseColor.r << ", " << mesh.diffuseColor.g << ", "
					        << mesh.diffuseColor.b << ", " << mesh.diffuseColor.a << ")";
					logMessage(LogLevel::Debug, message.str());
				}
			}

			if (!meshData.texture)
			{
				if (debugOutput)
				{
					logMessage(LogLevel::Debug, "[RiggedMesh] Texture and material color fallback unavailable; using default texture");
				}
				meshData.texture = defaultTexture_;
			}

			instanceData.meshes.push_back(std::move(meshData));
		}

		riggedInstances_.push_back(std::move(instanceData));

		// Populate buffers with the current pose
		updateRiggedInstances();

		if (sceneFinalized_)
		{
			createCommandBuffers();
		}

		return riggedInstances_.size() - 1;
	}

	void VkApp::removeRiggedObject(size_t index)
	{
		if (index >= riggedInstances_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("removeRiggedObject", index, riggedInstances_.size()));
		}

		if (sceneFinalized_ && device_ != VK_NULL_HANDLE)
		{
			VK_CHECK(vkDeviceWaitIdle(device_));
		}

		destroyRiggedInstance(riggedInstances_[index]);
		riggedInstances_.erase(riggedInstances_.begin() + static_cast<std::ptrdiff_t>(index));
		sceneGraph_->onRiggedObjectRemoved(index);
	}

	glm::mat4 VkApp::getObjectModelMatrix(size_t index) const
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("getObjectModelMatrix", index, _objects_.size()));
		}

		if (index < objectModelMatrixOverrides_.size() && objectModelMatrixOverrides_[index])
		{
			return *objectModelMatrixOverrides_[index];
		}

		const auto& obj = _objects_[index];
		const glm::mat4 translation = glm::translate(glm::mat4(1.0f), obj.getPosition());
		const glm::mat4 rotation = glm::mat4_cast(obj.getRotation());
		const glm::mat4 scale = glm::scale(glm::mat4(1.0f), obj.getSize());
		return translation * rotation * scale;
	}

	void VkApp::setObjectModelMatrixOverride(size_t index, const glm::mat4& model)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("setObjectModelMatrixOverride", index, _objects_.size()));
		}

		if (objectModelMatrixOverrides_.size() < _objects_.size())
		{
			objectModelMatrixOverrides_.resize(_objects_.size());
		}
		objectModelMatrixOverrides_[index] = model;

		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
		else
		{
			instanceDataDirty_ = true;
		}
	}

	void VkApp::clearObjectModelMatrixOverride(size_t index)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("clearObjectModelMatrixOverride", index, _objects_.size()));
		}

		clearObjectModelMatrixOverrideInternal(index);
		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
		else
		{
			instanceDataDirty_ = true;
		}
	}

	void VkApp::setLightTransformMatrixOverride(size_t index, const glm::mat4& transform)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("setLightTransformMatrixOverride", index, lights_.size()));
		}

		if (lightTransformMatrixOverrides_.size() < lights_.size())
		{
			lightTransformMatrixOverrides_.resize(lights_.size());
		}
		lightTransformMatrixOverrides_[index] = transform;
		markLightingDirty();
	}

	void VkApp::clearLightTransformMatrixOverride(size_t index)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("clearLightTransformMatrixOverride", index, lights_.size()));
		}

		if (index < lightTransformMatrixOverrides_.size())
		{
			lightTransformMatrixOverrides_[index].reset();
		}
		markLightingDirty();
	}

	void VkApp::setRiggedObjectTransformMatrixOverride(size_t index, const glm::mat4& transform)
	{
		if (index >= riggedInstances_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("setRiggedObjectTransformMatrixOverride", index, riggedInstances_.size()));
		}
		riggedInstances_[index].transformMatrixOverride = transform;
	}

	void VkApp::clearRiggedObjectTransformMatrixOverride(size_t index)
	{
		if (index >= riggedInstances_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("clearRiggedObjectTransformMatrixOverride", index, riggedInstances_.size()));
		}
		riggedInstances_[index].transformMatrixOverride.reset();
	}

	SceneGraph& VkApp::sceneGraph()
	{
		return *sceneGraph_;
	}

	const SceneGraph& VkApp::sceneGraph() const
	{
		return *sceneGraph_;
	}

	// Object update methods for physics simulation
	void VkApp::setObjectPosition(size_t index, const glm::vec3& position)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("setObjectPosition", index, _objects_.size()));
		}

		_objects_[index].setPosition(position);
		clearObjectModelMatrixOverrideInternal(index);
		sceneGraph_->onObjectChanged(index);
		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
	}

	void VkApp::setObjectScale(size_t index, const glm::vec3& scale)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("setObjectScale", index, _objects_.size()));
		}

		_objects_[index].setSize(scale);
		clearObjectModelMatrixOverrideInternal(index);
		sceneGraph_->onObjectChanged(index);
		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
	}

	void VkApp::setObjectRotation(size_t index, const glm::quat& rotation)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("setObjectRotation", index, _objects_.size()));
		}

		_objects_[index].setRotation(rotation);
		clearObjectModelMatrixOverrideInternal(index);
		sceneGraph_->onObjectChanged(index);
		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
	}

	void VkApp::setObjectColor(size_t index, const glm::vec4& color)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("setObjectColor", index, _objects_.size()));
		}

		_objects_[index].setColour(color);
		sceneGraph_->onObjectChanged(index);
		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
	}

	// Update multiple object properties at once for efficiency
	void VkApp::updateObjectProperties(size_t index, const glm::vec3& position,
									const glm::vec3& scale, const glm::quat& rotation)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("updateObjectProperties", index, _objects_.size()));
		}

		_objects_[index].setPosition(position);
		_objects_[index].setSize(scale);
		_objects_[index].setRotation(rotation);
		clearObjectModelMatrixOverrideInternal(index);
		sceneGraph_->onObjectChanged(index);

		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
	}

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

	void VkApp::removeObject(size_t index)
	{
		if (index < _objects_.size())
		{
			_objects_.erase(_objects_.begin() + index);
			if (index < dirtyObjects_.size())
			{
				dirtyObjects_.erase(dirtyObjects_.begin() + index);
			}
			if (index < objectModelMatrixOverrides_.size())
			{
				objectModelMatrixOverrides_.erase(objectModelMatrixOverrides_.begin() + index);
			}
			if (index < instanceDataCache_.size())
			{
				instanceDataCache_.erase(instanceDataCache_.begin() + index);
			}
			sceneGraph_->onObjectRemoved(index);
			instanceDataDirty_ = true;
			if (sceneFinalized_)
			{
				updateInstanceData();
			}
		}
	}

	void VkApp::addHexahedral(const glm::vec3& position, const glm::vec3& size,
							const glm::vec4& color,
							const glm::quat& rotation,
							const std::string& name,
							float mass)
	{
		addObject(lightGraphics::ShapeType::HEX, position, size, color, rotation, name, mass);
	}

	void VkApp::clearObjects()
	{
		const size_t removedCount = _objects_.size();
		_objects_.clear();
		dirtyObjects_.clear();
		objectModelMatrixOverrides_.clear();
		instanceDataCache_.clear();
		for (size_t i = 0; i < removedCount; ++i)
		{
			sceneGraph_->onObjectRemoved(0);
		}
		instanceDataDirty_ = true;
		if (sceneFinalized_)
		{
			updateInstanceData();
		}
	}

	void VkApp::updateObject(size_t index, const lightGraphics::pObject& obj)
	{
		if (index < _objects_.size())
		{
			_objects_[index] = obj;
			clearObjectModelMatrixOverrideInternal(index);
			sceneGraph_->onObjectChanged(index);
			if (sceneFinalized_)
			{
				markObjectDirty(index);
			}
		}
	}


	// ==================== PERFORMANCE OPTIMIZATION METHODS ====================

	void VkApp::markObjectDirty(size_t index)
	{
		if (index >= dirtyObjects_.size())
		{
			dirtyObjects_.resize(_objects_.size(), false);
		}
		dirtyObjects_[index] = true;
		instanceDataDirty_ = true;
	}

	void VkApp::markLightingDirty()
	{
		lightingDataDirty_ = true;
	}

	LightSource VkApp::lightForUpload(size_t index) const
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("lightForUpload", index, lights_.size()));
		}

		LightSource light = lights_[index];
		if (index < lightTransformMatrixOverrides_.size() && lightTransformMatrixOverrides_[index])
		{
			const glm::mat4& transform = *lightTransformMatrixOverrides_[index];
			light.position = glm::vec3(transform * glm::vec4(light.position, 1.0f));
			glm::vec3 transformedDirection = glm::mat3(transform) * light.direction;
			if (glm::length(transformedDirection) > 0.0f)
			{
				light.direction = glm::normalize(transformedDirection);
			}
		}

		if (glm::length(light.direction) > 0.0f)
		{
			light.direction = glm::normalize(light.direction);
		}
		else
		{
			light.direction = glm::vec3(0.0f, -1.0f, 0.0f);
		}
		return light;
	}

	LightingBufferObject VkApp::buildLightingBufferObject() const
	{
		LightingBufferObject lighting{};
		const size_t lightCount = std::min(lights_.size(), lightGraphics::MaxForwardLights);
		lighting.ambientAndCount = glm::vec4(ambientLight_, static_cast<float>(lightCount));

		for (size_t i = 0; i < lightCount; ++i)
		{
			const LightSource light = lightForUpload(i);
			GpuLight& gpuLight = lighting.lights[i];
			const float intensity = light.enabled ? light.intensity : 0.0f;
			const float range = std::max(light.range, 0.0f);
			const float inner = glm::clamp(light.innerConeAngleRadians, 0.0f, glm::pi<float>());
			const float outer = glm::clamp(light.outerConeAngleRadians, inner, glm::pi<float>());

			gpuLight.positionRange = glm::vec4(light.position, range);
			gpuLight.directionType = glm::vec4(light.direction, static_cast<float>(light.type));
			gpuLight.colorIntensity = glm::vec4(light.color, intensity);
			gpuLight.spotAngles = glm::vec4(std::cos(inner), std::cos(outer), light.castsShadow ? 1.0f : 0.0f, 0.0f);
		}

		return lighting;
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
		instance.shapeType = static_cast<float>(obj._type);
		return instance;
	}

	void VkApp::clearObjectModelMatrixOverrideInternal(size_t index)
	{
		if (index < objectModelMatrixOverrides_.size())
		{
			objectModelMatrixOverrides_[index].reset();
		}
	}

	void VkApp::ensureInstanceBufferSizeForFrame(uint32_t frameIndex, VkDeviceSize requiredSize)
	{
		if (instanceBufferSizes_[frameIndex] < requiredSize)
		{
			// Clean up old buffer if it exists
			if (instanceBufs_[frameIndex].buffer != VK_NULL_HANDLE)
			{
				if (instanceBufferMappedPerFrame_[frameIndex] != nullptr &&
				    instanceBufs_[frameIndex].memory != VK_NULL_HANDLE)
				{
					vkUnmapMemory(device_, instanceBufs_[frameIndex].memory);
					instanceBufferMappedPerFrame_[frameIndex] = nullptr;
				}
				vkDestroyBuffer(device_, instanceBufs_[frameIndex].buffer, nullptr);
				vkFreeMemory(device_, instanceBufs_[frameIndex].memory, nullptr);
			}

			// Create new larger buffer for this frame
			createBuffer(requiredSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
						instanceBufs_[frameIndex]);

			// Map the buffer persistently
			VK_CHECK(vkMapMemory(device_, instanceBufs_[frameIndex].memory, 0, requiredSize, 0, &instanceBufferMappedPerFrame_[frameIndex]));
			instanceBufferSizes_[frameIndex] = requiredSize;
		}
	}

	void VkApp::updateInstanceDataOptimized()
	{
		if (_objects_.empty())
		{
			instanceCount = 0;
			instanceCount_ = 0;
			dirtyObjects_.clear();
			instanceDataCache_.clear();
			instanceDataDirty_ = false;
			return;
		}

		if (!instanceDataDirty_)
		{
			return;
		}

		// Update instance count
		instanceCount_ = static_cast<uint32_t>(_objects_.size());

		// Ensure we have enough space in the dirty tracking array
		if (dirtyObjects_.size() < _objects_.size())
		{
			dirtyObjects_.resize(_objects_.size(), false);
		}

		// Ensure we have enough space in the instance data cache
		if (instanceDataCache_.size() < _objects_.size())
		{
			instanceDataCache_.resize(_objects_.size());
		}

		// Only update dirty objects
		bool anyDirty = false;
		for (size_t i = 0; i < _objects_.size(); ++i)
		{
			if (dirtyObjects_[i])
			{
				instanceDataCache_[i] = makeInstanceForObject(i);

				dirtyObjects_[i] = false;
				anyDirty = true;
			}
		}

		if (!anyDirty)
		{
			instanceDataDirty_ = false;
			return;
		}

		// Ensure buffer for current frame is large enough
		VkDeviceSize instBytes = sizeof(Instance) * _objects_.size();
		ensureInstanceBufferSizeForFrame(static_cast<uint32_t>(currentFrame_), instBytes);

		// Copy updated data to the persistently mapped buffer for this frame
		void* mapped = instanceBufferMappedPerFrame_[currentFrame_];
		if (mapped)
		{
			std::memcpy(mapped, instanceDataCache_.data(), (size_t)instBytes);
		}

		instanceDataDirty_ = false;
	}

	// Batch update methods for high performance
	void VkApp::updateObjectPositions(const std::vector<std::pair<size_t, glm::vec3>>& updates)
	{
		for (const auto& update : updates)
		{
			if (update.first < _objects_.size())
			{
				_objects_[update.first].setPosition(update.second);
				clearObjectModelMatrixOverrideInternal(update.first);
				sceneGraph_->onObjectChanged(update.first);
				markObjectDirty(update.first);
			}
		}
	}

	void VkApp::updateObjectProperties(const std::vector<std::tuple<size_t, glm::vec3, glm::vec3, glm::quat>>& updates)
	{
		for (const auto& update : updates)
		{
			size_t index = std::get<0>(update);
			if (index < _objects_.size())
			{
				_objects_[index].setPosition(std::get<1>(update));
				_objects_[index].setSize(std::get<2>(update));
				_objects_[index].setRotation(std::get<3>(update));
				clearObjectModelMatrixOverrideInternal(index);
				sceneGraph_->onObjectChanged(index);
				markObjectDirty(index);
			}
		}
	}

	void VkApp::flushPendingUpdates()
	{
		if (sceneFinalized_ && device_ != VK_NULL_HANDLE && currentFrame_ < inFlight_.size())
		{
			VK_CHECK(vkWaitForFences(device_, 1, &inFlight_[currentFrame_], VK_TRUE, UINT64_MAX));
		}
		sceneGraph_->updateWorldTransforms();
		sceneGraph_->syncToRenderer();
		updateInstanceDataOptimized();
		updateRiggedInstances();
	}

	void VkApp::updateRiggedInstances()
	{
		if (riggedInstances_.empty())
		{
			return;
		}

		const uint32_t frameIndex = static_cast<uint32_t>(currentFrame_);
		if (sceneFinalized_ && device_ != VK_NULL_HANDLE && frameIndex < inFlight_.size())
		{
			VK_CHECK(vkWaitForFences(device_, 1, &inFlight_[frameIndex], VK_TRUE, UINT64_MAX));
		}

		for (auto& instance : riggedInstances_)
		{
			auto riggedObject = instance.object;
			if (!riggedObject)
			{
				continue;
			}

			auto model = riggedObject->getModel();
			if (!model)
			{
				continue;
			}

			const auto& boneTransforms = riggedObject->getBoneTransforms();
			glm::mat4 globalInverse = model->globalInverseTransform;
			const bool useSkinningBindCorrection =
			    model->usesSkinningBindCorrection &&
			    boneTransforms.size() == model->bones.size();

			for (auto& meshData : instance.meshes)
			{
				const RiggedMesh* mesh = meshData.mesh;
				if (!mesh || mesh->vertices.empty())
				{
					continue;
				}

				if (meshData.skinnedVertices.size() != mesh->vertices.size())
				{
					meshData.skinnedVertices.resize(mesh->vertices.size());
				}

				bool hasSkinning = !mesh->bones.empty() && !boneTransforms.empty();
				std::vector<glm::mat4> finalBoneMatrices;
				if (hasSkinning)
				{
					finalBoneMatrices.resize(mesh->bones.size(), glm::mat4(1.0f));
					for (size_t i = 0; i < mesh->bones.size(); ++i)
					{
						const Bone& bone = mesh->bones[i];
						glm::mat4 finalMat = glm::mat4(1.0f);
						auto it = model->boneMapping.find(bone.name);
						if (it != model->boneMapping.end() &&
						    it->second < static_cast<int>(boneTransforms.size()))
						{
							glm::mat4 globalBone = boneTransforms[it->second];
							if (useSkinningBindCorrection)
							{
								// Assimp's offset matrix already maps mesh bind vertices into
								// the skin bind basis. Rebuilding another corrected bind
								// hierarchy here cancels the imported animation delta for
								// Worker.fbx and leaves the arms in the horizontal bind pose.
								finalMat = globalInverse * globalBone * bone.offsetMatrix;
							}
							else
							{
								const Bone& globalBoneBind = model->bones[it->second];
								// Default to the node-bind path for ordinary rigs. The corrected
								// skin-bind path is only enabled when bind mismatch is detected.
								finalMat = globalInverse *
								           globalBone *
								           glm::inverse(globalBoneBind.globalBindTransform) *
								           mesh->globalBindTransform;
							}
						}
						finalBoneMatrices[i] = finalMat;
					}
				}

				glm::vec2 uvMin(std::numeric_limits<float>::max());
				glm::vec2 uvMax(std::numeric_limits<float>::lowest());
				bool loggedInvalidSkinnedVertex = false;

				for (size_t v = 0; v < mesh->vertices.size(); ++v)
				{
					const RiggedVertex& src = mesh->vertices[v];
					glm::vec4 blendedPos(0.0f);
					glm::vec4 blendedNrm(0.0f);
					bool usedWeights = false;

					if (hasSkinning)
					{
						for (int k = 0; k < 4; ++k)
						{
							int boneIndex = src.boneIndices[k];
							float weight = src.boneWeights[k];
							if (boneIndex >= 0 &&
							    boneIndex < static_cast<int>(finalBoneMatrices.size()) &&
							    weight > 0.0f)
							{
								const glm::mat4& mat = finalBoneMatrices[boneIndex];
								blendedPos += (mat * glm::vec4(src.position, 1.0f)) * weight;
								blendedNrm += (mat * glm::vec4(src.normal, 0.0f)) * weight;
								usedWeights = true;
							}
						}
					}

					if (!usedWeights)
					{
						blendedPos = glm::vec4(src.position, 1.0f);
						blendedNrm = glm::vec4(src.normal, 0.0f);
					}

					glm::vec3 finalPos = glm::vec3(blendedPos);
					glm::vec3 finalNrm = glm::vec3(blendedNrm);
					if (!isFiniteVec3(finalPos) || !isFiniteVec3(finalNrm))
					{
						if (!loggedInvalidSkinnedVertex)
						{
							const std::string meshLabel =
							    mesh->materialName.empty() ? std::string("<unnamed>") : mesh->materialName;
							logMessage(LogLevel::Warning,
							           "[RiggedMesh] '" + meshLabel +
							           "' produced non-finite skinned vertices; using source vertices for safety");
							loggedInvalidSkinnedVertex = true;
						}

						finalPos = src.position;
						finalNrm = src.normal;
					}
					if (glm::length(finalNrm) > 0.0f)
					{
						finalNrm = glm::normalize(finalNrm);
					}

					meshData.skinnedVertices[v].pos = finalPos;
					meshData.skinnedVertices[v].nrm = finalNrm;

					// Assimp already flips V for us (aiProcess_FlipUVs), so keep coordinates as-is.
					glm::vec2 uv(src.texCoords.x, src.texCoords.y);
					meshData.skinnedVertices[v].uv = uv;
					uvMin = glm::min(uvMin, uv);
					uvMax = glm::max(uvMax, uv);
				}

				if (debugOutput)
				{
					std::ostringstream message;
					message << "[RiggedMesh] '" << mesh->materialName << "' UV range: ["
					        << uvMin.x << ", " << uvMin.y << "] -> ["
					        << uvMax.x << ", " << uvMax.y << "]";
					logMessage(LogLevel::Debug, message.str());
				}

				detail::Buffer& frameVertexBuffer = meshData.vertexBuffers[frameIndex];
				void* frameVertexMapped = meshData.vertexBufferMapped[frameIndex];
				if (!meshData.skinnedVertices.empty() && frameVertexBuffer.memory != VK_NULL_HANDLE)
				{
					VkDeviceSize vbSize = sizeof(Vertex) * meshData.skinnedVertices.size();
					if (frameVertexMapped != nullptr)
					{
						std::memcpy(frameVertexMapped, meshData.skinnedVertices.data(), static_cast<size_t>(vbSize));
					}
					else
					{
						void* mapped = nullptr;
						VK_CHECK(vkMapMemory(device_, frameVertexBuffer.memory, 0, vbSize, 0, &mapped));
						std::memcpy(mapped, meshData.skinnedVertices.data(), static_cast<size_t>(vbSize));
						vkUnmapMemory(device_, frameVertexBuffer.memory);
					}
				}
			}

			Instance riggedInstance{};
			if (instance.transformMatrixOverride)
			{
				riggedInstance.model = *instance.transformMatrixOverride * instance.uprightCorrection;
			}
			else
			{
				glm::mat4 translation = glm::translate(glm::mat4(1.0f), riggedObject->getPosition());
				glm::mat4 rotation = glm::mat4_cast(riggedObject->getRotation());
				glm::mat4 scale = glm::scale(glm::mat4(1.0f), riggedObject->getSize());
				riggedInstance.model = translation * rotation * instance.uprightCorrection * scale;
			}
			riggedInstance.color = glm::vec3(riggedObject->getColour());
			riggedInstance.shapeType = static_cast<float>(lightGraphics::ShapeType::HUMAN);

			detail::Buffer& frameInstanceBuffer = instance.instanceBuffers[frameIndex];
			void* frameInstanceMapped = instance.instanceBufferMapped[frameIndex];
			if (frameInstanceBuffer.memory != VK_NULL_HANDLE)
			{
				if (frameInstanceMapped != nullptr)
				{
					std::memcpy(frameInstanceMapped, &riggedInstance, sizeof(Instance));
				}
				else
				{
					void* mapped = nullptr;
					VK_CHECK(vkMapMemory(device_, frameInstanceBuffer.memory, 0, sizeof(Instance), 0, &mapped));
					std::memcpy(mapped, &riggedInstance, sizeof(Instance));
					vkUnmapMemory(device_, frameInstanceBuffer.memory);
				}
			}
		}
	}

	void VkApp::destroyRiggedInstance(RiggedInstanceRenderData& instance)
	{
		for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
		{
			if (instance.instanceBufferMapped[frameIndex] != nullptr &&
			    instance.instanceBuffers[frameIndex].memory != VK_NULL_HANDLE)
			{
				vkUnmapMemory(device_, instance.instanceBuffers[frameIndex].memory);
				instance.instanceBufferMapped[frameIndex] = nullptr;
			}
			destroyBuffer(device_, instance.instanceBuffers[frameIndex]);
		}
		for (auto& mesh : instance.meshes)
		{
			for (uint32_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
			{
				if (mesh.vertexBufferMapped[frameIndex] != nullptr &&
				    mesh.vertexBuffers[frameIndex].memory != VK_NULL_HANDLE)
				{
					vkUnmapMemory(device_, mesh.vertexBuffers[frameIndex].memory);
					mesh.vertexBufferMapped[frameIndex] = nullptr;
				}
				destroyBuffer(device_, mesh.vertexBuffers[frameIndex]);
			}
			destroyBuffer(device_, mesh.indexBuffer);
			mesh.skinnedVertices.clear();
			mesh.texture.reset();
		}
		instance.meshes.clear();
		instance.object.reset();
		instance.transformMatrixOverride.reset();
	}

	void VkApp::destroyRiggedInstances()
	{
		for (auto& instance : riggedInstances_)
		{
			destroyRiggedInstance(instance);
		}
		riggedInstances_.clear();
	}

	// ==================== ORIGINAL METHODS (NOW OPTIMIZED) ====================

	void VkApp::updateInstanceData()
	{
		if (_objects_.empty())
		{
			instanceCount = 0;
			instanceCount_ = 0;
			dirtyObjects_.clear();
			instanceDataCache_.clear();
			instanceDataDirty_ = false;
			return;
		}

		// Update instance count
		instanceCount = static_cast<uint32_t>(_objects_.size());
		instanceCount_ = static_cast<uint32_t>(_objects_.size());

		// Generate instance data from objects
		std::vector<Instance> instances(instanceCount_);
		for (size_t i = 0; i < _objects_.size() && i < instanceCount_; ++i)
		{
			instances[i] = makeInstanceForObject(i);
		}

		// Update the instance buffer
		VkDeviceSize instBytes = sizeof(Instance) * instances.size();

		// If the buffer is too small, recreate it
		if (instanceBuf.size < instBytes)
		{
			if (instanceBuf.buffer != VK_NULL_HANDLE)
			{
				vkDestroyBuffer(device_, instanceBuf.buffer, nullptr);
				vkFreeMemory(device_, instanceBuf.memory, nullptr);
			}
			createBuffer(instBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
						instanceBuf);
		}

		// Copy data to buffer for current frame
		ensureInstanceBufferSizeForFrame(static_cast<uint32_t>(currentFrame_), instBytes);
		void* ptr = instanceBufferMappedPerFrame_[currentFrame_];
		if (ptr)
		{
			std::memcpy(ptr, instances.data(), (size_t)instBytes);
		}
	}

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

		if (!hasRegularObjects && riggedInstances_.empty())
		{
			// Nothing to draw
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
				int shapeType = static_cast<int>(_objects_[i]._type);
				if (shapeType >= 0 && shapeType < 8)
				{
					// Objects with this name prefix are treated as debug overlays and
					// rendered in a second pass with depth testing disabled.
					std::string name = _objects_[i].getName();
					bool isOverlay = (name.rfind("CollisionCapsule", 0) == 0);
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

				// Issue indexed instanced draw for this contiguous range
				vkCmdDrawIndexed(cmd,
					shapeGeo.indexCount,
					countForShape,
					shapeGeo.indexOffset,
					0,
					runningFirst);

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

		// Draw overlay debug shapes (e.g. collision capsules) "through" the mesh
		if (indexed && useInstancing && hasOverlayObjects && flexibleShapeOverlayPipeline_ != VK_NULL_HANDLE)
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flexibleShapeOverlayPipeline_);

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

				vkCmdDrawIndexed(cmd,
					shapeGeo.indexCount,
					countForShape,
					shapeGeo.indexOffset,
					0,
					runningFirst);

				runningFirst += countForShape;
			}
		}

		vkCmdEndRenderPass(cmd);

		if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
		{
			throw std::runtime_error("recordCommandBuffer: vkEndCommandBuffer failed");
		}
	}

} // namespace lightGraphics
