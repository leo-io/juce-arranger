# Zoomable Main Grid + Playhead — UX/UI Design

## Context

**Task.** Two features for the arrangement grid: (1) a discrete *zoom* model that
re-scales how much of the song each row of the grid represents — from the entire
song in one row down to a single bar per row; and (2) a *playhead* overlay that
shows the live transport position on the grid during playback.

**Components involved (verified against current source):**

- `ArrangementView` (`ArrangementView.cpp:1`) — container. Owns `BarGridModel model`,
  a `juce::Viewport gridViewport` (vertical scroll only), the `BarGridComponent grid`,
  the header band, and the bars-per-row toolbar (`bars4/bars8/bars16`). Runs a 40 Hz
  `juce::Timer` for follow-transport (`ArrangementView.cpp:353`).
- `BarGridComponent` (`BarGridComponent.cpp:1`) — the scrolled content. Paints one
  cell per bar in rows of `model.barsPerRow`, handles mouse/keyboard selection, and
  already renders a *playing-bar* highlight (`BarGridComponent.cpp:194`).
- `BarGridModel` (`BarGridModel.h:30`) — pure data model. `bars` vector, `barsPerRow`,
  `cellBounds()`, `cellWidth()`, `rowCount()`, `requiredHeight()` (`BarGridModel.h:113-140`),
  hit testing, and selection.
- `Theme.h` (`Theme.h:4`) — `Palette` and `Spacing` token namespaces. Single source of
  truth. `Spacing::rowHeight = 28` (`Theme.h:20`).
- `SongAnalysis` (`SongAnalysis.h:13`) — `downbeats`, `segments`, `barNumberAt(time)`
  (`SongAnalysis.h:99`), `segmentAt(time)`.

**Constraints.**

- Dark palette is fixed (`Palette::surface 0xff1e1f22`, `accent 0xff7cb8ff`). New tokens
  must land in `Theme.h`.
- The viewport currently scrolls **vertically only** (`gridViewport.setScrollBarsShown
  (true, false)`, `ArrangementView.cpp:14`) and the grid is laid out to exactly the
  viewport width (`layoutForWidth`, `BarGridComponent.cpp:18`). The zoom model preserves
  this — content always fits the width; zoom changes *vertical* density.
- DPI: all sizes are logical pixels; JUCE applies the display scale. No `int` pixel value
  below is a physical-pixel assumption.

### Key reframing: zoom replaces "bars per row"

The existing 4/8/16 toolbar is a *bars-per-row* control. The requested zoom feature is
a **superset** of it expressed as song-structure granularity. We therefore **retire the
fixed 4/8/16 buttons** and drive the grid from a single `zoomLevel` integer. Bars-per-row
becomes a *derived* quantity computed from the zoom level and the song's section sizes.
This keeps `BarGridModel`'s row/column math intact — only the *source* of `barsPerRow`
changes, plus a new section-aware mode for the two zoomed-out levels.

---

## Information architecture

The shell is unchanged: `MainComponent` hosts the transport controls and the
`ArrangementView`. Inside `ArrangementView` the regions are:

```
+-----------------------------------------------------------------------+
| Header band      (Spacing::header = 24)   song · TS · BPM · legend     |
+-----------------------------------------------------------------------+
| Toolbar          (Spacing::toolbar = 22)  [-] [zoom level chip] [+]    |
|                                            ......... selection readout  |
+-----------------------------------------------------------------------+
| Viewport (vertical scroll)                                            |
|   +---------------------------------------------------------------+   |
|   | BarGridComponent  (rows of cells; playhead overlay on top)    |   |
|   |                                                               |   |
|   +---------------------------------------------------------------+   |
+-----------------------------------------------------------------------+
```

The toolbar's left cluster changes from three radio buttons to a **zoom stepper**:
`[−]  ⟨Zoom: 1 section/row⟩  [+]`. Everything else (selection readout on the right,
header band) is unchanged.

---

## Feature 1 — Zoomable main grid

### What "section per row / 2" means

"One section per row / 2" is read as **fractional section granularity**: at each zoom
step beyond "1 section/row" you cut the slice of song shown per row in half. Level 2 =
half a section per row, level 3 = a quarter, and so on, until the slice reaches **one bar
per row**, which is the maximum zoom. So the progression is: *whole song → one section →
fractions of a section → one bar*. Granularity is expressed in **bars per row**, derived
from the median section length so the fractions land on whole bars.

