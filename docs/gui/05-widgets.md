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
    void setTabular(bool);               // pad digit advances to the widest digit's --
                                          // for a Label used as a numeric readout
                                          // (docs/gui/03-text-and-fonts.md, "Tabular
                                          // figures"). Off by default.
    void setHeading(bool);               // draw with GuiContext's heading face/size
                                          // instead of the primary font
                                          // (docs/gui/03-text-and-fonts.md, "Headings").
                                          // Falls back to the primary font if the
                                          // GuiContext has none configured. Off by
                                          // default.
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

The greedy word-wrap itself lives in `src/ui/TextWrap.h`'s `wrapText()`, not as a private
method on this class — extracted once `LogView` (below) became a second caller needing
the identical algorithm. Internal-only, like `Utf8.h`'s helpers; not part of the public
GUI API.

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

Like `DragValue`, the value text drawn over the track (when `setShowValue(true)`, the
default) always uses `TextFlags::Tabular` (docs/gui/03-text-and-fonts.md, "Tabular
figures") — same reasoning: it is a live-updating digit string, so its columns must not
jitter as the value changes during a drag. The row label does not get the flag.

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

The formatted value always draws with `TextFlags::Tabular` (docs/gui/03-text-and-fonts.md,
"Tabular figures") — it is a live-updating digit string by construction, so its digit
columns must not jitter horizontally as the value changes. The row label (prose, not
digits) does not get the flag.

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

## ListBox

```cpp
class ListBox : public Widget {
public:
    ListBox(std::string label, std::vector<std::string> items, int initialIndex = -1);
    int  selectedIndex() const;
    std::string_view selectedText() const;
    void setItems(std::vector<std::string>);
    void setSelectedIndex(int, bool fireCallback = false);   // < 0 clears the selection
    void setOnChange(std::function<void(int)>);
    void setVisibleRows(int);   // default 6
};
```

What `DropDown`'s popup draws — the row list, the wheel/keyboard-only scrollbar, the
hover-vs-selected highlight — never collapsed behind a control row. Same shape question
as `Vec3Field` vs. a raw triple of `DragValue`s: when the selection itself is the point
(a timestep, a dataset, a light), rather than a setting tucked one click away, an
always-visible list is the right widget, not a combo box someone has to open first.

**Selection is immediate, not preview-then-commit.** `DropDown`'s popup separates
`m_highlightIndex` (what arrow keys move, previewed) from `m_selectedIndex` (what Enter
commits) because the popup needs something to show while it's still deciding whether to
close. `ListBox` never closes, so there's nothing to commit against — Up/Down changes
`selectedIndex` directly and fires immediately, the same convention `RadioGroup`'s
arrow-key handling already uses. Wrapping at both ends matches `DropDown`'s highlight
and `RadioGroup`'s selection too. From "nothing selected" (`initialIndex < 0`), either
arrow direction lands on the first item — simpler than picking a direction-dependent
wrap target for a base that doesn't have a well-defined predecessor/successor yet.

**`initialIndex < 0` is a real, distinct state**, unlike `DropDown`, whose control face
must always display something and so is never indexless. "No dataset chosen yet" is a
legitimate thing for a consumer to represent; `setSelectedIndex()` accepts a negative
index to clear back to it at any time, not just at construction.

**Scrolling** reuses `DropDown`'s popup formula exactly: `itemAtY()`'s row math, the
wheel scrolling 3 rows per tick (`Panel`'s own scrollbar convention, restated in row
units), and a visual-only scrollbar (no drag) using the same `theme.scrollbarBg`/
`scrollbarGrab`/`scrollbarWidth` tokens. The one addition is auto-scroll: Up/Down calls
`scrollToShowSelected()` after moving the selection, so keyboard navigation never leaves
the current pick scrolled out of view — the minimum scroll needed to bring it back onto
the top or bottom row of the visible window, not necessarily centred.

**Programmatic `setSelectedIndex()` does NOT auto-scroll**, matching
`DropDown::setSelectedIndex()`'s identical silence about its own popup's scroll
position. Auto-scroll is specifically an interactive-navigation courtesy, not a general
"keep the selection visible" invariant — a consumer setting the selection from
application code is assumed to know what it's doing to its own scroll position, the same
assumption `DropDown` already makes.

## ContextMenu

```cpp
class ContextMenu : public Widget {
public:
    void addItem(std::string label, std::function<void()> onSelect);
    void addSeparator();
    void open(Vec2 screenPos);
    bool isOpen(const GuiContext&) const;
};
```

A right-click popup with no control row of its own — nothing about it is on screen at
all until `open()` is called. Reuses `GuiContext::openPopup()`/`drawPopup()` exactly as
`DropDown`'s popup already works (see "DropDown" above); the only thing genuinely new is
that the "owner" widget has zero visible presence outside of when its own popup happens
to be open (`preferredSize()` returns `{0,0}`, `draw()` does nothing).

**`GuiContext` has no built-in right-click gesture**, and this class doesn't give it
one. `MouseButton::Right` isn't read anywhere in the capture/focus resolution that drives
every other widget in this library — only `Left` is. Detecting "was there a right-click,
and where" is left entirely to the caller, via the already-public
`ctx.input().mouseReleased[MouseButton::Right]`:

```cpp
// From application code, anywhere (a right-click over bare 3D scene works the same way):
if (gui.input().mouseReleased[static_cast<int>(lvgui::MouseButton::Right)]) {
    menu->open(gui.input().mousePos);
}

// Or from inside another widget's own update(), scoped to that widget's own hitTest():
if (hitTest(ctx.input().mousePos) && ctx.input().mouseReleased[MouseButton::Right]) {
    m_contextMenu.open(ctx.input().mousePos);
}
```

Both calls are the exact same `open(Vec2)`; nothing distinguishes "global" from
"per-widget" context menus beyond where the caller happens to check for the click.

**Clamped to stay fully on screen in both axes**, unlike `DropDown`'s popup, which only
ever needs to flip vertically — its horizontal position is anchored to a control that is
already guaranteed on-screen. A context menu opens at an arbitrary click point that could
be anywhere, including a screen corner, so both axes need the same treatment `DropDown`
only needed for one.

**Mouse-only in this version.** No keyboard navigation (no highlight-then-Enter the way
`DropDown`'s popup has one) — right-clicking is inherently a mouse gesture, and a menu
this small doesn't obviously need an alternate keyboard path yet. `Escape` still closes
it, for free, via `GuiContext`'s own generic "if a popup is open, close it" default — the
same one every other popup in this library already gets without asking for it.

**Clicking anywhere inside the menu closes it**, even a separator row (not selectable,
but still a "you clicked inside, so we're done here" gesture) — ordinary context-menu
behaviour, not a bug. Item selection fires `onSelect` once, on release-inside, the same
convention every clickable surface in this library uses; a press that starts inside the
menu and drags outside before releasing cancels without selecting, matching `Button`.

## LogView

```cpp
class LogView : public Widget {
public:
    explicit LogView(std::string label);
    void push(std::string line);
    void clear();
    void setHeight(float px);           // default 160 (roughly 8 rows)
    void setMaxLines(std::size_t n);    // ring-buffer cap, 0 = unbounded. Default 500.
    void setWordWrap(bool);             // default true
    std::size_t lineCount() const;
    bool isFollowingBottom() const;
};
```

A read-only, appendable, word-wrapping, internally scrollable text region for solver
output — what `Label`'s word-wrap and `Panel`'s own draggable scrollbar already do,
combined into one widget that owns its own scroll state instead of depending on an
owning `Panel`'s. `Label`'s wrap algorithm itself is shared, not duplicated: it moved to
an internal `wrapText()` free function (`src/ui/TextWrap.h`, alongside `Utf8.h`'s own
internal-only helpers) once this became a second caller needing the identical
greedy-break-at-the-last-space logic.

**Auto-follow, the way a terminal or a chat log behaves.** New content pushed via
`push()` keeps the view pinned to the newest line, for as long as nothing has scrolled it
away. The moment a user scrolls up (wheel, dragging the scrollbar thumb, `PageUp`,
`Home`) `isFollowingBottom()` goes false and stays false — new lines keep arriving in the
background without yanking their view out from under them — until they explicitly
return to the bottom (`End`, or scrolling/dragging all the way back down themselves),
at which point it resumes automatically. `isFollowingBottom()` is public specifically so
a consumer can draw their own "new output ↓" affordance when it's false; `LogView` itself
draws no such indicator.

**`setMaxLines()`'s cap is enforced lazily, not on every `push()`.** Eviction of the
oldest lines only ever happens while `isFollowingBottom()` — i.e. only when nothing is
currently depending on the content about to be evicted staying put. While scrolled away
reading history, `push()` keeps appending past the cap rather than trimming the very
content the user is looking at out from under them; `lineCount()` can legitimately exceed
`setMaxLines()`'s value during that window. Memory is therefore not strictly bounded
while scrolled away — it snaps back to the cap the moment the view returns to the bottom.
This was a deliberate choice over the alternative (evict immediately and shift the
scroll position to compensate): the alternative needs `Font` access to know exactly how
many *visual* (wrapped) lines the evicted *raw* line represented, which `push()` doesn't
have — application code can call it from anywhere, not only from inside a frame — so
deferring eviction to whenever the widget is next both updated *and* not being actively
read sidesteps the problem entirely rather than approximating it.

**Scrollbar dragging** works, unlike `DropDown`'s and `ListBox`'s popup-style scrollbars,
which are deliberately visual-only. The mechanism is `ColorEdit`'s single-`WidgetId`-plus-
internal-drag-target pattern (see "ColorEdit", above), not `Panel`'s synthetic-second-id
one: `LogView` has no child widgets whose own hit-testing the scrollbar's pixel region
could ever compete with, so its own `bounds()` already resolves the whole widget —
scrollbar included — through the ordinary per-panel hit-test walk, and an internal
`DragTarget` enum (mirroring `ColorEdit`'s own) is all that's needed to tell "dragging the
thumb" apart from "nothing currently held."

**The wrap-width-vs-scrollbar-gutter self-reference is sidestepped, not solved.**
Whether a scrollbar is needed depends on total wrapped-line count, which depends on wrap
width, which would otherwise depend on whether a scrollbar is needed — the same shape as
`Panel`'s own scrollbar ordering trap (see "Panel", below), but without a `layout()`
override for a cross-pass cache to live in (`LogView` is a leaf widget; nothing about its
own internal scrollbar ever round-trips back through a `Panel::layout()` decision the way
a wrapping `Label`'s height does). Rather than replicate `Panel`'s cross-pass-cache
solution for a leaf widget that has no natural second pass to read from, wrap width
unconditionally reserves the scrollbar gutter's width whether or not a scrollbar ends up
being drawn — a few pixels of unused width in the common case costs less than solving the
self-reference properly would be worth here.

**Keyboard**: `Home` jumps to the oldest line (and stops following); `End` jumps to the
newest (and resumes following); `PageUp`/`PageDown` move by `visibleRows - 1`, clamped —
none of this needs the highlight-vs-selection split `DropDown`'s popup has, since
`LogView` has nothing to select, only somewhere to be scrolled to.

## ColorEdit

```cpp
class ColorEdit : public Widget {
public:
    Color value() const;
    void setValue(Color, bool fireCallback = false);
    void bind(Color* target);
    void setOnChange(std::function<void(Color)>);
protected:
    ColorEdit(std::string label, Color initial, bool hasAlpha);
};

class ColorEdit3 : public ColorEdit {   // RGB
public:
    ColorEdit3(std::string label, Color initial);
};

class ColorEdit4 : public ColorEdit {   // RGBA
public:
    ColorEdit4(std::string label, Color initial);
};
```

A swatch button in the control row opens a saturation/value square + hue strip popup
(`ColorEdit4` adds a third, alpha strip) using the same `GuiContext::openPopup()`
machinery `DropDown` established — same follow-the-owner-every-frame mechanism, same
`drawPopup()` override point, same generic Escape-closes-popup default. Like `DropDown`,
`ColorEdit` does not derive from `CompositeWidget`: the picker controls exist only inside
the popup, not as always-visible panel-space children, so there is no inline child tree
to manage.

`ColorEdit3`/`ColorEdit4` are thin subclasses that fix the base class's `hasAlpha`
constructor argument, rather than a template — the two differ only in whether the alpha
strip exists, which doesn't warrant `SliderT<T>`-style templating on a value type.

**Layout.** `theme.colorSquareSize` (default 140px) and `theme.colorStripWidth` (16px)
size the square and both strips. The hue strip sits to the square's right, same height;
the alpha strip (when present) sits below both, spanning their combined width. The popup
opens below the control and flips above if it would run off the bottom of the
framebuffer — identical rule to `DropDown`'s popup.

**Interaction.** Dragging anywhere in the square sets saturation (horizontal) and value
(vertical, full brightness at the top); dragging the hue strip sets hue (top = 0°, bottom
= 360°, wrapping to the same red); dragging the alpha strip sets alpha (left =
transparent, right = opaque). Every drag clamps to its own region rather than stopping if
the cursor leaves it mid-gesture — the same "capture spans the whole press, not just
while hovering the target" contract `SliderT`'s track drag and `Panel`'s resize
grip/scrollbar grab already use. Unlike `DropDown`, the popup does not close on release —
color pickers conventionally stay open until the control is clicked again or
`GuiContext`'s generic outside-click rule closes it, since a picking session is usually
several drags across the square, the strip, and back.

**The HSV cache, and why hue survives a trip through grey.** `Color` (RGB, the public
value type) has no saturation/hue of its own to drag, so `ColorEdit` keeps a parallel
`m_hue`/`m_sat`/`m_val` cache that every square/strip drag reads and writes directly, only
converting back to RGB (`Color::fromHSV`) to `commit()`. The cache is never blindly
re-derived from `m_value` every frame — `Color::toHSV` canonically returns hue 0 for any
grey (`s == 0`) or black (`v == 0`) pixel, since hue is genuinely undefined there, and a
naive "re-derive HSV from RGB on every frame" would therefore silently discard whatever
hue was selected the moment saturation or value hit zero: drag hue to blue, drag
saturation to 0 (white), drag saturation back up, and the hue slider would have already
snapped to red without the user touching it. The fix is `pullBoundValue()`: it only
resyncs the cache from a bound pointer's value when that value no longer matches what the
cache already predicts (`Color::fromHSV(m_hue, m_sat, m_val, ...) != *m_bindTarget`) —
which is false for every commit `ColorEdit` made itself (it wrote the bound pointer to
exactly that prediction), so the cache survives its own round trip through the bound
pointer untouched. An external, out-of-band write to the bound value (from application
code, not from the picker) *does* trigger a resync, same as `setValue()` always does —
this is specifically about not treating your own writes as external ones.

**Alpha preview without a checkerboard.** The alpha strip and the swatch button both
render translucency by blending the current colour against whatever the popup/panel
already painted underneath (`addRectFilledMultiColor` interpolating alpha linearly across
the strip, from the widget's current RGB at `a=0` to the same RGB at `a=255`), rather than
against a tiled checkerboard. This is a deliberate scope cut, not an oversight — enough
feedback for "getting less opaque" without the extra tile-drawing machinery a true
checkerboard needs, tracked as a possible follow-up in [ROADMAP.md](ROADMAP.md) if it
turns out to matter in practice.

**Not yet included** (see [ROADMAP.md](ROADMAP.md)): numeric RGB or hex text entry for
exact values. The square/strip drag is precise enough for visual tuning but not for
typing in a known hex code.

## Image

```cpp
class Image : public Widget {
public:
    Image(std::string label, TextureId textureId, Vec2 size);
    void setTextureId(TextureId);
    TextureId textureId() const;
    void setSize(Vec2);
    Vec2 size() const;
    void setUVRect(Vec2 uv0, Vec2 uv1);   // defaults to the whole image, [0,0]-[1,1]
    void setTint(Color);                   // defaults to opaque white -- no tint
};
```

Draws a texture registered with `VkApp::registerUiTexture()` (below) at exactly `size`,
left-aligned in the control column — unlike almost every other widget here, it does NOT
stretch to fill the row. A colormap legend or an icon has a meaningful aspect ratio;
stretching it to whatever width the panel happens to be would distort it. Non-interactive
(`acceptsCapture()` is false): there is nothing to press or drag.

**Registering a texture.**

```cpp
ui::TextureId id = app.registerUiTexture(rgbaPixels, width, height);   // RGBA8, row-major
app.unregisterUiTexture(id);   // when you're done with it
```

Lives on `VkApp`, not `GuiContext` — `GuiContext.h` must never see a Vulkan header
(docs/gui/01-architecture.md's layering rule), and uploading a texture is unavoidably a
Vulkan operation. `registerTexture()` is synchronous, like `Font`'s own bake: safe to
call at any point between frames, not mid-`record()`. `unregisterTexture()` waits for the
device to go idle before destroying anything — a descriptor set pointing at the texture
may still be referenced by a command buffer a previous frame already submitted, the same
hazard `rebuildAtlas()` already has to account for on every DPI change, solved the same
way.

**Deliberately scoped to CPU-uploaded pixel buffers, not arbitrary render targets.**
Colormap legends and icons — decoded once into an RGBA buffer — are fully served by this.
Sampling an *existing* scene render target (a thumbnail of the 3D view, a volume slice
still living on the GPU) would mean synchronizing against the scene's own render pass and
image layout transitions, which is a meaningfully bigger and riskier feature than
uploading a static buffer once. Tracked as a follow-up in [ROADMAP.md](ROADMAP.md), not
attempted here.

**How this fits the draw list.** `DrawList::addImage(textureId, rect, uv0, uv1, tint)` is
the only primitive that can start a new `DrawCmd` for a reason other than a clip-rect
change (docs/gui/02-rendering.md) — each texture needs its own descriptor set bound at
record time. `UiRenderer` reuses the font atlas's own sampler (`CLAMP_TO_EDGE` + `LINEAR`)
for every registered texture rather than creating one per texture; filtering and
addressing aren't format-specific, and that sampler already does exactly what a
legend/icon/thumbnail wants.

Its descriptor pool is sized once, at `init()`, for the atlas plus up to
`UiRenderer::kMaxRegisteredTextures` (64) additional textures — a fixed cap chosen for
the expected use case (a modest number of static images registered near startup), not a
dynamic, high-churn texture stream. `registerTexture()` throws once that cap is reached.

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

This is how a panel with forty parameters stays usable. (`findDescendant()`/
`collectFocusable()` are not overridden, so a closed section's child can still be
reached by id or land in Tab-key focus order despite being invisible and unclickable --
a known, accepted gap; see `TabBar`'s own note on the identical limitation, below.)

## TabBar

```cpp
class TabBar : public Widget {
public:
    class Tab {
    public:
        template <typename W, typename... Args> W* add(Args&&...);
    };

    Tab addTab(std::string title);
    void setActiveTab(int index);
    int  activeTab() const;
    int  tabCount() const;
};
```

The other way a panel with too many parameters stays usable — grouping instead of
collapsing. A row of tab buttons across the top; below it, only the ACTIVE tab's
children exist as far as `update()`, `draw()`, `hitTestDeep()`, and `layout()` are
concerned. An inactive tab's children are never positioned, never see this frame's
input, and are not hit-testable — the same "closed means nothing happens to the
children" contract `CollapsingSection` already established for its closed state,
generalized from all-or-nothing to per-child (`m_childTabIndex`, a parallel array to
`CompositeWidget::m_children`, records which tab each one belongs to).

```cpp
auto general = tabBar->addTab("General");
general.add<Checkbox>("Enable X", true);
```

`addTab()` returns a `Tab` — a small, copyable handle, not a widget itself — whose own
`add<W>()` mirrors `Panel::add<W>()`/`CompositeWidget::add<W>()`'s shape so populating a
tab reads like populating anywhere else in this library. Tabs cannot be removed once
added; add every tab you need up front.

Click a tab button to switch (evenly divided across the header width — no per-tab
custom widths in this version); Left/Right switch tabs while the bar is focused,
wrapping at both ends, the same convention `DropDown`'s highlight and `RadioGroup`'s
selection already use. The active tab gets a thin accent underline rather than relying
on background-colour contrast alone, so it still reads clearly under the high-contrast
theme.

**A known, accepted gap, not a new one.** Like `CollapsingSection`, `TabBar` does not
override `findDescendant()`/`collectFocusable()` — an inactive tab's child can still be
reached by id (`ctx.findWidget()`) or land in Tab-key focus order, even though it is
never drawn or hit-testable by mouse. Fixing this for both widgets at once is a
reasonable future cleanup, not attempted here because it isn't a regression either
widget introduces on its own.

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
    Modal       = 1 << 7,
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
    void setOnClose(std::function<void()>);
    void requestClose();             // fires setOnClose() if Closable is set; else a no-op
    void bringToFront();

    // Anchoring so panels survive window resize sensibly
    enum class Anchor { TopLeft, TopRight, BottomLeft, BottomRight };
    void setAnchor(Anchor);

    // Layout persistence -- opt-in, see "Layout persistence" below.
    void setPersistenceId(std::string);
    const std::string& persistenceId() const;
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

**Title-bar buttons.** `PanelFlags::Collapsible` puts a disclosure triangle at the title
bar's leading edge (right-pointing when collapsed, down-pointing when open — the same
convention `CollapsingSection` uses); `PanelFlags::Closable` puts an X at its trailing
edge. Each occupies a `titleBarHeight` square, and the title text takes whatever they
leave between them. Both fire on release-*inside*, like `Button`, so pressing one and
dragging away cancels.

Collapsing changes what the panel *occupies*, not its stored rect: `bounds()` keeps the
expanded size so expanding restores it exactly, while drawing, hit-testing, and occlusion
all go through the title-bar-only `effectiveBounds()`. The collapse arrow stays hit-
testable while collapsed — it is the only way back out.

Closing sets `visible(false)` and then fires `setOnClose()`, in that order, so a handler
can veto by setting visibility straight back. It must not call
`GuiContext::destroyPanel()` directly: the callback runs from inside `GuiContext`'s own
walk over the panel list, and `destroyPanel()` erases the owning `unique_ptr` immediately.
Defer the destroy through `postToMainThread()`, which drains at the top of the next
`beginFrame()`.

**Layout persistence.** Opt-in: a panel with no `persistenceId()` set (the default,
empty string) is invisible to both halves of the mechanism below. Persistence is
deliberately keyed off an explicit id rather than the display `title` — titles are
neither stable (a consumer can retitle a panel freely) nor unique (nothing stops two
panels sharing one), so an id that means "this is the same panel across runs" has to be
something the consumer commits to on purpose.

```cpp
class GuiContext {
public:
    std::string saveLayout() const;
    void        loadLayout(std::string_view);
};
```

`saveLayout()` walks every current panel with a non-empty `persistenceId()` and emits one
`[id]` section per panel, each holding `x`/`y`/`w`/`h`/`collapsed`/`scrollY` as plain
`key=value` lines — a small hand-rolled text format, not JSON or INI through a library,
matching [00-overview.md](00-overview.md)'s "zero external GUI dependency" goal (there is
nothing here complex enough to need one). Anchor is deliberately not persisted: unlike
position/size/collapse/scroll, which change through user interaction the save is meant to
survive, anchor is a structural property the consumer sets once at construction via
`setAnchor()`, not something a user's drag or resize ever touches.

`loadLayout(data)` applies each section to whichever CURRENTLY EXISTING panel has a
matching `persistenceId()` — a panel created after the call, or an id with no current
match, is simply not touched. Malformed lines are skipped rather than thrown: this is a
best-effort restore of a previous session, governed by [07-public-api.md](07-public-api.md)'s
"per-frame operations never throw" rule, not "construction failures throw" (it can run at
any point after panels exist, not only at startup).

**The scrollY ordering trap.** `loadLayout()` may run before the panel it's restoring has
ever had a `layout()` pass — nothing requires a consumer to `beginFrame()`/`endFrame()`
between creating panels and loading a saved layout. At that point `m_contentHeight` is
still its construction-time `0.0f`, so `maxScrollY()` would answer `0` — clamping a
restored `scrollY` against it immediately would silently discard whatever was saved back
to `0`. `Panel::setScrollY()` (the internal setter `loadLayout()` uses) deliberately does
NOT clamp; it takes the value as-is and relies on `Panel::layout()`'s own
`m_scrollY = std::clamp(...)`, which already runs unconditionally at the end of every
pass, to settle it to a valid value the moment real content height is known — the exact
same self-correcting trick `m_needsScrollbar`'s own ordering trap (below) already relies
on, reused rather than re-solved.

**Modal panels.** `PanelFlags::Modal` is an ordinary panel flag, combinable freely with
every other one (`Movable`, `Resizable`, `Closable`, ...) — a modal dialog is still just
a `Panel`, with the same widgets, the same layout, the same title bar. What the flag
changes lives entirely in `GuiContext`, not `Panel` itself:

- **Only the frontmost visible `Modal` panel is interactive.** `GuiContext::
  activeModalPanel()` walks `m_panels` (already stored front-to-back) and returns the
  first `Modal` match. While it's non-null, the hover/hit-test walk in
  `GuiContext::update()` and the Tab-focus collection in `updateFocusNavigation()` both
  skip every OTHER panel outright — not "let the click through and have it do nothing",
  genuinely never resolved, so a background panel's widgets can't be reached by mouse
  or by keyboard while a modal is open. A popup belonging to a widget INSIDE the modal
  is unaffected (popup hit-testing runs before this and doesn't consult it).
- **`wantsMouse()`/`wantsKeyboard()`/`wantsScroll()` all report true unconditionally**
  while a modal is open, regardless of cursor position or focus state. This is the part
  a per-panel hover check alone can't give you: the camera hand-off has to be swallowed
  even when the cursor is nowhere near the dialog, not just when it's hovering the
  dialog's own rect — otherwise a click on the 3D scene behind a "Discard changes?"
  prompt would still reach the camera.
- **The backdrop and the modal panel itself draw through the overlay list** — the same
  mechanism `DropDown`'s popup already uses to escape its owner's clip rect and z-order
  (see "DropDown", above) — rather than in the modal's own raw position in the normal
  per-panel draw pass. A modal created early and shown later via `setVisible(true)`,
  rather than freshly `createPanel()`'d, keeps whatever z-order slot it already had;
  without this it could end up drawn BEHIND a panel raised afterward even though it's
  the only thing actually receiving input, which would look broken. `theme.modalBackdrop`
  (a translucent black, independent of the active theme) fills the screen behind it.
- **Nested modals resolve without an explicit stack.** If a second `Modal` panel is
  opened from inside a first one (a confirmation from within a confirmation),
  `createPanel()` inserts it frontmost as usual, so `activeModalPanel()` simply starts
  returning the newer one — which now blocks the older modal too, the same way a native
  nested dialog blocks its parent, for free.
- **`Escape` closes a `Closable` modal**, via the same `Panel::requestClose()` the
  title-bar X button calls (extracted into its own method specifically so both call
  sites stay identical) — checked in `updateFocusNavigation()` with priority just below
  "close an open popup" and just above the generic "clear focus" default. A modal
  without `Closable` is left alone: that flag is the panel's own opt-in signal that it
  may be dismissed this way at all, so a modal that deliberately omits it (forcing an
  explicit in-dialog choice, e.g. "Save" vs. "Discard" with no implicit cancel) can't be
  bypassed via Escape either.
- **Passive, time-driven widget state is NOT frozen** by a modal — a background
  `ProgressBar`'s indeterminate sweep keeps animating, a background `LogView` keeps
  accepting `push()`ed lines. Only INTERACTION is blocked (nothing can claim
  `activeId`/`hoveredId`/`focusedId` outside the modal), which is a narrower thing than
  "pause everything behind the dialog" — matching how a real application's background
  work keeps running while a confirmation prompt is up.

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

## MenuBar

A single, global row of dropdown menus fixed to the window's top edge — File / Edit /
View / Help, in the traditional desktop-app sense (VS Code's own menu bar is exactly the
shape this targets). **Not the same thing as `TabBar`**, which groups content *within*
one panel; this is one bar, owned by `GuiContext` itself, independent of which panels
exist or are visible.

```cpp
class MenuBar {
public:
    class Menu {
    public:
        void addItem(std::string label, std::function<void()> onSelect,
                     std::string shortcutHint = "");
        void addSeparator();
    };

    Menu addMenu(std::string title);
    std::size_t menuCount() const;
    float height(const GuiContext&) const;   // 0 if menuCount() == 0

    // Geometry queries -- useful for a consumer's own hit-testing/rendering decisions.
    Rect barRect(const GuiContext&) const;
    Rect menuTitleRect(const GuiContext&, std::size_t menuIndex) const;
    Rect openMenuRect(const GuiContext&) const;   // empty Rect if openIndex() < 0
    int  openIndex() const;                       // -1 if nothing is open
};
```

```cpp
auto file = gui.menuBar().addMenu("File");
file.addItem("New Scene", [] { ... });
file.addItem("Open...", [] { ... }, "Ctrl+O");
file.addSeparator();
file.addItem("Exit", [] { ... }, "Ctrl+Q");
```

Starts empty (`menuCount() == 0`) and stays entirely inert — zero draw calls, zero
interaction cost — until `addMenu()` is called at least once; a consumer who never
touches `gui.menuBar()` sees no change to their existing layout or behaviour at all.

**`shortcutHint` is display-only.** "Ctrl+O" draws right-aligned in the row, dimmed
(`theme.textDisabled`) — it does not read or dispatch the actual key combination. Wiring
a real accelerator is the consumer's own job, the same way it would be for any other
keyboard shortcut in this library: read `ctx.input()`'s key queue, or call
`injectKey()`.

**Layout is the consumer's job.** `height(ctx)` reports how tall the bar currently is
(`0` when empty); nothing here shifts panel positions to make room for it automatically.
Position your own panels' starting `y` at or below that height if you don't want them to
render underneath the bar — `gui_demo.cpp` does this by construction (every panel starts
comfortably below the default 24px bar height) rather than querying it, since a menu bar
is normally added once, near the top of `main()`, before any panel geometry is decided.

### Deliberately not a `Widget`

Every `Widget` belongs to exactly one `Panel` — `Panel::add()`/`CompositeWidget::add()`
are the only things that ever set a widget's owning panel/parent, and `GuiContext`
resolves ids, hit-testing, and the `DropDown`-style popup mechanism entirely through that
per-panel tree (`findWidget()` walks `m_panels`; the popup is keyed off a `Widget* owner`
passed to `openPopup()`). A menu bar is not scoped to any one panel — it must sit above
every panel, independent of which ones exist — so it is a second top-level kind of thing
`GuiContext` owns directly and drives with hard-coded calls throughout `update()`/
`endFrame()`/`wantsMouse()`, the exact same treatment `Panel` itself already gets (`Panel`
isn't a `Widget` either — see this document's own base-class section). Its interaction is
resolved with a small, self-contained hit-test/open-close state machine reading
`ctx.input()` directly, never through `WidgetId`/`activeId` capture at all.

### Interaction

**Click a title to open its menu; click the same title again to close it** (the same
"closes on a second click of the control" convention `DropDown`'s own control uses).
Clicking a *different* title always opens/switches to it — never a toggle for that one.

**Hover-switch between open menus.** Once a menu is open, moving the cursor onto a
sibling title — no click needed — switches the dropdown to that menu. This is what makes
sweeping the mouse across "File Edit View" feel like a native menu bar instead of
requiring a click per menu.

**Click anywhere outside the bar and the open dropdown closes it**, same as a widget
popup dismissing on an outside press (`GuiContext::update()`). `Escape` closes it too,
checked ahead of even the ordinary popup-close priority in `updateFocusNavigation()`
(`MenuBar` isn't a `Widget`, so `isPopupOpen()` can never observe it either way — it
needs its own explicit check).

**The bar (and its open dropdown, if any) claims hit-testing priority over every panel**,
identical in spirit to how an open popup already wins over a panel that happens to
overlap it: `GuiContext::update()` skips the panel hit-test walk entirely while the
cursor is over the bar's row or an open dropdown, so a click on "File" can never *also*
register on whatever panel sits underneath the bar. `wantsMouse()` reflects both cases —
merely hovering the bar (even without a click, matching how hovering an ordinary panel
already claims the mouse) and a menu that stayed open after the cursor moved back off it.

**Mouse-only in this version** — no `Alt`-key mnemonics, no arrow-key navigation within
an open menu, matching `ContextMenu`'s own "mouse-only, a menu this small doesn't
obviously need an alternate keyboard path yet" reasoning. `Escape` still closes it, for
the same reason `ContextMenu`'s does: `GuiContext`'s generic "if something's open, close
it on Escape" default, extended by one more `if` for this one case.

**Flat items and separators only — no nested submenus.** Each top-level menu opens one
flat dropdown, reusing exactly the layout/drawing approach `ContextMenu`'s own popup
already established (row height, `itemSpacing`-tall separators, hover highlight,
clamped-on-screen width). An item that itself needs to open a further cascade is out of
scope for this version.

### Modal panels

While a `PanelFlags::Modal` panel is open, `MenuBar::update()` returns immediately —
non-interactive, exactly matching "modal blocks INTERACTION, not the rest of the app"
(the same idiom `GuiContext::update()` already applies to background panels' own
`update()` calls while a modal is active). It still **draws** every frame regardless, so
the bar stays visually present (drawn even on top of the modal backdrop) rather than
flickering out of existence — there is just nothing it will do in response to a click
while a modal has the floor.
