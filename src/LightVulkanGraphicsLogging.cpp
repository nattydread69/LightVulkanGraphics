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
#include <iostream>
#include <ostream>
#include <streambuf>

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

	std::atomic<bool> consoleOutputEnabled{true};

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
		return consoleOutputEnabled.load(std::memory_order_relaxed);
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
