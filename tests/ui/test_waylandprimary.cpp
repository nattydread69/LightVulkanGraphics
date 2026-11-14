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

// Wayland PRIMARY selection (WaylandPrimary.h) against a live compositor, on a connection of
// the test's own. That connection owns no window, so the compositor sends it no keyboard focus
// and no selection offers: this checks what can be checked without one -- binding the
// protocol, taking ownership and reading it back, releasing, and shutting down cleanly -- and
// that the compositor raises no protocol error. Skipped (exit 0) without a Wayland session.
// The cross-program path needs a focused window; run an app with LVG_DEBUG_PRIMARY=1.

#include "WaylandPrimary.h"

#include <dlfcn.h>

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace lvgui = lightGraphics::ui;

int main() {
	if (!std::getenv("WAYLAND_DISPLAY") && !std::getenv("WAYLAND_SOCKET")) {
		std::cout << "skipped: no Wayland session\n";
		return 0;
	}
	void* lib = dlopen("libwayland-client.so.0", RTLD_LAZY | RTLD_LOCAL);
	if (!lib) {
		std::cout << "skipped: no libwayland-client\n";
		return 0;
	}
	using ConnectFn = void* (*)(const char*);
	using DisconnectFn = void (*)(void*);
	using ErrorFn = int (*)(void*);
	auto connect = reinterpret_cast<ConnectFn>(dlsym(lib, "wl_display_connect"));
	auto disconnect = reinterpret_cast<DisconnectFn>(dlsym(lib, "wl_display_disconnect"));
	auto getError = reinterpret_cast<ErrorFn>(dlsym(lib, "wl_display_get_error"));
	void* display = connect ? connect(nullptr) : nullptr;
	if (!display) {
		std::cout << "skipped: cannot connect to the Wayland compositor\n";
		return 0;
	}

	auto primary = lvgui::WaylandPrimary::create(display);
	if (!primary) {
		std::cout << "skipped: compositor has no primary-selection protocol\n";
		disconnect(display);
		return 0;
	}

	// This connection has no window and so no valid input serial: a compositor will usually
	// cancel the selection at once, and may also accept it. Either is correct; what matters
	// is that nothing crashes and no protocol error is raised.
	const std::string text = "lvg wayland primary test \xC3\xA9";
	primary->setText(text);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	const std::string got = primary->getText();
	assert(got.empty() || got == text);
	std::cout << "ok bind + set (" << (got.empty() ? "compositor declined without a serial" : "owned") << ")\n";

	primary->setText("");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	assert(primary->getText().empty());
	std::cout << "ok release\n";

	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	assert(getError(display) == 0);   // no protocol error was raised

	primary->shutdown();
	primary->shutdown();   // idempotent
	primary->setText("after shutdown");
	assert(primary->getText().empty());
	assert(getError(display) == 0);
	std::cout << "ok shutdown\n";

	disconnect(display);
	std::cout << "All WaylandPrimary tests passed\n";
	return 0;
}
