# 05 — Widgets

Every widget is specified here as: purpose, public interface, interaction rules, drawing
recipe, and edge cases. Claude Code should be able to implement each one from this
document alone.

## The base class

```cpp
class Widget {
public:
    virtual ~Widget() = default;

    WidgetId id() const noexcept { return m_id; }
    Panel*   panel() const noexcept { return m_panel; }

    // ---- layout ----
    virtual Vec2 preferredSize(const GuiContext&) const = 0;
    virtual void setBounds(const Rect& r) { m_bounds = r; }
    Rect bounds() const noexcept { return m_bounds; }

    // ---- interaction ----
    virtual void update(GuiContext&) {}
    virtual bool acceptsFocus()   const { return false; }
    virtual bool acceptsCapture() const { return true;  }
    virtual bool wantsTextInput() const { return false; }
    virtual bool hitTest(Vec2 p) const { return m_bounds.contains(p); }

    // ---- drawing ----
    virtual void draw(DrawList&, const GuiContext&) const = 0;

    // ---- common state ----
    void setEnabled(bool);   bool enabled() const;
    void setVisible(bool);   bool visible() const;
    void setLabel(std::string);
    void setTooltip(std::string);
    void setLabelWidthOverride(float px);   // -1 = use theme ratio

protected:
    WidgetId    m_id;
    Panel*      m_panel = nullptr;
    Rect        m_bounds;
    std::string m_label, m_tooltip;
    bool        m_enabled = true, m_visible = true;
};
```

`acceptsCapture()` returning false is used by `Label` and `Separator`, so a click passes
through to whatever is behind — in practice, to the panel, which then does nothing. It
prevents a stray click on a label from clearing focus in a way that feels arbitrary.

### The label / control split

Almost every widget renders as `[label text][control]` within its assigned row. The split
point is `theme.labelWidthRatio` (default 0.42) of the row width, overridable per widget.
This one convention is what makes a stack of heterogeneous widgets look like a designed
control panel instead of a pile of boxes. See [06-layout-and-theme.md](06-layout-and-theme.md).

A widget with an empty label gets the full row width for its control.

---

## Label

Static, non-interactive text.

```cpp
class Label : public Widget {
public:
    explicit Label(std::string text);
    void setText(std::string);
    std::string_view text() const;
    void setAlign(Align h);              // Left | Center | Right
    void setWordWrap(bool);              // greedy break at spaces
    void setColor(Color);                // default: theme.text
};
```

- `acceptsCapture()` → `false`, `acceptsFocus()` → `false`
- `preferredSize`: `measureText` if not wrapping; if wrapping, the row width is known
  only at layout time, so return `{0, lineHeight}` and recompute height in `setBounds`.
  Panels must therefore run layout twice when any wrapping label is present, or accept
  that a wrapping label's height is one frame stale. **Recommendation: run layout twice
  when the panel contains a wrapping label.** It's cheap and avoids a visible pop.
- Long non-wrapping text is truncated with an ellipsis: measure, and if it exceeds the
  region, binary-search the byte index that fits `width - measureText("…")`.

## Separator

A 1px horizontal rule with `theme.itemSpacing` above and below.

```cpp
class Separator : public Widget {
public:
    Separator() = default;
    explicit Separator(std::string caption);   // optional inline caption
};
```

With a caption, draw: line, gap, text, gap, line. It is the cheapest way to give a long
panel visible structure.

## Spacer

```cpp
class Spacer : public Widget {
public:
    explicit Spacer(float height);
};
```

Invisible, non-interactive, fixed height. Two lines of code and it saves users from
fighting the layout.

---

## Button

```cpp
class Button : public Widget {
public:
    explicit Button(std::string text);
    void setOnClick(std::function<void()>);
    void setToggle(bool isToggle);          // sticky on/off
    bool isPressed() const;                 // toggle state
    void setPressed(bool);
};
```

**Interaction.** Fires on *release inside the widget after a press inside the widget*.
Not on press. This is the standard behaviour of every native toolkit and lets a user
change their mind by dragging off the button before releasing.

```
if ctx.activeId == id():
    m_held = hitTest(ctx.input().mousePos);
    if ctx.input().mouseReleased[Left]:
        if m_held and m_onClick: m_onClick();
        if m_isToggle: m_pressed = !m_pressed;
```

`acceptsFocus()` → `true`. When focused, Space or Enter activates it.

**Drawing.** Filled rounded rect, then centred text. Colour selection:

