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

// Internal (not installed): how the main loop is paced, set where the
// swapchain is made (VkApp::createSwapChain) and read by VkApp::mainLoop().
// Kept out of VkApp's members so the installed VkApp.h -- and with it the
// class's layout, which applications deriving from it compile in -- stays as
// it is.
//
// The swapchain presents with MAILBOX where the surface has it, else FIFO.
// FIFO (vsync) blocks the whole main loop in present while the window is not
// shown -- a Wayland compositor sends no frame callbacks to a hidden or
// covered window -- so an application stops, its background work never
// noticed done, until the window is shown again. MAILBOX does not wait; the
// main loop then paces itself to the monitor's rate (frameIntervalSeconds)
// instead of spinning. LVG_PRESENT_MODE=fifo: FIFO regardless.

namespace lightGraphics::detail
{
	inline double frameIntervalSeconds = 0.0; // 0: presenting paces the loop (FIFO)
	inline double frameStartTime = 0.0;       // glfwGetTime() as the last frame began
}
