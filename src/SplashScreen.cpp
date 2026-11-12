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

#include "SplashScreen.h"

#include "ImageLoad.h"

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

namespace lightGraphics
{
	namespace
	{
		// Bilinear resample of tightly packed RGBA8, compositing alpha over `bg` so the
		// result is opaque. Source and destination are both top-to-bottom.
		std::vector<std::uint8_t> resampleOpaque(const std::uint8_t* src, int sw, int sh, int dw, int dh,
		                                         const std::uint8_t bg[3])
		{
			std::vector<std::uint8_t> out(static_cast<std::size_t>(dw) * dh * 4);
			for (int y = 0; y < dh; ++y)
			{
				const float fy = std::max(0.0f, (y + 0.5f) * sh / dh - 0.5f);
				const int y0 = std::min(sh - 1, static_cast<int>(fy));
				const int y1 = std::min(sh - 1, y0 + 1);
				const float ty = fy - y0;
				for (int x = 0; x < dw; ++x)
				{
					const float fx = std::max(0.0f, (x + 0.5f) * sw / dw - 0.5f);
					const int x0 = std::min(sw - 1, static_cast<int>(fx));
					const int x1 = std::min(sw - 1, x0 + 1);
					const float tx = fx - x0;
					const std::uint8_t* p00 = src + (static_cast<std::size_t>(y0) * sw + x0) * 4;
					const std::uint8_t* p10 = src + (static_cast<std::size_t>(y0) * sw + x1) * 4;
					const std::uint8_t* p01 = src + (static_cast<std::size_t>(y1) * sw + x0) * 4;
					const std::uint8_t* p11 = src + (static_cast<std::size_t>(y1) * sw + x1) * 4;
					float c[4];
					for (int k = 0; k < 4; ++k)
					{
						const float top = p00[k] + (p10[k] - p00[k]) * tx;
						const float bot = p01[k] + (p11[k] - p01[k]) * tx;
						c[k] = top + (bot - top) * ty;
					}
					const float a = c[3] / 255.0f;
					std::uint8_t* d = out.data() + (static_cast<std::size_t>(y) * dw + x) * 4;
					for (int k = 0; k < 3; ++k)
					{
						d[k] = static_cast<std::uint8_t>(std::lround(c[k] * a + bg[k] * (1.0f - a)));
					}
					d[3] = 255;
				}
			}
			return out;
		}

		bool hasExtension(const std::vector<VkExtensionProperties>& list, const char* name)
		{
			for (const auto& e : list)
			{
				if (std::strcmp(e.extensionName, name) == 0)
				{
					return true;
				}
			}
			return false;
		}
	}

	struct SplashScreen::Impl
	{
		Options options;
		std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

		GLFWwindow* window = nullptr;
		VkInstance instance = VK_NULL_HANDLE;
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		VkPhysicalDevice physical = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		std::uint32_t family = 0;
		VkQueue queue = VK_NULL_HANDLE;
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		std::vector<VkImage> images;
		VkExtent2D extent{};
		bool swapBgr = false;
		VkCommandPool pool = VK_NULL_HANDLE;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		VkSemaphore imageAvailable = VK_NULL_HANDLE;
		VkSemaphore renderDone = VK_NULL_HANDLE;
		VkFence fence = VK_NULL_HANDLE;
		VkBuffer staging = VK_NULL_HANDLE;
		VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
		VkDeviceSize stagingSize = 0;
		bool open = false;

		~Impl() { destroy(); }

		void destroy()
		{
			if (device != VK_NULL_HANDLE)
			{
				vkDeviceWaitIdle(device);
				if (staging != VK_NULL_HANDLE) vkDestroyBuffer(device, staging, nullptr);
				if (stagingMemory != VK_NULL_HANDLE) vkFreeMemory(device, stagingMemory, nullptr);
				if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
				if (renderDone != VK_NULL_HANDLE) vkDestroySemaphore(device, renderDone, nullptr);
				if (imageAvailable != VK_NULL_HANDLE) vkDestroySemaphore(device, imageAvailable, nullptr);
				if (pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, pool, nullptr);
				if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, nullptr);
				vkDestroyDevice(device, nullptr);
			}
			if (instance != VK_NULL_HANDLE)
			{
				if (surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, nullptr);
				vkDestroyInstance(instance, nullptr);
			}
			if (window) glfwDestroyWindow(window);
			staging = VK_NULL_HANDLE; stagingMemory = VK_NULL_HANDLE; fence = VK_NULL_HANDLE;
			renderDone = VK_NULL_HANDLE; imageAvailable = VK_NULL_HANDLE; pool = VK_NULL_HANDLE;
			swapchain = VK_NULL_HANDLE; device = VK_NULL_HANDLE; surface = VK_NULL_HANDLE;
			instance = VK_NULL_HANDLE; window = nullptr;
			open = false;
		}