| State | Fill |
|-------|------|
| disabled | `theme.frameBg` at 40% alpha, text `theme.textDisabled` |
| held (active + hovered) | `theme.accentActive` |
| hovered | `theme.accentHovered` |
| toggle on | `theme.accent` |
| default | `theme.frameBg` |

Focused widgets additionally get a 1px `theme.accent` border. Do this uniformly for
every focusable widget — a keyboard user needs to see where they are.

## Checkbox — the tick box

```cpp
class Checkbox : public Widget {
public:
    explicit Checkbox(std::string label, bool initial = false);
    bool value() const;
    void setValue(bool, bool fireCallback = false);
    void bind(bool* target);                       // optional two-way binding
    void setOnChange(std::function<void(bool)>);
    void setTriState(bool);                        // adds Indeterminate
    enum class State { Off, On, Indeterminate };
    State state() const;
};
```

**Interaction.** Toggles on mouse *press* (not release) — checkboxes are conventionally
immediate, and the tick appearing under the cursor on press feels responsive. Space
toggles when focused. The whole row, label included, is clickable; this is a small
usability detail that users notice only by its absence.

**Binding.** `bind(bool*)` is the answer to the retained-mode state-sync problem. When
bound, `update()` reads the target at the start of the frame (so external changes show up)
and writes it on toggle. Callback still fires. The pointer must outlive the widget; say
so in the header comment.

**Drawing.** A square of side `theme.fontSize`, vertically centred in the row, at the
control-column origin:

```
box = Rect at controlX, centred vertically, side = fontSize
addRectFilled(box, stateFill, theme.rounding)
addRect(box, theme.border, 1.0f, theme.rounding)

if state == On:
    # tick as a two-segment polyline, inset ~22% of the box
    p0 = box.min + box.size * (0.24, 0.52)
    p1 = box.min + box.size * (0.42, 0.72)
    p2 = box.min + box.size * (0.76, 0.28)
    addPolyline({p0,p1,p2}, theme.checkMark, 2.0f, false)

if state == Indeterminate:
    addRectFilled(box inset by 28%, theme.checkMark)

# label to the right of the box, or in the label column — pick ONE convention.
```

**Convention decision:** the checkbox control sits in the *control* column like every
other widget, with its label in the label column. This keeps a column of controls
vertically aligned, which is what makes a settings panel scannable. It differs from
Dear ImGui (which puts the label to the right of the box) and that is deliberate.

## RadioButton and RadioGroup

```cpp
class RadioGroup {
public:
    int  value() const;
    void setValue(int);
    void setOnChange(std::function<void(int)>);
};

class RadioButton : public Widget {
public:
    RadioButton(std::string label, RadioGroup* group, int valueWhenSelected);
};
```

Drawn as a circle with a filled inner dot. Same interaction as `Checkbox` except it
cannot be toggled off by clicking an already-selected button. Arrow keys move the
selection within the group when any member is focused.

---

## Slider

The centrepiece. Specified in more detail than the others because this is where the
usability lives.

```cpp
enum class SliderScale { Linear, Logarithmic };

template <typename T>   // T ∈ { float, double, int }
class SliderT : public Widget {
public:
    SliderT(std::string label, T minVal, T maxVal, T initial);

    T    value() const;
    void setValue(T, bool fireCallback = false);
    void bind(T* target);

    void setRange(T minVal, T maxVal);
    void setStep(T);                        // 0 = continuous
    void setScale(SliderScale);
    void setFormat(std::string printfFmt);  // default "%.3f" / "%d"
    void setUnitSuffix(std::string);        // " m/s", " rad"
    void setShowValue(bool);                // value text over the track, default true

    void setOnChange(std::function<void(T)>);  // fires continuously during drag
    void setOnCommit(std::function<void(T)>);  // fires once on release
};

using Slider    = SliderT<float>;
using SliderInt = SliderT<int>;
```

Two callbacks matter. A simulation that rebuilds a mesh on every parameter change wants
`onCommit`; one that just updates a uniform wants `onChange`. Providing only one forces
users into either laggy dragging or stale values.

### Interaction rules

| Input | Behaviour |
|-------|-----------|
| Press on track | Jump handle to cursor, take capture, begin drag |
| Drag | Value follows cursor X, clamped to range |
| Release | Release capture, fire `onCommit` |
| **Shift + drag** | Fine adjustment — 0.1× cursor sensitivity |
| **Ctrl/Cmd + click** | Switch to inline text entry (see below) |
| **Double click** | Reset to the initial value supplied at construction |
| Left / Right arrow (focused) | Step by `step`, or 1% of range if step is 0 |
| Shift + arrow | 10× step |
| Wheel while hovered | Step, and consume the wheel so the panel doesn't scroll |

