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

// Frame capture: reading presented frames back from the swapchain, and a small
// dependency-free PNG writer to save them.

#include "VkAppCapture.h"

#include <array>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace lightGraphics
{

namespace detail
{
	namespace
	{
		std::mutex& captureMutex()
		{
			static std::mutex m;
			return m;
		}
		std::unordered_map<const VkApp*, CaptureState>& captureStates()
		{
			static std::unordered_map<const VkApp*, CaptureState> states;
			return states;
		}
	}

	CaptureState& captureState(const VkApp* app)
	{
		std::lock_guard<std::mutex> lock(captureMutex());
		return captureStates()[app];
	}

	void forgetCaptureState(const VkApp* app)
	{
		std::lock_guard<std::mutex> lock(captureMutex());
		captureStates().erase(app);
	}
}

void VkApp::requestFrameCapture(std::function<void(const CapturedFrame&)> onCaptured)
{
	assertOwnerThread("requestFrameCapture");
	if (onCaptured)
	{
		detail::captureState(this).pending.push_back(std::move(onCaptured));
	}
}

// The read-back is recorded into the frame's own command buffer, after the main render pass
// (whose 0 -> EXTERNAL dependency hands the image to the transfer stage, see
// createRenderPass()), and is covered by the frame's ordinary render-finished semaphore and
// fence. It used to be a separate submission chained on that semaphore and re-signalling
// it, which synchronisation validation reported as an unsynchronised present
// (SYNC-HAZARD-WRITE-AFTER-PRESENT).
void VkApp::prepareFrameCapture(uint32_t imageIndex)
{
	detail::CaptureState& state = detail::captureState(this);
	if (state.pending.empty())
	{
		return;
	}
	std::vector<std::function<void(const CapturedFrame&)>> callbacks;
	callbacks.swap(state.pending);

	auto fail = [&](const std::string& error)
	{
		CapturedFrame frame;
		frame.error = error;
		for (auto& cb : callbacks)
		{
			cb(frame);
		}
	};

	bool swapRedBlue = false;
	switch (swapChainImageFormat_)
	{
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_SRGB:
		swapRedBlue = true;
		break;
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
		break;
	default:
		fail("frame capture: unsupported swapchain format " + std::to_string(swapChainImageFormat_));
		return;
	}
	if (!state.swapchainReadable)
	{
		fail("frame capture: this surface does not allow reading the swapchain (no TRANSFER_SRC usage)");
		return;
	}
	if (imageIndex >= swapChainImages_.size())
	{
		fail("frame capture: swapchain image index out of range");
		return;
	}

	const VkDeviceSize bytes = static_cast<VkDeviceSize>(swapChainExtent_.width) * swapChainExtent_.height * 4;
	try
	{
		createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, state.readback);
	}
	catch (const std::exception& e)
	{
		fail(std::string("frame capture: ") + e.what());
		return;
	}
	state.active = std::move(callbacks);
	state.imageIndex = imageIndex;
	state.width = swapChainExtent_.width;
	state.height = swapChainExtent_.height;
	state.swapRedBlue = swapRedBlue;
	state.recorded = false;
}

void VkApp::recordFrameCapture(VkCommandBuffer cmd, uint32_t imageIndex)
{
	detail::CaptureState& state = detail::captureState(this);
	if (state.active.empty() || state.recorded || imageIndex != state.imageIndex ||
		state.readback.buffer == VK_NULL_HANDLE || imageIndex >= swapChainImages_.size())
	{
		return;
	}
	const VkImage image = swapChainImages_[imageIndex];

	// After the render pass the image is in PRESENT_SRC, its colour writes made available to
	// the transfer stage by the pass's 0 -> EXTERNAL dependency, which this chains on.
	VkImageMemoryBarrier toSrc{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	toSrc.srcAccessMask = 0;
	toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	toSrc.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toSrc.image = image;
	toSrc.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
		0, nullptr, 0, nullptr, 1, &toSrc);

	VkBufferImageCopy region{};
	region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	region.imageExtent = { state.width, state.height, 1 };
	vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, state.readback.buffer, 1, &region);

	// Back to PRESENT_SRC for presentation, which waits on the frame's render-finished
	// semaphore (its signal covers every command in this command buffer).
	VkImageMemoryBarrier toPresent = toSrc;
	toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	toPresent.dstAccessMask = 0;
	toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
		0, nullptr, 0, nullptr, 1, &toPresent);

	VkBufferMemoryBarrier toHost{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
	toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toHost.buffer = state.readback.buffer;
	toHost.size = VK_WHOLE_SIZE;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
		0, nullptr, 1, &toHost, 0, nullptr);
	state.recorded = true;
}

