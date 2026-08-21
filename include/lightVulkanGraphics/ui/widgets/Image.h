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

// docs/gui/05-widgets.md, "Image": draws a texture registered with
// VkApp::registerUiTexture() (docs/gui/07-public-api.md) at a fixed logical-pixel size.
// Non-interactive -- there is nothing here for a user to press or drag.

#include "../Widget.h"

#include <string>

namespace lightGraphics::ui {

class Image : public Widget {
public:
	// `size` is the image's OWN logical-pixel size, drawn at that size regardless of how
	// wide the control column is -- unlike most widgets, an Image does not stretch to
	// fill its row. A colour-legend gradient or an icon has a meaningful aspect ratio;
	// stretching it to whatever width the panel happens to be would distort it.
	Image(std::string label, TextureId textureId, Vec2 size);

	void setTextureId(TextureId id) { m_textureId = id; }
	TextureId textureId() const { return m_textureId; }
	void setSize(Vec2 size) { m_size = size; }
	Vec2 size() const { return m_size; }
	// Sub-rectangle of the texture to sample, in [0,1] normalised coordinates -- the
	// same convention DrawList::addImage() itself uses. Defaults to the whole image.
	void setUVRect(Vec2 uv0, Vec2 uv1) { m_uv0 = uv0; m_uv1 = uv1; }
	// Multiplies every sampled texel -- opaque white (the default) means "no tint".
	void setTint(Color tint) { m_tint = tint; }

	Vec2 preferredSize(const GuiContext& ctx) const override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsCapture() const override { return false; }

private:
	TextureId m_textureId;
	Vec2 m_size;
	Vec2 m_uv0{ 0.0f, 0.0f };
	Vec2 m_uv1{ 1.0f, 1.0f };
	Color m_tint{ 0xFF, 0xFF, 0xFF, 0xFF };
};

} // namespace lightGraphics::ui