Shift-fine-drag is not optional polish. On a 200px track mapping a range of 0–1000, one
pixel is 5 units. Without a fine mode, precise values are unreachable and users resort to
text entry for everything.

### Inline text entry

Ctrl-click replaces the slider's control region with a live `TextBox` for one editing
session. On Enter or focus loss, parse with `strtod`/`strtol`; on success clamp to range
and commit; on failure revert silently and restore the slider.

Implement by having `SliderT` own an optional `std::unique_ptr<TextBox>` that it creates
on demand, forwards `update`/`draw` to while active, and destroys on commit. Do not
duplicate the text-editing logic.

### Logarithmic scale

For scientific ranges spanning orders of magnitude (viscosity, timestep, stiffness) a
linear slider is useless — 99% of the track covers values nobody wants.

```cpp
// value → normalised position [0,1]
float t = (std::log(value) - std::log(minVal)) / (std::log(maxVal) - std::log(minVal));
// position → value
T value = std::exp(std::log(minVal) + t * (std::log(maxVal) - std::log(minVal)));
```

Guard: logarithmic requires `minVal > 0`. Assert on construction with a clear message,
and document that a range crossing or touching zero must use `Linear`.

### Stepping

```cpp
if (step > 0) value = minVal + std::round((value - minVal) / step) * step;
value = std::clamp(value, minVal, maxVal);
```

Apply the round **after** the clamp for integers, or a range of 0–10 with step 3 can
produce 12. Apply clamp last. Write a test for exactly this.

### Drawing recipe

```
row      = m_bounds
labelR   = row.leftPortion(labelWidth)
track    = row.rightPortion().insetY((row.h - theme.sliderTrackHeight) / 2)

addTextClipped(labelR, m_label, theme.text or textDisabled, Left, Middle)

# track background
addRectFilled(track, theme.frameBg, track.h * 0.5)

# filled portion up to the handle
t        = normalisedPosition(value)
fillEnd  = track.x + t * track.w
addRectFilled(Rect(track.x, track.y, fillEnd - track.x, track.h),
              theme.accent, track.h * 0.5)

# handle: rounded rect, not a circle — crisper without AA
hw = theme.sliderHandleWidth        # ~10px
handle = Rect(fillEnd - hw/2, row.y + 2, hw, row.h - 4)
addRectFilled(handle, handleColorForState(), theme.rounding)
addRect(handle, theme.border, 1.0f, theme.rounding)

# value text, centred over the track, drawn last so it sits above the fill
if showValue:
    text = format(value) + unitSuffix
    addTextClipped(track, text, theme.text, Center, Middle)
```

Snap `fillEnd` and the handle rect to integer pixels. An unsnapped handle shimmers as it
moves, which reads as low quality even when nobody can say why.

## DragValue

An unbounded numeric scrubber — no track, no range. Essential for quantities like a
camera position or a mass, where an arbitrary min/max is a lie.

```cpp
template <typename T>
class DragValueT : public Widget {
public:
    DragValueT(std::string label, T initial, float speed = 0.1f);
    void setSpeed(float unitsPerPixel);
    void setSoftRange(T lo, T hi);      // clamps but the slider is not scaled to it
    // value(), setValue(), bind(), setFormat(), setOnChange(), setOnCommit() as Slider
};
```

Interaction: press and drag horizontally; value changes by `dx * speed`. Shift = 0.1×,
Alt/Ctrl = 10×. Ctrl-click for text entry, as with `Slider`. The control region is drawn
as a `frameBg` rect with the formatted value centred — visually identical to a text box,
which correctly signals "this holds a number you can edit".

## Vec3Field

Three `DragValue`s in a row, labelled X/Y/Z with the conventional red/green/blue tint on
each sub-field's border. Given how much of your API takes `glm::vec3` — positions,
scales, light directions — this will be one of the most-used widgets in practice.

```cpp
class Vec3Field : public Widget {
public:
    Vec3Field(std::string label, glm::vec3 initial);
    glm::vec3 value() const;
    void setValue(glm::vec3, bool fireCallback = false);
    void bind(glm::vec3* target);
    void setOnChange(std::function<void(glm::vec3)>);
};
```

Implement as a composite that owns three `DragValueT<float>` children, splitting the
control column into thirds with `theme.itemSpacing` between. The composite pattern used
here should be reusable — factor out a small `CompositeWidget` base that forwards
`update`, `draw`, and hit-testing to children.