Let `S` = median section length in bars (e.g. a song of 8-bar sections → `S = 8`).

### Zoom-level table

| Level | Name (toolbar chip)   | What each row shows                         | bars/row (`barsPerRow`)        | Layout mode |
|-------|-----------------------|---------------------------------------------|--------------------------------|-------------|
| 0     | `Song`                | **Entire song in one row**                  | `= total bar count`            | `WholeSong` |
| 1     | `Section`             | **One section per row** (section-aligned)   | per-row = that section's bars  | `Section`   |
| 2     | `½ Section`           | Half a section per row                      | `ceil(S / 2)`                  | `Fixed`     |
| 3     | `¼ Section`           | Quarter of a section per row                | `ceil(S / 4)`                  | `Fixed`     |
| 4     | `⅛ Section`           | Eighth of a section per row                 | `ceil(S / 8)`                  | `Fixed`     |
| …     | …                     | halve each step                             | `ceil(S / 2^(level-1))`        | `Fixed`     |
| N     | `Bar`                 | **One bar per row** (max zoom)              | `1`                            | `Fixed`     |

`N = 1 + ceil(log2(S))` (the first level where `ceil(S / 2^(level-1))` reaches 1).
For `S = 8`: levels 0..4 = Song, Section, 4, 2, 1 bar/row → `N = 4`.

Three layout modes:

- **`WholeSong` (level 0).** One row only. `barsPerRow = bars.size()`. Each bar is a very
  thin column; the row is the full song overview. Section colour bands dominate; bar
  numbers are sparse/absent.
- **`Section` (level 1).** **Section-aligned, ragged rows.** Each row is exactly one
  section, so rows may have *different* bar counts (a 16-bar chorus row is wider per-cell
  than an 8-bar verse row, but both rows fill the viewport width). This is the one mode
  that breaks the uniform `barsPerRow` grid — see model changes below.
- **`Fixed` (levels 2..N).** Uniform `barsPerRow` across all rows, exactly the existing
  `BarGridModel` math. Rows do **not** align to section boundaries; section colour simply
  flows cell-to-cell as today.

> Recommendation: keep level 1 (`Section`) section-aligned because it is the most useful
> "read the arrangement" view and the request explicitly asks for "one section per row".
> The ragged-row cost is contained to a new `rowSpans` table in the model (below).

### How rows vs. columns change across zoom

- **Number of rows changes with zoom**, not the content type of a row. A row is always a
  horizontal strip of bar-cells filling the viewport width; zooming in puts *fewer bars in
  each row* → *more rows* → taller scrollable content.
- **Cell width is always `viewportWidth / barsPerRow`** (existing `cellWidth()`,
  `BarGridModel.h:113`). Zooming in widens each cell (fewer per row), which is what raises
  label density (`labelDetailFor`, `BarGridModel.h:188`).
- **Row height is fixed** at `Spacing::rowHeight` in all modes for predictable scrolling.
  (Optional future: taller rows at max zoom to host waveform miniatures — out of scope.)

### ASCII wireframes

**Level 0 — `Song` (whole song, one row).** 64-bar song, 4 sections.

```
 Viewport width  ───────────────────────────────────────────────────────►
+-------------------------------------------------------------------------+
| Intro |  Verse        |   Chorus        |  Verse        |  Chorus      | | ← single row
+-------------------------------------------------------------------------+
  (no scroll; bar numbers suppressed; thin per-bar ticks; colour bands rule)
```

**Level 1 — `Section` (one section per row, ragged).** Same song.

```
+-------------------------------------------------------------------------+
| 1  2  3  4                                  Intro            (4 bars)    | ← row = Intro
+-------------------------------------------------------------------------+
| 5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20   Verse      (16 bars)   | ← row = Verse
+-------------------------------------------------------------------------+
| 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36  Chorus     (16 bars)   |
+-------------------------------------------------------------------------+
| 37 ...                                            Verse                  |
+-------------------------------------------------------------------------+
   each row fills full width; cell width = width / (bars in THAT section)
```

**Level N − 1 — `Fixed`, 4 bars/row.** (`S=8`, level 2.)

```
+-------------------------------------------------------------------------+
|    1     |     2     |      3      |       4        Intro... Verse       | ← 4 cells / row
+-------------------------------------------------------------------------+
|    5     |     6     |      7      |       8                             |
+-------------------------------------------------------------------------+
|    9     |    10     |     11      |      12        Chorus               |
+-------------------------------------------------------------------------+
   uniform grid; segment label drawn on first cell of each run; vertical scroll
```

