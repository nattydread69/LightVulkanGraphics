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

// X11 PRIMARY selection (X11Primary.h): two independent X connections in one process, one owning
// PRIMARY and the other fetching it, as two programs would. Skipped (exit 0) with no X display.

#include "X11Primary.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace lvgui = lightGraphics::ui;

int main() {
	if (!std::getenv("DISPLAY")) {
		std::cout << "skipped: no X display\n";
		return 0;
	}
	auto a = lvgui::X11Primary::create();
	auto b = lvgui::X11Primary::create();
	if (!a || !b) {
		std::cout << "skipped: cannot open an X connection\n";
		return 0;
	}
	a->setText("selected in A");
	assert(a->getText() == "selected in A");   // the owner answers from memory
	assert(b->getText() == "selected in A");   // another connection fetches it over X

	b->setText("now owned by B");              // takes PRIMARY away from A
	for (int i = 0; i < 50 && a->getText() != "now owned by B"; ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));   // A learns of the loss asynchronously
	}
	assert(a->getText() == "now owned by B");
	assert(b->getText() == "now owned by B");

	// A selection far above one X property (INCR): served in chunks by the owner, reassembled by
	// the requestor, in both directions. Multi-byte characters straddle chunk boundaries.
	std::string big;
	for (int i = 0; big.size() < 3 * 1024 * 1024; ++i) {
		big += "line " + std::to_string(i) + " \xC3\xA9\xE2\x82\xAC caf\xC3\xA9\n";
	}
	a->setText(big);
	for (int i = 0; i < 50; ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));   // B learns it is no longer the owner
	}
	const std::string fromA = b->getText();
	assert(fromA.size() == big.size());
	assert(fromA == big);

	b->setText(big);
	for (int i = 0; i < 50; ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	assert(a->getText() == big);

	std::cout << "✓ x11 PRIMARY round trip\n";
	return 0;
}
