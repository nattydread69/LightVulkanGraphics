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

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lightGraphics::ui {

// Decodes the codepoint starting at s[pos] and advances pos past it. On malformed
// input (bad lead byte, truncated sequence, overlong encoding, surrogate codepoint)
// emits U+FFFD and advances pos by exactly one byte, so callers can never spin forever
// on untrusted text.
std::uint32_t decodeUtf8(std::string_view s, std::size_t& pos);

// Byte index of the codepoint boundary immediately before pos. Never walks back more
// than one codepoint's worth of continuation bytes, so malformed runs of continuation
// bytes cannot pull the caret arbitrarily far.
std::size_t utf8PrevBoundary(std::string_view s, std::size_t pos);

// Byte index of the codepoint boundary immediately after pos.
std::size_t utf8NextBoundary(std::string_view s, std::size_t pos);

// Appends the UTF-8 encoding of cp to out. Surrogates and codepoints beyond U+10FFFF
// are replaced with U+FFFD.
void utf8Encode(std::uint32_t cp, std::string& out);

} // namespace lightGraphics::ui