---

## TextBox

The hardest widget. Budget more time for this than for the rest combined, and test the
editing state machine headlessly ([10-testing.md](10-testing.md)).

```cpp
enum class TextFilter { None, Integer, Decimal, Identifier };

class TextBox : public Widget {
public:
    explicit TextBox(std::string label, std::string initial = {});

    std::string_view text() const;
    void setText(std::string, bool fireCallback = false);
    void bind(std::string* target);

    void setPlaceholder(std::string);
    void setMaxLength(size_t);              // in codepoints, 0 = unlimited
    void setFilter(TextFilter);
    void setReadOnly(bool);
    void setPasswordMode(bool);

    void setOnChange(std::function<void(std::string_view)>);  // every keystroke
    void setOnCommit(std::function<void(std::string_view)>);  // Enter or focus loss
    void setValidator(std::function<bool(std::string_view)>); // red border when false

    bool acceptsFocus()   const override { return true; }
    bool wantsTextInput() const override { return true; }
};
```

### State

```cpp
std::string m_text;        // UTF-8
size_t      m_caret  = 0;  // byte index, always on a codepoint boundary
size_t      m_anchor = 0;  // selection anchor; caret == anchor means no selection
float       m_scrollX = 0; // horizontal scroll when text exceeds the box
std::string m_textBeforeEdit;  // for Escape revert
double      m_lastCaretMoveTime = 0;  // for blink phase reset
```

Selection is the ordered pair `[min(caret, anchor), max(caret, anchor))`.

### Editing operations

Implement each as a small named method so they can be unit-tested directly:

| Operation | Trigger | Rule |
|-----------|---------|------|
| `insertText(cp)` | char event | Replace selection if any, insert at caret, caret advances |
| `deleteBackward()` | Backspace | If selection, delete it; else delete the codepoint before caret |
| `deleteForward()` | Delete | If selection, delete it; else delete the codepoint at caret |
| `moveCaret(dir, select)` | Arrows | `select` = Shift held; if not selecting, collapse selection to the appropriate end first |
| `moveCaretWord(dir, select)` | Ctrl+Arrow | Skip whitespace, then skip word characters |
| `moveHome/End(select)` | Home/End | |
| `selectAll()` | Ctrl+A | |
| `copy()` / `cut()` / `paste()` | Ctrl+C/X/V | Paste strips control characters and newlines |
| `commit()` | Enter, focus loss | Fire `onCommit`, keep text |
| `revert()` | Escape | Restore `m_textBeforeEdit`, clear focus |

**The collapse rule catches people out.** Pressing Right with an active selection and no
Shift should move the caret to the *right edge of the selection* and deselect — not move
one character past the caret. Every native text field behaves this way and its absence
feels broken without users being able to articulate why.

### Mouse

- Press inside: place caret at `Font::indexAtOffset`, clear selection, take capture
- Drag: extend selection to the cursor position; auto-scroll when dragging past an edge
- Double-click: select the word under the cursor
- Triple-click: select all

### Drawing

```
box = control region
addRectFilled(box, focused ? theme.frameBgActive : theme.frameBg, theme.rounding)
addRect(box, validationFailed ? theme.error
                              : (focused ? theme.accent : theme.border),
        1.0f, theme.rounding)

pushClipRect(box.inset(theme.framePadding))

if selection non-empty and focused:
    x0 = offsetAtIndex(selStart), x1 = offsetAtIndex(selEnd)
    addRectFilled(Rect over [x0,x1], theme.selectionBg)

addText(text.empty() ? placeholder : displayText,
        text.empty() ? theme.textDisabled : theme.text,
        textOrigin - Vec2(m_scrollX, 0))

if focused and blinkPhaseVisible():
    caretX = offsetAtIndex(m_caret) - m_scrollX
    addRectFilled(Rect(caretX, box.y+2, 1, box.h-4), theme.text)

popClipRect()
```

Blink: 1.06 s period, visible for the first 53%, phase **reset to fully visible on every
caret move**. A caret that blinks out exactly as you finish typing looks like a dropped
keystroke.

Scroll: after any caret move, if `offsetAtIndex(caret) - scrollX` falls outside
`[0, boxWidth - padding]`, adjust `scrollX` to bring it just inside. Do this in one place,
called at the end of every editing operation.

`setPasswordMode` renders `•` (U+2022) per codepoint but must measure the *displayed*
string for caret positioning, not the source. Easy to get wrong.

---

