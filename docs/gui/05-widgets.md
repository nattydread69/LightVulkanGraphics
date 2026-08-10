# 05 — Widgets

Every widget is specified here as: purpose, public interface, interaction rules, drawing
recipe, and edge cases.

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

## CompositeWidget

A shared base for widgets that own a small tree of interactive children — implemented
once here rather than duplicated across `Row`, `Vec3Field`, and `CollapsingSection`
below, all three of which derive from it.

```cpp
class CompositeWidget : public Widget {
public:
    template <typename W, typename... Args>
    W* add(Args&&... args);          // mirrors Panel::add<W>

    std::size_t childCount() const;
    Widget* childAt(std::size_t) const;

    // update()/draw()/hitTest()/hitTestDeep()/findDescendant()/collectFocusable() are
    // all overridden to forward to children. preferredSize() and layout() (see below)
    // are NOT overridden — every derived composite must supply both; there is no
    // default that suits more than one of Row/Vec3Field/CollapsingSection.
protected:
    std::vector<std::unique_ptr<Widget>> m_children;
};
```

A derived composite overrides `preferredSize()` (still pure-virtual, inherited from
`Widget`) and `layout(const GuiContext&)` (see below) to run its own placement policy —
Row splits horizontally, Vec3Field into thirds, CollapsingSection stacks vertically below
a header.

**Each child is independently interactive**, which requires three small extensions to
`Widget` itself. A child (e.g. a `DragValue` inside a `Vec3Field`) needs its own
hover/capture/focus, exactly like a top-level `Panel` widget —
but `GuiContext`'s hit-testing, id lookup, and Tab-cycling only ever walked a panel's
*top-level* widget list. Three small virtuals were added to `Widget`, each with a
leaf-widget default that makes every existing widget's behaviour unchanged:

```cpp
virtual Widget* hitTestDeep(Vec2 p) { return hitTest(p) ? this : nullptr; }
virtual Widget* findDescendant(WidgetId) { return nullptr; }
virtual void collectFocusable(std::vector<Widget*>&);   // default: push self iff
                                                          // visible && enabled && acceptsFocus()
```

`GuiContext::update()`'s hit-test, `findWidget()`, and `updateFocusNavigation()`'s
Tab-list now call these instead of `hitTest()` / an id-equality loop / an inlined
`acceptsFocus()` check, so they transparently recurse into a composite's children
without knowing composites exist as a concept. `CompositeWidget` overrides all three:
`hitTestDeep`/`collectFocusable` gate on the composite's OWN `enabled()`/`visible()`
before recursing (see below), and `hitTestDeep` never resolves to the composite itself —
only to a child, or to nothing (the gaps between a Vec3Field's three fields are not a
click target, the same way `Label`'s `acceptsCapture() == false` lets a click pass
through it).

**Layout timing: `layout(const GuiContext&)`, not `setBounds()`.** `Widget::setBounds`
takes only a `Rect` — no `GuiContext` — but a composite's placement policy needs theme
metrics and font measurement. Rather than caching a `GuiContext*` across calls (which
would be one frame stale whenever the composite itself moves or resizes in the same
frame, e.g. during a panel drag), `Widget` gained a second, sibling virtual:

```cpp
virtual void layout(const GuiContext&) {}    // no-op default; CompositeWidget leaves it
                                              // un-overridden -- derived composites
                                              // override it directly to position m_children
```

`Panel::layout()` calls `w->layout(ctx)` immediately after **every** `w->setBounds(...)`
it performs on a widget — including the bounds-override (absolute placement) path — so a
composite's children are always positioned from the exact same bounds, in the exact same
pass, as the composite's own. The alternative — `CompositeWidget` caching a `GuiContext*`
from an earlier call and driving the traversal itself from inside `setBounds()` — doesn't
work: it depends on state cached from an earlier call in the same frame, which breaks for
a composite placed via the bounds-override escape hatch (`Panel::layout()` calls
`setBounds()` there without ever calling `preferredSize()` first, so nothing populates
the cache in time).

