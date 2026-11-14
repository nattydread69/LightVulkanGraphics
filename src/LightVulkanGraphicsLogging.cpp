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

#include "LightVulkanGraphicsLogging.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <ostream>
#include <streambuf>
#include <string>

namespace
{
	class NullStreamBuffer final : public std::streambuf
	{
	protected:
		int overflow(int character) override
		{
			return traits_type::not_eof(character);
		}
	};

	class NullOStream final : public std::ostream
	{
	public:
		NullOStream()
		    : std::ostream(&buffer_)
		{
		}

	private:
		NullStreamBuffer buffer_;
	};

	std::atomic<bool> consoleOutputEnabled{false};

	// LVG_CONSOLE_OUTPUT overrides the application's setting: 1/on forces output
	// on, 0/off forces it off, unset defers to setConsoleOutputEnabled().
	// Returns -1 (unset), 0 or 1.
	int environmentOverride()
	{
		static const int value = []
		{
			std::string setting;
#if defined(_WIN32)
			char* raw = nullptr;
			std::size_t length = 0;
			if (_dupenv_s(&raw, &length, "LVG_CONSOLE_OUTPUT") != 0 || raw == nullptr)
			{
				return -1;
			}
			setting.assign(raw);
			std::free(raw);
#else
			char const* raw = std::getenv("LVG_CONSOLE_OUTPUT");
			if (raw == nullptr)
			{
				return -1;
			}
			setting.assign(raw);
#endif
			if (setting == "1" || setting == "on") return 1;
			if (setting == "0" || setting == "off") return 0;
			return -1;
		}();
		return value;
	}

	NullOStream& nullStream()
	{
		static NullOStream stream;
		return stream;
	}
}

namespace lightGraphics
{
	void setConsoleOutputEnabled(bool enabled)
	{
		consoleOutputEnabled.store(enabled, std::memory_order_relaxed);
	}

	bool getConsoleOutputEnabled()
	{
		const int override = environmentOverride();
		return override >= 0 ? override == 1 : consoleOutputEnabled.load(std::memory_order_relaxed);
	}

	std::ostream& consoleInfoStream()
	{
		return getConsoleOutputEnabled() ? std::cout : nullStream();
	}

	std::ostream& consoleErrorStream()
	{
		return getConsoleOutputEnabled() ? std::cerr : nullStream();
	}
}