**Level N — `Bar` (one bar/row).**

```
+-------------------------------------------------------------------------+
| 1                                              Intro                     |
+-------------------------------------------------------------------------+
| 2                                              Intro                     |
+-------------------------------------------------------------------------+
| 3   ...                                                                  |
+-------------------------------------------------------------------------+
   one tall-readable row per bar; full label + number; long vertical scroll
```

### Resize behaviour

- **Width**: content is always re-laid to the viewport's visible width
  (`layoutForWidth`, called from `ArrangementView::resized()` → `layoutGrid()`,
  `ArrangementView.cpp:286`). Cell widths recompute; **zoom level is independent of window
  width** — it only sets bars-per-row. Resizing never changes the zoom level.
- **Min window**: at very narrow widths in `Section`/`WholeSong` mode, per-cell width can
  fall below ~6 px. Clamp: if `cellWidth < kMinCellPx (6)`, drop one effective zoom step
  for rendering density only (labels → `SparseNumber`), never re-quantise the model.
- **Large window**: more cells become legible (label detail rises automatically via
  `labelDetailFor`). No layout change needed.
- **DPI 1.5×/2×**: all values are logical px; the only scale-sensitive constant is the
  `kMinCellPx` floor and the playhead line width — both expressed in logical px and fine.

### Zoom interaction spec

| Gesture | Action | Anchor |
|---------|--------|--------|
| **`Ctrl` + mouse wheel** over the grid | wheel up = zoom in (+1 level), down = zoom out | **bar under the cursor** stays under the cursor |
| **`+` / `−` toolbar stepper** | ±1 level | **playhead bar** if playing, else top-of-view bar |
| **`Ctrl` + `=` / `Ctrl` + `−`** keyboard | ±1 level | playhead bar if playing, else current selection anchor |
| **`Ctrl` + `0`** | jump to level 0 (`Song`) | n/a |
| Plain mouse wheel (no modifier) | normal vertical scroll (unchanged) | n/a |

**Anchor preservation (the important detail).** Zoom must keep a reference bar visually
stable so the user doesn't lose their place:

1. Before changing level, record the **anchor bar index** (from cursor hit-test
   `barIndexAtClamped`, `BarGridModel.h:167`, or the playhead bar).
2. Apply the new level → model re-derives `barsPerRow`/`rowSpans` → content re-lays out.
3. Compute the anchor bar's new `cellBounds().getY()` and call
   `gridViewport.setViewPosition (0, newY − cursorYWithinViewport)` so the same bar sits
   under the same screen point (`Viewport::setViewPosition`, verified `juce_Viewport.h:105`).

**Wheel handling.** `BarGridComponent` overrides `mouseWheelMove (const MouseEvent&,
const MouseWheelDetails&)` (verified `juce_Component.h:1822`; `MouseWheelDetails::deltaY`
and `isReversed` verified `juce_MouseEvent.h:419,424`). If `event.mods.isCtrlDown()`,
consume the event and emit a zoom request; otherwise `return false` to let the parent
`Viewport` scroll normally. Honour `isReversed` for natural-scroll trackpads.

### LookAndFeel / rendering notes per zoom level

Rendering already keys off `cellWidth` via `labelDetailFor` (`BarGridModel.h:188`) and
`LabelDetail` (`BarGridComponent.cpp:88-136`). Extend that, governed by zoom mode:

| Aspect | `WholeSong` (0) | `Section` (1) | `Fixed` mid | `Bar` (max) |
|--------|-----------------|---------------|-------------|-------------|
| Bar numbers | none | sparse (first of each 4) | per `labelDetailFor` | every bar, prominent |
| Segment label | section name centered per band | section name right-aligned per row | first cell of run | every row |
| Vertical bar-lines (cell outlines) | hairline at section boundaries only (`divider`) | hairline per cell, **bold (2 px) at section starts** | hairline per cell | top/bottom rule only |
| Section header treatment | colour band fill at full sat | row tinted with `colour.withAlpha(0.40)` + bold left edge at section start | existing 0.40 fill | full 0.40 fill |
| Beat ticks | none | none | none (optional) | optional faint beat ticks inside the bar |

Concretely, two new render hints the model exposes per bar/row:

- `bar.isSectionBoundary` (already have `isSegmentStart`, `BarGridModel.h:23`) → draw the
  **2 px section divider** using a new `Palette::gridStrong` token.
