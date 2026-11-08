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

#include "VkAppExtras.h"

#include <memory>
#include <mutex>

namespace lightGraphics::detail
{
	namespace
	{
		std::mutex& extrasMutex()
		{
			static std::mutex m;
			return m;
		}

		std::unordered_map<const void*, std::unique_ptr<AppExtras>>& extrasTable()
		{
			static std::unordered_map<const void*, std::unique_ptr<AppExtras>> table;
			return table;
		}
	}

	AppExtras& appExtras(const void* app)
	{
		std::lock_guard<std::mutex> lock(extrasMutex());
		std::unique_ptr<AppExtras>& entry = extrasTable()[app];
		if (!entry)
		{
			entry = std::make_unique<AppExtras>();
		}
		return *entry;
	}

	void forgetAppExtras(const void* app)
	{
		std::lock_guard<std::mutex> lock(extrasMutex());
		extrasTable().erase(app);
	}
}