**Enabled/visible inheritance is functional, not (fully) visual.** A disabled or
invisible composite skips calling `update()`/`hitTestDeep()`/`collectFocusable()` on its
children entirely, without ever touching a child's own `m_enabled`/`m_visible` — this is
what "a disabled composite disables its children without mutating their own flags"
means in practice. Visual greying-out piggybacks on a NEW `Widget::effectivelyEnabled()`
(`m_enabled && (m_parent == nullptr || m_parent->effectivelyEnabled())`, walking a new
`m_parent` back-pointer that `CompositeWidget::add()` sets), which every existing
widget's `draw()` now reads instead of `m_enabled` directly. `update()`/interaction code
paths were deliberately left reading `m_enabled` — the composite's own gating already
makes them unreachable when a parent is disabled, and mixing the two would be redundant.

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

Implemented as a composite that owns three `DragValueT<float>` children, splitting the
control column into thirds with `theme.itemSpacing` between. `CompositeWidget` (above) is
the reusable base; `Vec3Field` overrides `preferredSize()` and `layout(const
GuiContext&)` to run this splitting policy.

---

## TextBox

The hardest widget. Budget more time for this than for the rest combined, and test the
editing state machine headlessly ([08-testing.md](08-testing.md)).

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

Does **not** derive from `CompositeWidget` — the popup's items are not separate
`Widget`s (no per-item `WidgetId`); the whole control-plus-popup is a single widget that
answers "which item" from its own coordinate math (`itemAtY()`), the same way a
`Panel`'s content region maps a click to a row.

**Popup mechanism.** `GuiContext` exposes an explicit API rather than a bare
`popupOwner` field:

```cpp
void openPopup(Widget* owner, const Rect& screenRect);   // (re-)registers; idempotent
void closePopup();
bool isPopupOpen() const;
Widget* popupOwner() const;   // resolves via findWidget() -- see "must not dangle" below
Rect popupRect() const;
```

