# juce-arranger

## Build

Uses CMake + Visual Studio 2022. Run from PowerShell 5.1+:

```powershell
.\build.ps1                   # Debug build
.\build.ps1 -Config Release
.\build.ps1 -Clean            # wipe cmake-build/ first
```

Output: `juce-arranger.exe` in project root.

Prerequisite: a sibling `../JUCE` directory with a JUCE checkout at configure time.

## Architecture

- **Entry**: `Main.cpp` → `MarkerPlayerApplication` → `MainComponent` (orchestrator)
- **Core classes**: `TimelineComponent` (waveform + transport bar grid), `BarGridComponent` (bar grid), `BarGridModel` (pure data, header-only), `SongAnalysis` (data + JSON parsing)
- **Theme**: `Theme.h` — `Palette` colors and `Spacing` constants used project-wide
- **Debug**: `LogConsole` hijacks `juce::Logger`; toggled via "Log" button
- All paint/mouse/key callbacks wrap body in try/catch and log to `juce::Logger`
- No test framework beyond JUCE's built-in `JUCE_UNIT_TESTS` (see `BarGridModel.h`)

## Data format

App loads a JSON analysis file (paired with a WAV). Supported fields:

```json
{ "bpm": 120, "beats": [...], "downbeats": [...], "beat_positions": [...], "segments": [{"start": 0, "end": 2, "label": "verse"}] }
```

WAV and JSON can be loaded:
- directly via "Open JSON File..." button
- drag-and-drop a `.json` onto the timeline
- opening a WAV auto-looks for a sibling `.json`

## Git

- Primary branch: `dev` — commit and PR against `dev`, not `master`
- `master` is the stable release branch
- No tags yet

## Key conventions

- `juce::URL` returned from open — prefer `showAudioResource()` in `MainComponent`
- New components should follow JUCE's `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR` pattern
- Source files are flat (no subdirectories); CMakeLists.txt lists each `.cpp` explicitly
