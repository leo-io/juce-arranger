#!/usr/bin/env python3
"""
Convert an allin1player analysis JSON to juce-arranger arrangement JSON.

Input:  <name>.json  (flat arrays: beats, downbeats, beat_positions, segments)
Output: <name>.juce.arrangement.json  (hierarchical: arrangement→segments→bars→beats)

Usage:
    python convert_to_juce_arrangement.py path/to/<name>.json
"""

import json
import sys
from pathlib import Path


def _build_bars(beats, beat_positions, downbeats_set, beat_ends):
    """Group flat beat arrays into a list of bars (each bar is a list of beat dicts)."""
    bars = []
    current = []

    for i, t in enumerate(beats):
        is_downbeat = round(t, 6) in downbeats_set
        if is_downbeat and current:
            bars.append(current)
            current = []
        current.append({
            "time": t,
            "end": beat_ends[i],
            "position": int(beat_positions[i]),
            "downbeat": is_downbeat,
        })

    if current:
        bars.append(current)

    return bars


def convert(input_path_str: str) -> Path:
    path = Path(input_path_str).resolve()
    data = json.loads(path.read_text(encoding="utf-8"))

    bpm = float(data["bpm"])
    beats: list = data["beats"]
    downbeats: list = data["downbeats"]
    beat_positions: list = data["beat_positions"]
    segments_in: list = data["segments"]
    audio_path: str = data.get("path", "")

    downbeats_set = {round(d, 6) for d in downbeats}
    total_duration = segments_in[-1]["end"] if segments_in else (beats[-1] if beats else 0.0)
    beat_ends = beats[1:] + [total_duration]

    all_bars = _build_bars(beats, beat_positions, downbeats_set, beat_ends)

    segments_out = []
    global_bar_index = 1

    for seg_idx, seg in enumerate(segments_in):
        seg_start = seg["start"]
        seg_end = seg["end"]

        seg_bars = []
        for bar in all_bars:
            bar_start = bar[0]["time"]
            if seg_start <= bar_start < seg_end:
                seg_bars.append({
                    "index": global_bar_index,
                    "beats": [
                        {
                            "position": b["position"],
                            "downbeat": b["downbeat"],
                            "start": b["time"],
                            "end": b["end"],
                        }
                        for b in bar
                    ],
                })
                global_bar_index += 1

        segments_out.append({
            "id": seg_idx + 1,
            "label": seg["label"],
            "localBpm": None,
            "localKey": "",
            "audioSource": audio_path,
            "colour": None,
            "bars": seg_bars,
        })

    output = {
        "arrangement": {
            "schemaVersion": "2.0",
            "name": path.stem,
            "globalKey": "",
            "tempo": {"bpm": bpm, "timeSignature": [4, 4]},
            "segments": segments_out,
        }
    }

    out_path = path.parent / (path.stem + ".juce.arrangement.json")
    out_path.write_text(json.dumps(output, indent=2), encoding="utf-8")
    return out_path


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python convert_to_juce_arrangement.py <path/to/file.json>")
        sys.exit(1)

    result = convert(sys.argv[1])
    print(f"Saved: {result}")