- `mode == Section` → draw the section name once per row, right-aligned (mirrors the
  wireframe), instead of per-run.

No `LookAndFeel` virtual override is required for the grid itself — it is fully custom
`paint()`. The **toolbar zoom stepper** uses standard widgets and *will* read a
`LookAndFeel` for its button colours (see Theming). If/when the app introduces a shared
`LookAndFeel_V4` subclass, map the new tokens to `ColourId`s there; until then the stepper
sets colours directly via `setColour (TextButton::buttonColourId, ...)` exactly as the
current 4/8/16 buttons do.

---

## Feature 2 — Playhead in main grid

### Visual spec

A **playhead** is a vertical marker over the cell whose bar is currently playing, plus a
horizontal position *within* that bar's cell (because each row spans multiple bars and
each bar spans real time, we can place the line precisely by interpolating the transport
position inside the playing bar's `[startTime, endTime)`).

```
        play-progress within the bar's cell
                 │
   ┌─────────────▼───────────────┐   row containing the playing bar
   │ 12        ▽                  │   ▽ = downward triangle cap at the row's top edge
   │           ┃  (2 px accent)   │   ┃ = vertical line, full row height
   └───────────┃──────────────────┘
               playheadAccent
```

- **Vertical line**: 2 px wide, full `Spacing::rowHeight`, colour
  `Palette::playhead 0xffff5c5c` (warm red — the conventional DAW playhead colour, and
  distinct from the blue `accent` used for selection so the two never read as the same
  thing). Drawn with `g.fillRect`.
- **Cap**: a small downward triangle (~7 px base) at the top of the line, via
  `juce::Path::addTriangle` (verified `juce_Path.h:324`) filled with the same colour, so
  the head is findable when scanning vertically.
- **X position** = `cellX + cellWidth * (pos − bar.startTime) / (bar.endTime −
  bar.startTime)`, clamped to the cell. This makes the line glide *across* the bar between
  downbeats rather than snapping.
- **Z-order**: painted **last** in `BarGridComponent::paint`, above selection and the
  existing playing-bar tint. Keep the existing soft fill (`accent.withAlpha(0.25)`,
  `BarGridComponent.cpp:201`) as the "this is the live row" band, but recolour or remove
  the bright left-edge bar (`BarGridComponent.cpp:204`) so it doesn't compete with the new
  red line — recommend **removing** the left edge and letting the red playhead be the sole
  precise marker.
- **When stopped**: show a thinner, dimmer *play-cursor* (1 px, `playhead.withAlpha(0.5)`)
  at the seek position so the user sees where Play will start. This reuses the same draw
  path with reduced weight.

### How it moves

Reuse the **existing 40 Hz `juce::Timer`** in `ArrangementView` (`ArrangementView.cpp:54`)
— do **not** drive UI from the audio callback (transport state would be touched off the
message thread). The timer already polls `transportSource.getCurrentPosition()`
(`ArrangementView.cpp:365`). Extend its body to also push the *sub-bar fraction* to the
grid, not just the bar index:

- Today it calls `grid->setPlayingBar(barIndex)` only when the bar changes
  (`ArrangementView.cpp:373`). For smooth motion the line must update *within* a bar, so
  add `grid->setPlayheadPosition(double timeSeconds)` (or pass bar index + fraction). The
  grid repaints only the **two cells** the line can occupy (old + new) via a tight
  `repaint(rect)` to avoid full-surface churn at 40 Hz.
- 40 Hz is smooth enough for a moving line and is already the chosen rate. If we later want
  vsync-perfect motion, `juce::VBlankAttachment` exists (verified
  `juce_VBlankAttachment.h`) — note as a future option, not now.

### Auto-scroll to follow

Keep and refine the current behaviour (`ArrangementView.cpp:377`): only scroll when the
playing cell is **not** intersecting the view area (`getViewArea`, verified
`juce_Viewport.h:149`). Refinements:

- **Gated by `followTransport`** (already wired via `followTransportButton`,
  `MainComponent.h:35`). When off, the playhead still *draws* but the viewport does not
  chase it.
- Scroll so the playing row lands ~⅓ from the top (lookahead) rather than the very top
  edge, so the user sees what's coming. Compute
  `newY = cellBounds.getY() − viewHeight/3`, clamp ≥ 0.
- **Never fight the user**: suppress auto-scroll for ~750 ms after any manual scroll/zoom
  so a deliberate look-around isn't yanked back. Track a `lastUserScrollMs` timestamp.

