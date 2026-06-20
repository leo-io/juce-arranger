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


def _normalize_allin1(beats: list, downbeats: list, beat_positions: list):
    """
    Adjust raw allin1 arrays in-place before any parsing:
      1. Trim beats/positions from the front until beat_positions[0] == 1.
      2. Trim beats/positions from the back until beat_positions[-1] == 4.
         Expand (from adjacent data) is not possible here, so trimming only.
      3. Drop any downbeat that has no matching beat (rounded to 6 dp).
    Returns (beats, downbeats, beat_positions) as new lists.
    """
    beats = list(beats)
    downbeats = list(downbeats)
    beat_positions = list(beat_positions)

    def _drop_downbeat_at(t):
        key = round(t, 6)
        return [d for d in downbeats if round(d, 6) != key]

    # 1. Trim front until beat_positions starts with 1
    while beat_positions and int(beat_positions[0]) != 1:
        removed = beats.pop(0)
        beat_positions.pop(0)
        downbeats = _drop_downbeat_at(removed)

    # 2. Trim back until beat_positions ends with 4
    while beat_positions and int(beat_positions[-1]) != 4:
        removed = beats.pop()
        beat_positions.pop()
        downbeats = _drop_downbeat_at(removed)

    # 3. Remove downbeats that have no corresponding beat
    beats_set = {round(t, 6) for t in beats}
    downbeats = [d for d in downbeats if round(d, 6) in beats_set]

    return beats, downbeats, beat_positions


def _build_bars(beats, beat_positions, downbeats_set, beat_ends):
    """Group flat beat arrays into a list of bars (each bar is a list of beat dicts).

    Bars are downbeat-delimited (the downbeat array is the single grouping authority).
    Anacrusis beats — any beats before the first downbeat — are dropped, since a bar
    has no meaning before its opening downbeat (see arrangement-json-v2-spec.md).
    """
    bars = []
    current = []
    started = False

    for i, t in enumerate(beats):
        is_downbeat = round(t, 6) in downbeats_set
        if not started:
            if not is_downbeat:
                continue  # drop anacrusis: beats preceding the first downbeat
            started = True
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

    beats, downbeats, beat_positions = _normalize_allin1(beats, downbeats, beat_positions)

    downbeats_set = {round(d, 6) for d in downbeats}
    total_duration = segments_in[-1]["end"] if segments_in else (beats[-1] if beats else 0.0)
    beat_ends = beats[1:] + [total_duration]

    all_bars = _build_bars(beats, beat_positions, downbeats_set, beat_ends)

    segments_out = []
    global_bar_index = 1

    for seg_idx, seg in enumerate(segments_in):
        seg_start = seg["start"]
        seg_end = seg["end"]

        # A bar belongs to exactly one segment: the one whose half-open
        # [start, end) time range contains the bar's opening downbeat. Because
        # the ranges partition the timeline, every bar lands in a single segment
        # with no overlap and no gap, so global_bar_index stays contiguous.
        seg_indices = [
            i for i, bar in enumerate(all_bars)
            if seg_start <= bar[0]["time"] < seg_end
        ]

        seg_bars = []
        for i in seg_indices:
            bar = all_bars[i]
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
