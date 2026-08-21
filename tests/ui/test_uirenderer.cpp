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

// Headless coverage for the parts of the Vulkan backend that are pure arithmetic.
// No device, no window, no swapchain -- just UiRenderer::clampToFramebuffer, which
// docs/gui/02-rendering.md singles out as the most common source of validation errors.

#include "UiRenderer.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

namespace lvgui = lightGraphics::ui;

namespace {
	VkExtent2D extent(std::uint32_t w, std::uint32_t h) { return { w, h }; }

	void testFullyInsideIsUnchanged() {
		VkRect2D r = lvgui::UiRenderer::clampToFramebuffer({ 10, 20, 100, 50 }, extent(800, 600));
		assert(r.offset.x == 10 && r.offset.y == 20);
		assert(r.extent.width == 100 && r.extent.height == 50);
		std::cout << "✓ testFullyInsideIsUnchanged\n";
	}

	void testNegativeOriginClampsAndShrinksExtent() {
		// The bug this guards: clamping the origin to 0 without shrinking the extent
		// would leave width 100 and cover x=[0,100) instead of the correct x=[0,80).
		VkRect2D r = lvgui::UiRenderer::clampToFramebuffer({ -20, -30, 100, 90 }, extent(800, 600));
		assert(r.offset.x == 0);
		assert(r.offset.y == 0);
		assert(r.extent.width == 80);
		assert(r.extent.height == 60);
		std::cout << "✓ testNegativeOriginClampsAndShrinksExtent\n";
	}

	void testClampsToFramebufferFarEdge() {
		VkRect2D r = lvgui::UiRenderer::clampToFramebuffer({ 700, 500, 400, 400 }, extent(800, 600));
		assert(r.offset.x == 700 && r.offset.y == 500);
		assert(r.extent.width == 100);
		assert(r.extent.height == 100);
		assert(r.offset.x + static_cast<std::int32_t>(r.extent.width) == 800);
		assert(r.offset.y + static_cast<std::int32_t>(r.extent.height) == 600);
		std::cout << "✓ testClampsToFramebufferFarEdge\n";
	}

	void testFullyOutsideIsEmpty() {
		VkRect2D right = lvgui::UiRenderer::clampToFramebuffer({ 900, 10, 50, 50 }, extent(800, 600));
		assert(right.extent.width == 0 || right.extent.height == 0);

		VkRect2D above = lvgui::UiRenderer::clampToFramebuffer({ 10, -200, 50, 50 }, extent(800, 600));
		assert(above.extent.width == 0 || above.extent.height == 0);

		std::cout << "✓ testFullyOutsideIsEmpty\n";
	}

	void testZeroAreaIsEmpty() {
		VkRect2D r = lvgui::UiRenderer::clampToFramebuffer({ 10, 10, 0, 50 }, extent(800, 600));
		assert(r.extent.width == 0 || r.extent.height == 0);
		std::cout << "✓ testZeroAreaIsEmpty\n";
	}

	// The 1x1 framebuffer is the case the acceptance criteria call out: it is where a
	// clip rect wider than the framebuffer and a degenerate extent meet.
	void testOneByOneFramebufferNeverExceedsBounds() {
		const VkExtent2D fb = extent(1, 1);
		const lvgui::Rect clips[] = {
			{ 0, 0, 10000, 10000 },   // the DrawList base clip rect
			{ -50, -50, 100, 100 },
			{ 0.5f, 0.5f, 0.25f, 0.25f },
			{ 2, 2, 5, 5 },
			{ -10, -10, 5, 5 },
		};

		for (const lvgui::Rect& clip : clips) {
			VkRect2D r = lvgui::UiRenderer::clampToFramebuffer(clip, fb);
			assert(r.offset.x >= 0);
			assert(r.offset.y >= 0);
			assert(r.offset.x + static_cast<std::int32_t>(r.extent.width) <= 1);
			assert(r.offset.y + static_cast<std::int32_t>(r.extent.height) <= 1);
		}
		std::cout << "✓ testOneByOneFramebufferNeverExceedsBounds\n";
	}

	void testContentScaleScalesClipRect() {
		// Logical 100x50 at 2x content scale covers 200x100 physical pixels.
		VkRect2D r = lvgui::UiRenderer::clampToFramebuffer({ 10, 20, 100, 50 }, extent(1600, 1200), 2.0f, 2.0f);
		assert(r.offset.x == 20 && r.offset.y == 40);
		assert(r.extent.width == 200 && r.extent.height == 100);
		std::cout << "✓ testContentScaleScalesClipRect\n";
	}

	void testNonFiniteClipIsEmpty() {
		const float nan = std::nan("");
		const float inf = std::numeric_limits<float>::infinity();

		VkRect2D withNan = lvgui::UiRenderer::clampToFramebuffer({ nan, 0, 100, 100 }, extent(800, 600));
		assert(withNan.extent.width == 0 || withNan.extent.height == 0);

		VkRect2D nanSize = lvgui::UiRenderer::clampToFramebuffer({ 0, 0, nan, nan }, extent(800, 600));
		assert(nanSize.extent.width == 0 || nanSize.extent.height == 0);

		// Infinities must still land inside the framebuffer rather than overflowing.
		VkRect2D withInf = lvgui::UiRenderer::clampToFramebuffer({ 0, 0, inf, inf }, extent(800, 600));
		assert(withInf.offset.x + static_cast<std::int32_t>(withInf.extent.width) <= 800);
		assert(withInf.offset.y + static_cast<std::int32_t>(withInf.extent.height) <= 600);

		std::cout << "✓ testNonFiniteClipIsEmpty\n";
	}

	// Whatever the clip rect and framebuffer, the result must be a legal
	// VkRect2D: non-negative origin, and origin+extent within the framebuffer.
	void testNeverProducesAnIllegalScissor() {
		const float values[] = { -10000, -321.5f, -1, 0, 0.5f, 1, 799, 800, 1234.75f, 10000 };
		const VkExtent2D framebuffers[] = { extent(1, 1), extent(800, 600), extent(1920, 1080) };

		int checked = 0;
		for (const VkExtent2D& fb : framebuffers) {
			for (float x : values) {
				for (float y : values) {
					for (float w : values) {
						for (float h : values) {
							VkRect2D r = lvgui::UiRenderer::clampToFramebuffer({ x, y, w, h }, fb);
							assert(r.offset.x >= 0);
							assert(r.offset.y >= 0);
							assert(r.offset.x + static_cast<std::int32_t>(r.extent.width) <=
							       static_cast<std::int32_t>(fb.width));
							assert(r.offset.y + static_cast<std::int32_t>(r.extent.height) <=
							       static_cast<std::int32_t>(fb.height));
							++checked;
						}
					}
				}
			}
		}
		std::cout << "✓ testNeverProducesAnIllegalScissor (" << checked << " combinations)\n";
	}
}

int main() {
	testFullyInsideIsUnchanged();
	testNegativeOriginClampsAndShrinksExtent();
	testClampsToFramebufferFarEdge();
	testFullyOutsideIsEmpty();
	testZeroAreaIsEmpty();
	testOneByOneFramebufferNeverExceedsBounds();
	testContentScaleScalesClipRect();
	testNonFiniteClipIsEmpty();
	testNeverProducesAnIllegalScissor();

	std::cout << "\n✅ All UiRenderer scissor tests passed!\n";
	return 0;
}