		bool createWindow(int imgW, int imgH, int& winW, int& winH)
		{
			if (glfwInit() != GLFW_TRUE || glfwVulkanSupported() != GLFW_TRUE)
			{
				return false;
			}
			winW = imgW;
			winH = imgH;
			if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
			{
				int mx = 0, my = 0, mw = 0, mh = 0;
				glfwGetMonitorWorkarea(monitor, &mx, &my, &mw, &mh);
				if (mw > 0 && mh > 0)
				{
					const float fraction = std::clamp(options.maxMonitorFraction, 0.05f, 1.0f);
					const float scale = std::min({ 1.0f, fraction * mw / imgW, fraction * mh / imgH });
					winW = std::max(1, static_cast<int>(std::lround(imgW * scale)));
					winH = std::max(1, static_cast<int>(std::lround(imgH * scale)));
				}
			}

			glfwDefaultWindowHints();
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
			glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
			glfwWindowHint(GLFW_FLOATING, options.alwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
			glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
			window = glfwCreateWindow(winW, winH, options.title.c_str(), nullptr, nullptr);
			// Hints are global: don't leak ours into the application's own window creation.
			glfwDefaultWindowHints();
			if (!window)
			{
				return false;
			}
			if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
			{
				int mx = 0, my = 0, mw = 0, mh = 0;
				glfwGetMonitorWorkarea(monitor, &mx, &my, &mw, &mh);
				glfwSetWindowPos(window, mx + (mw - winW) / 2, my + (mh - winH) / 2);
			}
			return true;
		}

		bool createVulkan()
		{
			std::uint32_t count = 0;
			const char** glfwExt = glfwGetRequiredInstanceExtensions(&count);
			if (!glfwExt || count == 0)
			{
				return false;
			}
			std::vector<const char*> extensions(glfwExt, glfwExt + count);

			VkApplicationInfo app{};
			app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
			app.pApplicationName = "SplashScreen";
			app.apiVersion = VK_API_VERSION_1_0;
			VkInstanceCreateInfo ici{};
			ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			ici.pApplicationInfo = &app;
			ici.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
			ici.ppEnabledExtensionNames = extensions.data();
			if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS)
			{
				instance = VK_NULL_HANDLE;
				return false;
			}
			if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
			{
				surface = VK_NULL_HANDLE;
				return false;
			}

			std::uint32_t deviceCount = 0;
			vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
			std::vector<VkPhysicalDevice> devices(deviceCount);
			vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
			for (VkPhysicalDevice candidate : devices)
			{
				std::uint32_t extCount = 0;
				vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extCount, nullptr);
				std::vector<VkExtensionProperties> exts(extCount);
				vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extCount, exts.data());
				if (!hasExtension(exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
				{
					continue;
				}
				std::uint32_t qCount = 0;
				vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qCount, nullptr);
				std::vector<VkQueueFamilyProperties> families(qCount);
				vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qCount, families.data());
				for (std::uint32_t i = 0; i < qCount; ++i)
				{
					VkBool32 present = VK_FALSE;
					vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present);
					if (present && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
					{
						physical = candidate;
						family = i;
						break;
					}
				}
				if (physical != VK_NULL_HANDLE)
				{
					break;
				}
			}
			if (physical == VK_NULL_HANDLE)
			{
				return false;
			}

			const float priority = 1.0f;
			VkDeviceQueueCreateInfo qci{};
			qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			qci.queueFamilyIndex = family;
			qci.queueCount = 1;
			qci.pQueuePriorities = &priority;
			const char* devExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
			VkDeviceCreateInfo dci{};
			dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
			dci.queueCreateInfoCount = 1;
			dci.pQueueCreateInfos = &qci;
			dci.enabledExtensionCount = 1;
			dci.ppEnabledExtensionNames = &devExt;
			if (vkCreateDevice(physical, &dci, nullptr, &device) != VK_SUCCESS)
			{
				device = VK_NULL_HANDLE;
				return false;
			}
			vkGetDeviceQueue(device, family, 0, &queue);
			return true;
		}

		bool createSwapchain()
		{
			VkSurfaceCapabilitiesKHR caps{};
			if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps) != VK_SUCCESS
			    || !(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
			{
				return false;
			}
			std::uint32_t fmtCount = 0;
			vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fmtCount, nullptr);
			std::vector<VkSurfaceFormatKHR> formats(fmtCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fmtCount, formats.data());
			bool found = false;
			VkSurfaceFormatKHR chosen{};
			for (const auto& f : formats)
			{
				// Only the four 8-bit RGBA/BGRA layouts: the CPU copy writes bytes as-is, so
				// UNORM and SRGB both show the image's own values unchanged.
				if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_SRGB)
				{
					chosen = f; swapBgr = true; found = true; break;
				}
				if (!found && (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_SRGB))
				{
					chosen = f; swapBgr = false; found = true;
				}
			}
			if (!found)
			{
				return false;
			}

			extent = caps.currentExtent;
			if (extent.width == 0xFFFFFFFFu)
			{
				int fw = 0, fh = 0;
				glfwGetFramebufferSize(window, &fw, &fh);
				extent.width = static_cast<std::uint32_t>(std::max(1, fw));
				extent.height = static_cast<std::uint32_t>(std::max(1, fh));
			}
			if (extent.width == 0 || extent.height == 0)
			{
				return false;
			}

			VkSwapchainCreateInfoKHR sci{};
			sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			sci.surface = surface;
			sci.minImageCount = std::max(caps.minImageCount, 2u);
			if (caps.maxImageCount > 0)
			{
				sci.minImageCount = std::min(sci.minImageCount, caps.maxImageCount);
			}
			sci.imageFormat = chosen.format;
			sci.imageColorSpace = chosen.colorSpace;
			sci.imageExtent = extent;
			sci.imageArrayLayers = 1;
			sci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			sci.preTransform = caps.currentTransform;
			sci.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
			                         ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
			                         : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
			sci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // always available
			sci.clipped = VK_TRUE;
			if (vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain) != VK_SUCCESS)
			{
				swapchain = VK_NULL_HANDLE;
				return false;
			}
			std::uint32_t imageCount = 0;
			vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
			images.resize(imageCount);
			vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data());
			return true;
		}

		bool createSyncAndCommands()
		{
			VkCommandPoolCreateInfo pci{};
			pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			pci.queueFamilyIndex = family;
			if (vkCreateCommandPool(device, &pci, nullptr, &pool) != VK_SUCCESS)
			{
				pool = VK_NULL_HANDLE;
				return false;
			}
			VkCommandBufferAllocateInfo cai{};
			cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			cai.commandPool = pool;
			cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			cai.commandBufferCount = 1;
			if (vkAllocateCommandBuffers(device, &cai, &cmd) != VK_SUCCESS)
			{
				return false;
			}
			VkSemaphoreCreateInfo semi{};
			semi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			VkFenceCreateInfo fi{};
			fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			return vkCreateSemaphore(device, &semi, nullptr, &imageAvailable) == VK_SUCCESS
			    && vkCreateSemaphore(device, &semi, nullptr, &renderDone) == VK_SUCCESS
			    && vkCreateFence(device, &fi, nullptr, &fence) == VK_SUCCESS;
		}

		bool uploadPixels(const std::vector<std::uint8_t>& rgba)
		{
			stagingSize = rgba.size();
			VkBufferCreateInfo bci{};
			bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bci.size = stagingSize;
			bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			if (vkCreateBuffer(device, &bci, nullptr, &staging) != VK_SUCCESS)
			{
				staging = VK_NULL_HANDLE;
				return false;
			}
			VkMemoryRequirements req{};
			vkGetBufferMemoryRequirements(device, staging, &req);
			VkPhysicalDeviceMemoryProperties mem{};
			vkGetPhysicalDeviceMemoryProperties(physical, &mem);
			const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			std::uint32_t type = mem.memoryTypeCount;
			for (std::uint32_t i = 0; i < mem.memoryTypeCount; ++i)
			{
				if ((req.memoryTypeBits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & want) == want)
				{
					type = i;
					break;
				}
			}
			if (type == mem.memoryTypeCount)
			{
				return false;
			}
			VkMemoryAllocateInfo mai{};
			mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			mai.allocationSize = req.size;
			mai.memoryTypeIndex = type;
			if (vkAllocateMemory(device, &mai, nullptr, &stagingMemory) != VK_SUCCESS)
			{
				stagingMemory = VK_NULL_HANDLE;
				return false;
			}
			vkBindBufferMemory(device, staging, stagingMemory, 0);
			void* mapped = nullptr;
			if (vkMapMemory(device, stagingMemory, 0, stagingSize, 0, &mapped) != VK_SUCCESS)
			{
				return false;
			}
			std::memcpy(mapped, rgba.data(), rgba.size());
			if (swapBgr)
			{
				auto* p = static_cast<std::uint8_t*>(mapped);
				for (std::size_t i = 0; i + 3 < rgba.size(); i += 4)
				{
					std::swap(p[i], p[i + 2]);
				}
			}
			vkUnmapMemory(device, stagingMemory);
			return true;
		}

		// Returns false if presenting failed in a way that means the splash can't continue.
		bool present()
		{
			if (!open)
			{
				return false;
			}
			vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
			std::uint32_t index = 0;
			VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailable, VK_NULL_HANDLE, &index);
			if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
			{
				return false;
			}
			vkResetFences(device, 1, &fence);
			vkResetCommandBuffer(cmd, 0);
			VkCommandBufferBeginInfo bi{};
			bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(cmd, &bi);

			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = images[index];
			barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			                     0, nullptr, 0, nullptr, 1, &barrier);

			VkBufferImageCopy region{};
			region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			region.imageExtent = { extent.width, extent.height, 1 };
			vkCmdCopyBufferToImage(cmd, staging, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = 0;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
			                     0, nullptr, 0, nullptr, 1, &barrier);
			vkEndCommandBuffer(cmd);

			const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			VkSubmitInfo si{};
			si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			si.waitSemaphoreCount = 1;
			si.pWaitSemaphores = &imageAvailable;
			si.pWaitDstStageMask = &waitStage;
			si.commandBufferCount = 1;
			si.pCommandBuffers = &cmd;
			si.signalSemaphoreCount = 1;
			si.pSignalSemaphores = &renderDone;
			if (vkQueueSubmit(queue, 1, &si, fence) != VK_SUCCESS)
			{
				return false;
			}
			VkPresentInfoKHR pi{};
			pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
			pi.waitSemaphoreCount = 1;
			pi.pWaitSemaphores = &renderDone;
			pi.swapchainCount = 1;
			pi.pSwapchains = &swapchain;
			pi.pImageIndices = &index;
			r = vkQueuePresentKHR(queue, &pi);
			return r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR;
		}
	};

	SplashScreen::SplashScreen(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

	SplashScreen::~SplashScreen()
	{
		if (impl_)
		{
			impl_->destroy();
		}
	}

	std::unique_ptr<SplashScreen> SplashScreen::create(const std::uint8_t* rgba, int width, int height,
	                                                   const Options& options)
	{
		if (!rgba || width <= 0 || height <= 0)
		{
			return nullptr;
		}
		auto impl = std::make_unique<Impl>();
		impl->options = options;
		int winW = 0, winH = 0;
		if (!impl->createWindow(width, height, winW, winH) || !impl->createVulkan() || !impl->createSwapchain()
		    || !impl->createSyncAndCommands())
		{
			return nullptr;
		}
		// Fit the image to the swapchain (which on HiDPI is larger than the window).
		const auto pixels = resampleOpaque(rgba, width, height, static_cast<int>(impl->extent.width),
		                                   static_cast<int>(impl->extent.height), options.backgroundRgb);
		if (!impl->uploadPixels(pixels))
		{
			return nullptr;
		}
		impl->open = true;
		glfwShowWindow(impl->window);
		glfwPollEvents();
		if (!impl->present())
		{
			return nullptr;
		}
		return std::unique_ptr<SplashScreen>(new SplashScreen(std::move(impl)));
	}

	std::unique_ptr<SplashScreen> SplashScreen::showFromRgba(const std::uint8_t* rgba, int width, int height,
	                                                         const Options& options)
	{
		return create(rgba, width, height, options);
	}

	std::unique_ptr<SplashScreen> SplashScreen::showFromRgba(const std::uint8_t* rgba, int width, int height)
	{
		return create(rgba, width, height, Options{});
	}

	std::unique_ptr<SplashScreen> SplashScreen::showFromFile(const std::string& path, const Options& options)
	{
		ImageRgba image;
		if (!loadImageRgba(path, image))
		{
			return nullptr;
		}
		return create(image.rgba.data(), image.width, image.height, options);
	}

	std::unique_ptr<SplashScreen> SplashScreen::showFromFile(const std::string& path)
	{
		return showFromFile(path, Options{});
	}

	void SplashScreen::pump()
	{
		if (!impl_ || !impl_->open)
		{
			return;
		}
		glfwPollEvents();
		if (!impl_->present())
		{
			impl_->destroy(); // can't continue; the application proceeds without a splash
		}
	}

	double SplashScreen::elapsedSeconds() const
	{
		return impl_ ? std::chrono::duration<double>(std::chrono::steady_clock::now() - impl_->start).count() : 0.0;
	}

	bool SplashScreen::isOpen() const
	{
		return impl_ && impl_->open;
	}

	void SplashScreen::close()
	{
		if (!impl_ || !impl_->open)
		{
			return;
		}
		while (impl_->open && elapsedSeconds() < impl_->options.minDisplaySeconds)
		{
			pump();
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}
		impl_->destroy();
	}
}