### Interaction with zoom

The playhead is **defined in song-time and bar-space**, so it is zoom-invariant by
construction — it always lands on the live bar's cell wherever that cell currently is:

- On **zoom change**, recompute the playhead's cell from the bar index + fraction; nothing
  about the playhead state needs to change.
- At **level 0 (`Song`)** the whole song is one row; the playhead becomes a single line
  sweeping left→right across the entire width — effectively a song-progress bar. Very
  useful as an overview; no special-casing needed beyond the existing interpolation.
- At **max zoom (`Bar`)** the line sweeps top bar's width once per bar then jumps to the
  next row — natural.
- Use the **playhead bar as the zoom anchor** when zooming while playing (see zoom anchor
  rules) so the live position stays on screen across a zoom.

---

## Component / class changes (no implementation)

### `BarGridModel.h`

- Add `enum class GridMode { WholeSong, Section, Fixed };` and `GridMode mode`.
- Add `int zoomLevel` and a derivation helper
  `void applyZoom (const SongAnalysis&, int level, double totalLen)` that sets `mode`,
  `barsPerRow`, and (for `Section`) a `std::vector<RowSpan> rowSpans` where
  `RowSpan { int firstBar; int barCount; }`.
- Add `int medianSectionBars (const SongAnalysis&)` and `int maxZoomLevel()` (= the bar
  level) used to clamp the stepper.
- Generalise layout for ragged rows: `cellBounds`, `rowCount`, `barIndexAtExact/Clamped`,
  `requiredHeight` must consult `rowSpans` when `mode == Section` (variable cell width per
  row, variable cells per row). In `Fixed`/`WholeSong` they keep today's uniform math.
- Add `bool isSectionBoundary(int barIndex)` (alias/derive from existing `isSegmentStart`,
  `BarGridModel.h:23`) for the bold divider.
- Extend unit tests (the file already has a `JUCE_UNIT_TESTS` block, `BarGridModel.h:286`):
  cover `applyZoom` level→barsPerRow mapping, ragged `Section` `cellBounds`, and zoom
  clamping at both ends.

### `BarGridComponent.h/.cpp`

- Override `void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&)` — Ctrl+wheel
  → emit zoom request (via a new `std::function<void(int delta, int anchorBar)>
  onZoomRequest` callback the `ArrangementView` sets), else return without consuming.
- Add `void setPlayheadPosition (std::optional<double> timeSeconds)` storing
  `std::optional<double> playheadTime`; replace/augment the bar-only `setPlayingBar`
  (`BarGridComponent.cpp:47`).
- In `paint`, after selection/playing-bar drawing (`BarGridComponent.cpp:194`): draw the
  red playhead line + triangle cap using the interpolation formula; remove the old bright
  left-edge bar.
- `paint` reads `model.mode` to choose label/divider density per the rendering table.
- Add `juce::Rectangle<int> playheadCellRect() const` so the timer can `repaint(rect)`
  tightly instead of repainting the whole component each tick.

### `ArrangementView.h/.cpp`

- Replace `bars4/bars8/bars16` (`ArrangementView.h:62`) and `setBarsPerRow`
  (`ArrangementView.cpp:92`) with a **zoom stepper**: `juce::TextButton zoomOut {"−"},
  zoomIn {"+"}; juce::Label zoomChip;` and `int zoomLevel = 1; void setZoomLevel(int);`
  that calls `model.applyZoom(...)`, updates `zoomChip` text from the level→name table,
  `grid->refresh()`, `layoutGrid()`, repaints, and persists the level.
- Implement `zoomAroundAnchor(int newLevel, int anchorBar, int cursorYInView)` performing
  the anchor-preserving `setViewPosition` described above.
- Wire `grid->onZoomRequest` to `zoomAroundAnchor`.
- In `timerCallback` (`ArrangementView.cpp:353`): call `grid->setPlayheadPosition(pos)`
  every tick (smooth), keep the changed-bar guard only for the *auto-scroll* part, apply
  the ⅓-lookahead scroll and the post-user-scroll suppression window.
- `layoutGrid` (`ArrangementView.cpp:286`) is unchanged in spirit (still
  `grid->layoutForWidth(width)`); the model now produces the right height for the mode.

### `MainComponent.h/.cpp`

- The persistence call that currently stores bars-per-row (driven by `sendChangeMessage`
  from `ArrangementView`, `ArrangementView.cpp:33`) stores `zoomLevel` instead. Property
  key change only; no structural change. `followTransportButton` (`MainComponent.h:35`)
  and the existing change-listener plumbing (`MainComponent.h:60`) are reused as-is.