## DropDown

```cpp
class DropDown : public Widget {
public:
    DropDown(std::string label, std::vector<std::string> items, int initialIndex = 0);
    int  selectedIndex() const;
    std::string_view selectedText() const;
    void setItems(std::vector<std::string>);
    void setSelectedIndex(int, bool fireCallback = false);
    void setOnChange(std::function<void(int)>);
};
```

The popup must escape its parent panel's clip rect and draw above every other panel.
Mechanism: when open, the context records `popupOwner = this`; `GuiContext::endFrame`
draws the popup into `overlayList` after all panels, using `pushClipRect(rect, false)` to
replace rather than intersect.

Popup rules: opens below the control, flips above if it would run off the bottom of the
framebuffer; scrolls if more than 12 items; closes on selection, Escape, or any click
outside; arrow keys move the highlight and Enter selects.

While a popup is open, `GuiContext::wantsMouse()` returns true regardless of cursor
position, and hit testing checks the popup before any panel.

## ProgressBar

```cpp
class ProgressBar : public Widget {
public:
    explicit ProgressBar(std::string label);
    void setFraction(float);            // clamped to [0,1]
    void setIndeterminate(bool);        // animated sweep
    void setOverlayText(std::string);   // e.g. "1420 / 5000 steps"
};
```

Non-interactive. Indeterminate mode sweeps a bar of 30% width across the track on a 1.5 s
loop using accumulated `deltaTime`.

## PlotLine

A sparkline. Small, cheap, and disproportionately useful in a simulation panel.

```cpp
class PlotLine : public Widget {
public:
    PlotLine(std::string label, size_t historySize = 256);
    void push(float sample);                  // ring buffer
    void setValues(std::span<const float>);   // or supply externally
    void setRange(float lo, float hi);        // NaN,NaN = auto-scale
    void setHeight(float px);
    void setShowLatestValue(bool);
};
```

Draw with a single `addPolyline` over the ring buffer mapped to the plot rect. Auto-scale
should track a running min/max with mild hysteresis, or the plot jumps distractingly on
every new extreme.

## CollapsingSection

```cpp
class CollapsingSection : public Widget {
public:
    explicit CollapsingSection(std::string title, bool openInitially = true);
    template <typename W, typename... Args> W* add(Args&&...);
    void setOpen(bool);
    bool isOpen() const;
};
```

Owns children. `preferredSize` returns just the header height when closed, header plus
laid-out children when open. Header draws a triangle glyph (built from
`addTriangleFilled`, not a font character) plus the title, and toggles on click.

This is how a panel with forty parameters stays usable.

---

## Panel

```cpp
enum class PanelFlags : uint32_t {
    None        = 0,
    Movable     = 1 << 0,
    Resizable   = 1 << 1,
    Collapsible = 1 << 2,
    Closable    = 1 << 3,
    Scrollable  = 1 << 4,
    NoTitleBar  = 1 << 5,
    NoBackground= 1 << 6,
    Default     = Movable | Resizable | Collapsible | Scrollable,
};

class Panel {
public:
    template <typename W, typename... Args>
    W* add(Args&&... args);          // constructs, takes ownership, returns raw ptr

    void remove(Widget*);
    void clear();

    void setTitle(std::string);
    void setBounds(const Rect&);
    void setFlags(PanelFlags);
    void setVisible(bool);
    void setCollapsed(bool);
    void bringToFront();

    // Anchoring so panels survive window resize sensibly
    enum class Anchor { TopLeft, TopRight, BottomLeft, BottomRight };
    void setAnchor(Anchor);
};
```

**Anchoring matters more than it sounds.** Without it, a panel placed at the right edge
of the window ends up floating in the middle after the user maximises. Store the offset
from the anchored corner and recompute the absolute position whenever `displaySize`
changes.

**Constraints.** Minimum size is `titleBarHeight + 2 * windowPadding` by
`8 * fontSize`. Clamp the panel rect so at least the title bar stays on screen — a panel
dragged fully off the top edge becomes permanently unreachable.

**Scrolling.** When content height exceeds the content region, draw a vertical scrollbar
in a `theme.scrollbarWidth` gutter on the right, inset the content region accordingly,
and offset widget layout by `-scrollY`. Wheel scrolls by 3 × `lineHeight`. Clamp
`scrollY` to `[0, contentHeight - viewHeight]`.

**Resize grip.** A `theme.resizeGripSize` triangle in the bottom-right corner, hit-tested
before widgets. Sets the cursor to `ResizeNWSE` on hover.
