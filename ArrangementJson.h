#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
/**
    Parser for the arrangement JSON format:
    { "arrangement": { "tempo": {...}, "segments": [ { "bars": [ { "beats": [...] } ] } ] } }

    This header is included from Arrangement.h AFTER the Arrangement class is
    fully defined, so Arrangement is visible here without a forward declaration.
    It does NOT re-include Arrangement.h.
*/
struct ArrangementJson
{
    /** Parse an arrangement-wrapped JSON object into the flat Arrangement model.

        Walks segments → bars → beats and populates:
          – beats[]          from each beat's .start
          – beatPositions[]  from each beat's .position (converted to 0-based)
          – downbeats[]      from beats where .downbeat is true
          – segments[]       span = first-beat-start → last-beat-end per segment
          – bpm              from arrangement.tempo.bpm
          – name / globalKey from arrangement-level fields
    */
    static void parseInto (const juce::var& json, Arrangement& out)
    {
        out = Arrangement();

        const juce::var& root = json["arrangement"];

        if (!root.isObject())
            return;

        //--------------------------------------------------------------------------
        // Arrangement-level metadata
        //--------------------------------------------------------------------------
        out.name      = root.getProperty ("name", "").toString();
        out.globalKey = root.getProperty ("globalKey", "").toString();

        //--------------------------------------------------------------------------
        // Tempo
        //--------------------------------------------------------------------------
        out.bpm = root["tempo"].getProperty ("bpm", 0.0);

        //--------------------------------------------------------------------------
        // Segments
        //--------------------------------------------------------------------------
        auto segmentsVar = root["segments"];

        if (auto* segmentsArray = segmentsVar.getArray())
        {
            for (const auto& segmentVar : *segmentsArray)
            {
                juce::String label    = segmentVar.getProperty ("label", "");
                juce::String audioSrc = segmentVar.getProperty ("audioSource", "");
                double localBpm       = segmentVar.getProperty ("localBpm", 0.0);
                juce::String localKey = segmentVar.getProperty ("localKey", "").toString();

                double segStart = std::numeric_limits<double>::max();
                double segEnd   = -std::numeric_limits<double>::max();

                auto barsVar = segmentVar["bars"];
                if (auto* barsArray = barsVar.getArray())
                {
                    for (const auto& barVar : *barsArray)
                    {
                        auto beatsVar  = barVar["beats"];
                        auto* beatsArr = beatsVar.getArray();
                        if (beatsArr == nullptr || beatsArr->isEmpty())
                            continue;

                        for (const auto& beatVar : *beatsArr)
                        {
                            double start    = beatVar.getProperty ("start",    0.0);
                            double end      = beatVar.getProperty ("end",      0.0);
                            int    position = (int) beatVar.getProperty ("position", 1);
                            bool   downbeat = beatVar.getProperty ("downbeat", false);

                            out.beats.push_back (start);
                            out.beatPositions.push_back (position - 1);  // 1-based → 0-based

                            if (downbeat)
                                out.downbeats.push_back (start);

                            if (start < segStart) segStart = start;
                            if (end   > segEnd)   segEnd   = end;
                        }
                    }
                }

                if (segStart <= segEnd && segStart != std::numeric_limits<double>::max())
                {
                    Segment seg;
                    seg.start       = segStart;
                    seg.end         = segEnd;
                    seg.label       = label;
                    seg.audioSource = audioSrc;
                    seg.localBpm    = localBpm;
                    seg.localKey    = localKey;
                    out.segments.push_back (seg);
                }
            }
        }
    }
};