void VkApp::finishFrameCapture(VkFence frameFence)
{
	detail::CaptureState& state = detail::captureState(this);
	if (state.active.empty())
	{
		return;
	}
	std::vector<std::function<void(const CapturedFrame&)>> callbacks;
	callbacks.swap(state.active);

	CapturedFrame frame;
	try
	{
		if (!state.recorded)
		{
			throw std::runtime_error("the frame was not recorded with the read-back");
		}
		if (vkWaitForFences(device_, 1, &frameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
		{
			throw std::runtime_error("vkWaitForFences failed");
		}
		const VkDeviceSize bytes = static_cast<VkDeviceSize>(state.width) * state.height * 4;
		void* mapped = nullptr;
		if (vkMapMemory(device_, state.readback.memory, 0, bytes, 0, &mapped) != VK_SUCCESS)
		{
			throw std::runtime_error("vkMapMemory failed");
		}
		frame.width = state.width;
		frame.height = state.height;
		frame.rgba.resize(static_cast<std::size_t>(bytes));
		const auto* src = static_cast<const std::uint8_t*>(mapped);
		for (std::size_t i = 0; i < frame.rgba.size(); i += 4)
		{
			frame.rgba[i + 0] = src[i + (state.swapRedBlue ? 2 : 0)];
			frame.rgba[i + 1] = src[i + 1];
			frame.rgba[i + 2] = src[i + (state.swapRedBlue ? 0 : 2)];
			frame.rgba[i + 3] = 255;
		}
		vkUnmapMemory(device_, state.readback.memory);
		frame.ok = true;
	}
	catch (const std::exception& e)
	{
		frame = CapturedFrame{};
		frame.error = std::string("frame capture: ") + e.what();
	}
	destroyBuffer(device_, state.readback);
	state.recorded = false;
	for (auto& cb : callbacks)
	{
		cb(frame);
	}
}

// ---------------------------------------------------------------------------------
// PNG writer (8-bit RGBA, filter 0, deflate "stored" blocks -- valid and lossless,
// without a compressor dependency).
// ---------------------------------------------------------------------------------

namespace
{
	std::uint32_t crc32(const std::uint8_t* data, std::size_t n, std::uint32_t crc = 0)
	{
		static const std::array<std::uint32_t, 256> table = []
		{
			std::array<std::uint32_t, 256> t{};
			for (std::uint32_t i = 0; i < 256; ++i)
			{
				std::uint32_t c = i;
				for (int k = 0; k < 8; ++k)
				{
					c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
				}
				t[i] = c;
			}
			return t;
		}();
		crc = ~crc;
		for (std::size_t i = 0; i < n; ++i)
		{
			crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
		}
		return ~crc;
	}

	void put32(std::vector<std::uint8_t>& out, std::uint32_t v)
	{
		out.push_back(static_cast<std::uint8_t>(v >> 24));
		out.push_back(static_cast<std::uint8_t>(v >> 16));
		out.push_back(static_cast<std::uint8_t>(v >> 8));
		out.push_back(static_cast<std::uint8_t>(v));
	}

	void chunk(std::vector<std::uint8_t>& out, const char type[4], const std::vector<std::uint8_t>& data)
	{
		put32(out, static_cast<std::uint32_t>(data.size()));
		const std::size_t start = out.size();
		out.insert(out.end(), type, type + 4);
		out.insert(out.end(), data.begin(), data.end());
		put32(out, crc32(out.data() + start, out.size() - start));
	}
}

bool VkApp::writePng(const std::string& path, const CapturedFrame& frame, std::string* error)
{
	auto fail = [error](const std::string& why)
	{
		if (error)
		{
			*error = why;
		}
		return false;
	};
	if (!frame.ok || frame.width == 0 || frame.height == 0 ||
		frame.rgba.size() != static_cast<std::size_t>(frame.width) * frame.height * 4)
	{
		return fail(frame.error.empty() ? "writePng: frame has no pixels" : frame.error);
	}

	// Raw scanlines, each prefixed with filter type 0.
	const std::size_t rowBytes = static_cast<std::size_t>(frame.width) * 4;
	std::vector<std::uint8_t> raw;
	raw.reserve((rowBytes + 1) * frame.height);
	for (std::uint32_t y = 0; y < frame.height; ++y)
	{
		raw.push_back(0);
		const std::uint8_t* row = frame.rgba.data() + y * rowBytes;
		raw.insert(raw.end(), row, row + rowBytes);
	}

	// zlib stream of stored blocks, then Adler-32.
	std::vector<std::uint8_t> z;
	z.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
	z.push_back(0x78);
	z.push_back(0x01);
	std::size_t pos = 0;
	do
	{
		const std::size_t n = std::min<std::size_t>(65535, raw.size() - pos);
		const bool last = pos + n == raw.size();
		z.push_back(last ? 1 : 0);
		z.push_back(static_cast<std::uint8_t>(n & 0xFF));
		z.push_back(static_cast<std::uint8_t>(n >> 8));
		z.push_back(static_cast<std::uint8_t>(~n & 0xFF));
		z.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xFF));
		z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
		pos += n;
	} while (pos < raw.size());
	std::uint32_t a = 1;
	std::uint32_t b = 0;
	for (std::uint8_t v : raw)
	{
		a = (a + v) % 65521;
		b = (b + a) % 65521;
	}
	put32(z, (b << 16) | a);

	std::vector<std::uint8_t> png{ 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	std::vector<std::uint8_t> ihdr;
	put32(ihdr, frame.width);
	put32(ihdr, frame.height);
	ihdr.insert(ihdr.end(), { 8, 6, 0, 0, 0 });   // 8-bit, RGBA, deflate, no filter set, no interlace
	chunk(png, "IHDR", ihdr);
	chunk(png, "IDAT", z);
	chunk(png, "IEND", {});

	std::ofstream out(path, std::ios::binary);
	if (!out)
	{
		return fail("writePng: cannot open " + path);
	}
	out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
	if (!out)
	{
		return fail("writePng: write failed for " + path);
	}
	return true;
}

}