---

## Theme.h additions

Add to the `Palette` namespace in `Theme.h` (after line 12):

```cpp
const juce::Colour playhead      { 0xffff5c5c }; // transport playhead line/triangle (warm red)
const juce::Colour playheadDim   { 0x80ff5c5c }; // stopped-state seek cursor (50% alpha)
const juce::Colour gridStrong    { 0xff52565c }; // bold section-boundary divider (2 px)
const juce::Colour zoomChipBg     { 0xff2a2c30 }; // zoom stepper chip background (= surfaceElevated)
```

Add to the `Spacing` namespace (after line 23):

```cpp
constexpr int playheadWidth  = 2;  // playhead line thickness (logical px)
constexpr int playheadCap    = 7;  // triangle cap base width
constexpr int minCellPx      = 6;  // cell width floor before density downgrade
constexpr int sectionDivider = 2;  // bold divider weight at section boundaries
```

Token → `ColourId` mapping (when a shared `LookAndFeel_V4` lands; today set directly):

| Token | Used by | ColourId (if/when LookAndFeel introduced) |
|-------|---------|-------------------------------------------|
| `Palette::playhead` | playhead line + cap (custom paint) | none — custom draw |
| `Palette::gridStrong` | section divider (custom paint) | none — custom draw |
| `Palette::zoomChipBg` | zoom stepper buttons | `TextButton::buttonColourId` |
| `Palette::accent` (existing) | zoom stepper toggled/hover | `TextButton::buttonOnColourId` |

No new `ColourId` constants are invented; the grid is fully custom-painted and the stepper
reuses the standard `juce::TextButton::buttonColourId` already used by the current
4/8/16 buttons.

---

## Interaction & state model summary

| Element | Default | Hover | Pressed | Disabled | Active/Selected |
|---------|---------|-------|---------|----------|-----------------|
| Zoom `[+]`/`[−]` | `surfaceElevated` bg, `textPrimary` glyph | bg lightens to `divider` | bg `accent.withAlpha(0.3)` | dim glyph at min/max level (`textSecondary`) | n/a |
| Zoom chip (label) | `textSecondary` text on `zoomChipBg` | — | — | — | shows current level name |
| Grid cell | section colour @ 0.40 fill, `divider` outline | `accent @0.15` fill + `accent` outline (existing) | — | — | `accent` 2 px outline (existing selection) |
| Section boundary | n/a | n/a | n/a | n/a | `gridStrong` 2 px left edge |
| Playhead (playing) | `playhead` 2 px line + triangle cap | — | — | — | follows transport |
| Playhead (stopped) | `playheadDim` 1 px cursor at seek pos | — | — | — | — |

**Animation.** None required for correctness. The playhead's motion is the 40 Hz timer
repaint. Zoom is an instant re-layout (no tween) — deliberate, since animating a full
re-grid is costly and the anchor-preservation already makes the transition legible. If
desired later, `juce_animation` could ease the `setViewPosition` scroll on zoom; flagged as
optional polish, not in scope.

---

## Open questions & recommended option

1. **Section-aligned level 1 (ragged rows) vs. uniform grid throughout?**
   *Recommend ragged `Section` mode.* It is the headline value of the feature ("read the
   arrangement at a glance") and the request explicitly says "one section per row". Cost is
   a `rowSpans` table and mode-aware `cellBounds` — contained, well-tested via the existing
   unit-test harness.

2. **Derive `S` from median vs. per-song fixed section size?**
   *Recommend median section length*, rounded to a whole bar, with a floor of 2 bars. It
   degrades gracefully for songs with irregular sections and keeps the fraction steps
   landing on whole bars. (If `segments` is empty, fall back to `S = 8`.)

3. **Keep the discrete 4/8/16 buttons as a power-user shortcut?**
   *Recommend removing them.* Two controls for the same axis (density) is clutter and
   violates "remove before you add". The zoom stepper + Ctrl-wheel covers every case; the
   `Fixed` levels already reproduce 1/2/4/8/16 bars-per-row implicitly.

4. **Playhead colour: red vs. theme accent blue?**
   *Recommend warm red (`0xffff5c5c`).* It must be unmistakably distinct from the blue
   `accent` used for selection and hover; using accent for both would make "where am I
   playing" and "what is selected" read identically. Red is also the DAW convention.
