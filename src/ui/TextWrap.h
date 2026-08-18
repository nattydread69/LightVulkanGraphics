#pragma once

// Word-wrap, shared between Label (docs/gui/05-widgets.md, "Label") and LogView
// (docs/gui/05-widgets.md, "LogView") -- extracted here once LogView became a second
// caller needing the exact same greedy-break-at-the-last-space algorithm Label already
// had worked out and tested. Internal only, like Utf8.h: not part of the public GUI API,
// exercised indirectly through both widgets' own tests plus test_utf8.cpp's neighbours.

#include <lightVulkanGraphics/ui/Types.h>

#include <string_view>
#include <vector>

namespace lightGraphics::ui {

class Font;

// Greedy word-wrap: breaks at the last space before a line would exceed `width`. A
// single word wider than `width` on its own is never split mid-word -- it overflows
// that one line rather than being hyphenated (matching Label's own long-standing
// behaviour; hyphenation is out of scope for a scientific-visualisation parameter panel).
// `width <= 0` or empty `text` returns the whole string as a single line, unwrapped.
std::vector<std::string_view> wrapText(const Font& font, std::string_view text, float pixelSize, float width,
                                        TextFlags flags = TextFlags::None);

} // namespace lightGraphics::ui