`DropDown::update()` calls `ctx.openPopup(this, computePopupRect(ctx))` **every frame**
its popup stays open, not just once at open time — see "Popup position when the world
moves under it" below for why. `Widget` gained a matching `virtual void drawPopup(DrawList&,
const GuiContext&) const {}` (default no-op); `GuiContext::endFrame()` calls
`popupOwner()->drawPopup(overlayList, ctx)` after every panel has drawn, wrapped in
`pushClipRect(popupRect(), /*intersect=*/false)` — REPLACE, not intersect, so the popup
escapes whatever clip its owning panel left pushed. `DropDown::drawPopup()` is the only
override.

**Hit-test priority.** `GuiContext::update()` computes `popupHit = popupOwner() &&
popupRect().contains(mouse)` before the normal per-panel scan, and skips that scan
entirely when true — the popup wins over every panel, including one that overlaps it and
sits in front of the popup's own owner in z-order (this is the scenario the test suite
weights most heavily). The owning control's OWN bounds are explicitly exempted from the
"any press outside the popup closes it" rule — otherwise a second click meant to close
the popup via the control would close it via this generic path and then the control's own
press-driven open/close toggle would immediately reopen it, net leaving it open.

**Escape ordering.** Unlike `TextBox`, `DropDown` needs neither the entering-state
pattern nor a plain `ctx.focusedId() == id()` check for Escape — see
[04-input-and-events.md](04-input-and-events.md) for `TextBox`'s `m_wasFocused`-style
entering-focus-state comparison, which this widget sidesteps entirely. `GuiContext`'s own
Escape default
(`updateFocusNavigation()`) was changed to `if (isPopupOpen()) closePopup(); else
m_focusedId = kInvalidWidgetId;` — i.e. GuiContext closes the popup itself, before
`DropDown::update()` ever runs, and does *not* also clear focus (the control stays
focused, matching every native combo box). `DropDown::update()` then just reads
`ctx.popupOwner() == this` fresh each frame as its sole "am I open" signal; since
GuiContext is the one source of truth for that state and DropDown never needs to tell
"Escape closed it" apart from any other cause, there is nothing left to disambiguate — no
`m_wasFocused` equivalent needed.

**Popup position when the world moves under it: FOLLOW, not close.** Chosen because
`Panel::layout()` already recomputes every widget's bounds unconditionally, every frame,
regardless of whether anything actually moved — so recomputing the popup rect from
`computePopupRect(ctx)` (which reads the control's current `splitRow(ctx)`) and
re-calling `ctx.openPopup(this, rect)` every frame the popup is open is free, not an
added mechanism. Closing on movement, by contrast, would need extra bookkeeping
(remembering the bounds at open time, comparing every frame) purely to detect the case
this doesn't need to detect.

**`popupOwner` must not dangle when the owning panel is destroyed.** `GuiContext` stores
only a `WidgetId`; `popupOwner()` resolves it via `findWidget()` and — since that already
answers `nullptr` for an id nothing claims — self-heals `m_popupOwnerId` back to
"no popup" the moment the owner's panel (and the widget with it) is gone. No explicit
teardown call in `destroyPanel()`/`destroyAllPanels()` was needed.

**Keyboard.** Arrow keys move the highlight and **wrap** at both ends (matching
`RadioGroup`'s arrow-key convention elsewhere in this library), Enter selects the
highlighted item and closes. Enter/Space also **open** a focused-but-closed control —
without this a keyboard-only user who tabs to a `DropDown` has no way to operate it at
all (same baseline `Button`/`Checkbox` already provide).

Popup rules: opens below the control, flips above if it would run off the bottom of the
framebuffer; scrolls if more than 12 items, with a visual (wheel- and
arrow-key-scrolled, not drag-to-scroll) scrollbar using `theme.scrollbarBg`/
`scrollbarGrab`/`scrollbarWidth` — the same tokens `Panel`'s own scrollbar draws with (see
"Panel" below); closes on selection, Escape, or any mouse press outside the popup rect
(control excepted, see above).

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

### Implementation notes

A few decisions worth recording because they're easy to get wrong or aren't obvious from
the summary above:

- **Minimum size axis mapping.** "`titleBarHeight + 2*windowPadding` by `8*fontSize`" is
  read as height-by-width: `titleBarHeight` is a vertical metric, so that half can only
  sensibly be the minimum *height*; `8*fontSize` is the minimum *width*.
- **The scrollbar-presence ordering trap — whether the scrollbar is needed depends on
  content height, which depends on content width, which depends on whether the scrollbar
  is needed — is resolved by reading `needsScrollbar()` from the PREVIOUS `layout()`
  pass**, not deciding it fresh mid-pass. `GuiContext::endFrame()` already runs
  `layout()` twice a frame for wrapping-`Label` convergence (see `Label` above); the same
  double pass corrects a scrollbar-presence flip within the same frame it happens, for
  the same reason: for every widget whose `preferredSize()` doesn't itself depend on row
  width (everything except a wrapping `Label`), content height doesn't depend on
  `contentW` at all, so the two passes can't disagree twice. The flag is stable
  frame-to-frame otherwise — it does not oscillate.
- **`Widget::wantsWheel()`** (virtual, default `false`, overridden `true` on
  `SliderT`/`DropDown`) is what lets Panel tell "the hovered widget will consume this
  wheel tick itself" apart from "nothing hovered will, so scroll the panel instead" — the
  mechanism behind the Slider entry's "consume the wheel so the panel doesn't scroll"
  above.
- **Resize grip / scrollbar grab capture is real `GuiContext` capture**, not a
  Panel-private bool the way title-bar dragging is. Each gets its own `WidgetId` from the
  same counter real widgets draw from (`allocateWidgetId()`, `Widget.h`) even though
  neither is a `Widget` — `GuiContext::findWidget()` naturally resolves that id to
  `nullptr` (nothing claims it), which is what lets `activeId` be shared by both kinds of
  capture without collision. This is what makes `wantsMouse()` stay true, and a
  scrollbar-thumb drag keep tracking, once the cursor leaves the panel's own bounds —
  title-bar dragging doesn't need the same treatment, because the panel's bounds move
  WITH the cursor during that drag, so the cursor never actually leaves them.
- **Resize grip hit-testing uses the full `resizeGripSize` bounding box**, not the
  triangle's exact diagonal half — easier to grab, and only the *drawing* needs to be a
  triangle.
- **A non-scrollable (or non-overflowing) panel passes the wheel through to the
  camera** rather than swallowing it — see [04-input-and-events.md](04-input-and-events.md),
  "Scroll wheel" for the full reasoning and the resulting `GuiContext::wantsScroll()` /
  `VkApp::uiWantsScroll()` split from `wantsMouse()`.
- **Tooltips are suppressed while `activeId` is set** (any drag, including a panel's own
  resize/scrollbar drag) **or a popup is open** — showing one on top of an in-progress
  drag or an open popup would be visual noise for state the user is already mid-gesture
  on.
