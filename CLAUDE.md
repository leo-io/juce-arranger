# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A JUCE GUI desktop app (C++17) that loads an arrangement-analysis JSON file (BPM, beats,
downbeats, beat positions, segment labels) alongside its audio and presents an
**arrangement view**: a zoomable, bar-by-bar grid with segment colour
coding, selection, and a transport playhead that sweeps the grid during playback.

The app opens a `.json` analysis file; the JSON's `path` field points at the audio
(falling back to a same-named audio file sitting next to the JSON when the embedded
absolute path doesn't resolve — see `MainComponent::showAudioResource`).

## Build & run

JUCE is **not** vendored. It is pulled in via `add_subdirectory(../JUCE ...)`, so a
JUCE checkout must exist as a **sibling directory** of this repo (`../JUCE`).

```powershell
# Configure (first time, or after CMakeLists changes)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build --config Debug

# The executable is emitted to the repo root (RUNTIME_OUTPUT_DIRECTORY = source dir),
# not into build/. Run juce-arranger.exe from the project root.
```

`build/` and `cmake-build/` are both pre-existing local build trees (gitignored
artifacts); prefer `build/`.

## Tests

`BarGridModel.h` contains an extensive `juce::UnitTest` suite under
`#if JUCE_UNIT_TESTS` (covers `rebuild`, zoom level → bars-per-row mapping, ragged
Segment `cellBounds`, hit-testing, and the selection API). **`JUCE_UNIT_TESTS` is not
currently defined in `CMakeLists.txt`, so these tests are not compiled or run by the
default build.** To exercise them you must define the flag and add a
`juce::UnitTestRunner` call to an entry point — there is no test target today. Keep new
model logic covered by extending this suite in the same `#if JUCE_UNIT_TESTS` block.

## Architecture

The data flows **analysis JSON → model → grid component → view → main shell**, with
the audio transport threaded through as a shared reference.

- **`Arrangement`** (`Arrangement.h`, header-only) — the parsed arrangement: `bpm`,
  `beats`, `downbeats`, `beatPositions`, `segments`. `fromJsonFile` parses; `segmentAt`,
  `barNumberAt`, and `colourForLabel` are the query/lookup surface. This is the single
  source of arrangement structure consumed everywhere downstream.

- **`BarGridModel`** (`BarGridModel.h`, header-only, **message-thread-only, no locks**) —
  the pure data model for the grid. `rebuild()` turns arrangement data into a `bars` vector
  (using downbeats, else synthesizing from BPM). The **zoom system** lives here:
  `applyZoom(level)` sets one of three `GridMode`s and derives the layout —
  - `WholeArrangement` (level 0): entire arrangement in one row.
  - `Segment` (level 1): one segment per row, **ragged** rows of varying bar counts,
    driven by a `rowSpans` table — this is the one mode that breaks uniform grid math.
  - `Fixed` (levels 2..N): uniform `barsPerRow = ceil(S / 2^(level-1))` where `S` is the
    median segment length in bars; level N reaches 1 bar/row.
  All geometry (`cellBounds`, `cellWidthForRow`, `rowCount`, hit-testing) consults
  `rowSpans` in `Segment` mode and uses uniform math otherwise. Selection is a
  half-open `juce::Range<int>` of bar indices.

- **`BarGridComponent`** (`.h/.cpp`) — the scrolled, custom-painted grid. Paints one
  cell per bar, handles mouse/keyboard selection, and draws the playhead overlay last
  (on top). Label/divider density keys off `cellWidth` via `model.labelDetailFor`.
  Emits `onZoomRequest(delta, anchorBar)` on Ctrl+wheel; broadcasts selection changes
  via `ChangeBroadcaster`.

- **`ArrangementView`** (`.h/.cpp`) — container that wraps `BarGridComponent` in a
  vertical-only `juce::Viewport`, plus a header band and a zoom-stepper toolbar.
  Owns the `BarGridModel`. Runs a **40 Hz `juce::Timer`** that polls
  `transportSource.getCurrentPosition()` to drive the playhead and follow-transport
  auto-scroll (with ~⅓ lookahead and a post-user-scroll suppression window). Implements
  anchor-preserving zoom (`zoomAroundAnchor`). Re-broadcasts grid changes upward so
  `MainComponent` can react and persist zoom level.

- **`MainComponent`** (`.h/.cpp`) — the app shell. Owns the audio stack
  (`AudioDeviceManager`, `AudioFormatManager`, `AudioTransportSource`,
  `TimeSliceThread`), transport controls, the `ArrangementView`, the `LogConsole`, and
  persistence via `juce::ApplicationProperties` (zoom level is stored under key
  `"zoomLevel"`). Resolves the JSON → audio-file path.

- **`Theme.h`** — `Palette` (colours) and `Spacing` (pixel constants, all **logical**
  px) token namespaces. **Single source of truth for visual design.** New colours and
  sizes go here, not inline; the grid is fully custom-painted (no LookAndFeel override).

- **`TimelineComponent`** (`.h/.cpp`) — an older waveform/timeline strip
  (`AudioThumbnail`-based, file drag-and-drop). Not wired into the current
  `ArrangementView` flow; the bar grid is the active arrangement UI.

- **`Main.cpp`** — `JUCEApplication` entry point (`MarkerPlayerApplication`), hosting
  `MainComponent` in a `DocumentWindow`.

`docs/design/zoom-playhead-spec.md` is the authoritative UX/UI design spec for the zoom
and playhead features and explains the rationale behind the model's mode/zoom design.

## Conventions

- **Threading**: GUI/model code runs on the message thread only; never touch a
  `juce::Component` or `BarGridModel` from the audio or background thread. UI follows the
  transport via the `ArrangementView` timer polling — **not** via the audio callback.
- **Lean on JUCE**: prefer existing JUCE classes (`juce::String`, `juce::Array`,
  `juce::Range`, `juce::Viewport`, `juce::Timer`, `juce::ChangeBroadcaster`, etc.) over
  hand-rolled equivalents — match the idiom of the surrounding file.
- **Style**: JUCE house style — `//====` section separators, `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR`
  on classes, space-before-paren call style. Match the file you are editing.
- Button `onClick` handlers in `MainComponent` wrap their bodies in try/catch that logs
  to `juce::Logger` (surfaced live in the collapsible `LogConsole`).
- New source files must be added to the `target_sources` list in `CMakeLists.txt` —
  a file that isn't compiled isn't done.

## Agents

`.opencode/agents/juce-developer.md` defines a JUCE-implementer agent persona used in
this project (implements a written dev plan into idiomatic JUCE code; flags gaps rather
than re-architecting). Worth reading if continuing that plan-driven workflow.
