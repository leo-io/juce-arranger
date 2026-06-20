#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <limits>
#include <cmath>
#include <algorithm>

//==============================================================================
// Internal types used by SongJsonV2 — declared at file scope so they are visible
// to all inline member functions regardless of declaration order.
//==============================================================================
struct V2Beat { double start; double end; int position; };
struct V2Bar  { std::vector<V2Beat> beats; };

//==============================================================================
/**
    Helper functions for detecting, parsing, and converting the v2 nested
    song-analysis JSON format (sections → bars → beats, with beat-only timing).

    This header is designed to be included from SongAnalysis.h AFTER the
    SongAnalysis class definition, so that SongAnalysis is fully defined when
    these free functions are compiled.  It does NOT re-include SongAnalysis.h.
*/
struct SongJsonV2
{
    //==========================================================================
    /** True iff the JSON contains a "sections" array (v2 format).
        Old-format files use a "segments" array instead.
    */
    static bool isV2 (const juce::var& json)
    {
        return json.isObject()
            && json.getProperty ("sections", juce::var()).isArray();
    }

    //==========================================================================
    /** Parse a v2 sections→bars→beats JSON object into the flat SongAnalysis model.

        Walks the nested structure and populates:
          – beats[]          from each beat's .start
          – beatPositions[]  from each beat's .position (converted to 0-based)
          – downbeats[]      from each bar's position-1 beat .start
          – segments[]       from each section (span = first-beat→last-beat)
          – bpm              from tempo.bpm
    */
    static void parseV2Into (const juce::var& json, SongAnalysis& out)
    {
        out = SongAnalysis();

        //--------------------------------------------------------------------------
        // Tempo
        //--------------------------------------------------------------------------
        out.bpm = json["tempo"].getProperty ("bpm", 0.0);

        //--------------------------------------------------------------------------
        // Sections
        //--------------------------------------------------------------------------
        auto sections = json["sections"];

        if (auto* sectionsArray = sections.getArray())
        {
            for (const auto& sectionVar : *sectionsArray)
            {
                juce::String label     = sectionVar.getProperty ("label", "");
                juce::String audioSrc  = sectionVar.getProperty ("audioSource", "");

                double sectionStart = std::numeric_limits<double>::max();
                double sectionEnd   = -std::numeric_limits<double>::max();

                auto bars = sectionVar["bars"];
                if (auto* barsArray = bars.getArray())
                {
                    for (const auto& barVar : *barsArray)
                    {
                        auto beats = barVar["beats"];
                        auto* beatsArray = beats.getArray();
                        if (beatsArray == nullptr || beatsArray->size() == 0)
                            continue;

                        for (const auto& beatVar : *beatsArray)
                        {
                            double start    = beatVar.getProperty ("start", 0.0);
                            double end      = beatVar.getProperty ("end", 0.0);
                            int    position = (int) beatVar.getProperty ("position", 1);
                            bool   downbeat = beatVar.getProperty ("downbeat", false);

                            // Store beat start time (old-format representation)
                            out.beats.push_back (start);

                            // Convert from 1-based (v2) to 0-based (old internal)
                            out.beatPositions.push_back (position - 1);

                            // Downbeats = position-1 beats
                            if (downbeat)
                                out.downbeats.push_back (start);

                            // Track section span
                            if (start < sectionStart)
                                sectionStart = start;
                            if (end > sectionEnd)
                                sectionEnd = end;
                        }
                    }
                }

                // Add segment if we have a valid span
                if (sectionStart <= sectionEnd && sectionStart != std::numeric_limits<double>::max())
                {
                    Segment seg;
                    seg.start       = sectionStart;
                    seg.end         = sectionEnd;
                    seg.label       = label;
                    seg.audioSource = audioSrc;
                    out.segments.push_back (seg);
                }
            }
        }
    }

    //==========================================================================
    /** Build the v2 nested structure from the flat SongAnalysis model.

        Key conversion rules:
          – Split beats into bars by downbeat ranges [downbeats[i], downbeats[i+1]).
          – Beats before the first downbeat (anacrusis) are dropped.
          – Bars are numbered 1..N, gapless across the song.
          – Each bar's position-1 beat.start == downbeats[i].
          – Each section gets audioSource = audioPath.
          – Beat positions are 1-based in the output.
          – Beat end times are the next beat's start, or the next downbeat for the
            last beat of a non-last bar, or estimated from BPM for the very last beat.
    */
    static juce::var toV2 (const SongAnalysis& analysis, const juce::String& audioPath)
    {
        juce::var root (new juce::DynamicObject());

        //--------------------------------------------------------------------------
        // Schema version
        //--------------------------------------------------------------------------
        root.getDynamicObject()->setProperty ("schemaVersion", "2.0");

        //--------------------------------------------------------------------------
        // Tempo
        //--------------------------------------------------------------------------
        juce::var tempo (new juce::DynamicObject());
        tempo.getDynamicObject()->setProperty ("bpm", analysis.bpm);

        juce::Array<juce::var> timeSig;
        timeSig.add (4);
        timeSig.add (4);
        tempo.getDynamicObject()->setProperty ("timeSignature", timeSig);

        root.getDynamicObject()->setProperty ("tempo", tempo);

        //--------------------------------------------------------------------------
        // Build raw beat list for bar grouping
        //--------------------------------------------------------------------------
        struct RawBeat { double start; int position; };
        std::vector<RawBeat> rawBeats;

        // If beats array is non-empty and matches beatPositions, use it
        if (!analysis.beats.empty() && analysis.beats.size() == analysis.beatPositions.size())
        {
            for (size_t i = 0; i < analysis.beats.size(); ++i)
                rawBeats.push_back ({ analysis.beats[i], analysis.beatPositions[i] });
        }
        else if (!analysis.beats.empty())
        {
            // Sizes differ — just use the beats with synthetic positions
            for (size_t i = 0; i < analysis.beats.size(); ++i)
                rawBeats.push_back ({ analysis.beats[i], (int)(i % 4) });
        }
        // If beats array is empty but we have downbeats + BPM, synthesize
        else if (!analysis.downbeats.empty() && analysis.bpm > 0.0)
        {
            double beatLen = 60.0 / analysis.bpm;
            for (size_t bi = 0; bi < analysis.downbeats.size(); ++bi)
            {
                double barStart = analysis.downbeats[bi];
                double barEnd   = (bi + 1 < analysis.downbeats.size())
                                    ? analysis.downbeats[bi + 1]
                                    : barStart + 4.0 * beatLen;

                int beatsInThisBar = 4;  // default 4/4
                double thisBeatLen = (barEnd - barStart) / (double) beatsInThisBar;

                for (int b = 0; b < beatsInThisBar; ++b)
                {
                    double t = barStart + (double) b * thisBeatLen;
                    if (t < barEnd - 0.0001 || b == 0)
                        rawBeats.push_back ({ t, b });
                }
            }
        }
        else
        {
            // Fallback: just use downbeats as position-0 beats
            for (size_t i = 0; i < analysis.downbeats.size(); ++i)
                rawBeats.push_back ({ analysis.downbeats[i], 0 });
        }

        // Sort by start time (belt-and-suspenders)
        std::sort (rawBeats.begin(), rawBeats.end(),
                   [] (const RawBeat& a, const RawBeat& b) { return a.start < b.start; });

        //--------------------------------------------------------------------------
        // Group raw beats into bars by downbeat boundaries
        //--------------------------------------------------------------------------
        std::vector<V2Bar> bars;
        std::vector<double> barDownbeats;  // copy of downbeats for bar-start tracking

        if (analysis.downbeats.empty())
        {
            // No downbeats — create a single bar with all beats
            V2Bar bar;
            barDownbeats.push_back (rawBeats.empty() ? 0.0 : rawBeats.front().start);

            for (size_t i = 0; i < rawBeats.size(); ++i)
            {
                double end = (i + 1 < rawBeats.size()) ? rawBeats[i + 1].start
                           : (analysis.bpm > 0.0 ? rawBeats[i].start + (60.0 / analysis.bpm)
                                                 : rawBeats[i].start);
                bar.beats.push_back ({ rawBeats[i].start, end, rawBeats[i].position });
            }

            bars.push_back (bar);
        }
        else
        {
            barDownbeats = analysis.downbeats;

            for (size_t bi = 0; bi < analysis.downbeats.size(); ++bi)
            {
                double barStart = analysis.downbeats[bi];
                double barEnd   = (bi + 1 < analysis.downbeats.size())
                                    ? analysis.downbeats[bi + 1]
                                    : std::numeric_limits<double>::max();

                // Find the first raw beat at or after barStart
                size_t idx = 0;
                while (idx < rawBeats.size() && rawBeats[idx].start < barStart - 0.0001)
                    ++idx;

                // Now walk beats until we hit barEnd
                V2Bar bar;
                while (idx < rawBeats.size() && rawBeats[idx].start < barEnd)
                {
                    double start = rawBeats[idx].start;
                    int pos = rawBeats[idx].position;

                    // Compute end: next beat start, or bar end for last beat in bar
                    double end;
                    if (idx + 1 < rawBeats.size() && rawBeats[idx + 1].start < barEnd)
                        end = rawBeats[idx + 1].start;
                    else
                        end = barEnd;  // extends to next bar start

                    // Clamp infinite end for the very last bar
                    if (end == std::numeric_limits<double>::max() || end < start)
                    {
                        end = start + (analysis.bpm > 0.0 ? (60.0 / analysis.bpm) : 0.5);
                    }

                    bar.beats.push_back ({ start, end, pos });
                    ++idx;
                }

                if (bar.beats.empty())
                {
                    // Empty bar (no beats found) — create a synthetic position-1 beat
                    double synthEnd = (bi + 1 < analysis.downbeats.size())
                                        ? analysis.downbeats[bi + 1]
                                        : (analysis.bpm > 0.0 ? barStart + (60.0 / analysis.bpm)
                                                               : barStart + 0.5);
                    bar.beats.push_back ({ barStart, synthEnd, 0 });
                }

                bars.push_back (bar);
            }
        }

        //--------------------------------------------------------------------------
        // Build sections from segments, assigning bars to each
        //--------------------------------------------------------------------------
        juce::Array<juce::var> sections;

        if (!analysis.segments.empty())
        {
            for (size_t si = 0; si < analysis.segments.size(); ++si)
            {
                const auto& seg = analysis.segments[si];
                auto sectionObj = buildSection (seg, audioPath, bars, barDownbeats, (int) si);

                // Only add non-empty sections
                if (auto* arr = sectionObj["bars"].getArray())
                {
                    if (arr->size() > 0)
                        sections.add (sectionObj);
                }
            }
        }
        else
        {
            // No segments — put all bars into a single section
            Segment fallbackSeg;
            fallbackSeg.start = (barDownbeats.empty() ? 0.0 : barDownbeats.front());
            fallbackSeg.end   = (barDownbeats.empty() ? 0.0 : barDownbeats.back());
            fallbackSeg.label = (analysis.downbeats.empty()) ? "song" : "untitled";

            auto sectionObj = buildSection (fallbackSeg, audioPath, bars, barDownbeats, 0);
            sections.add (sectionObj);
        }

        root.getDynamicObject()->setProperty ("sections", sections);
        return root;
    }

    //==========================================================================
    /** Write the v2 JSON to a file (pretty-printed). */
    static bool writeV2File (const SongAnalysis& analysis,
                             const juce::String& audioPath,
                             const juce::File& out)
    {
        auto json = toV2 (analysis, audioPath);
        auto text = juce::JSON::toString (json, false, 15);
        return out.replaceWithText (text);
    }

private:
    //==========================================================================
    /** Internal: assemble a single section's JSON object from a Segment and its bars. */
    static juce::var buildSection (const Segment& seg,
                                   const juce::String& audioPath,
                                   const std::vector<V2Bar>& bars,
                                   const std::vector<double>& barDownbeats,
                                   int sectionId)
    {
        juce::var section (new juce::DynamicObject());
        section.getDynamicObject()->setProperty ("id", sectionId + 1);
        section.getDynamicObject()->setProperty ("label", seg.label);
        section.getDynamicObject()->setProperty ("audioSource", audioPath);
        section.getDynamicObject()->setProperty ("colour", juce::var());  // null

        juce::Array<juce::var> sectionBars;

        for (size_t bi = 0; bi < bars.size(); ++bi)
        {
            double db = (bi < barDownbeats.size()) ? barDownbeats[bi] : 0.0;

            // A bar belongs to this section if its downbeat is within [seg.start, seg.end)
            if (db >= seg.start - 0.0001 && db < seg.end)
            {
                juce::var barVar (new juce::DynamicObject());
                barVar.getDynamicObject()->setProperty ("index", (int)(bi + 1));  // 1-based, gapless

                juce::Array<juce::var> beatsArray;
                for (const auto& beat : bars[bi].beats)
                {
                    juce::var beatVar (new juce::DynamicObject());
                    beatVar.getDynamicObject()->setProperty ("position", beat.position + 1);  // 1-based
                    beatVar.getDynamicObject()->setProperty ("downbeat", beat.position == 0);
                    beatVar.getDynamicObject()->setProperty ("start", beat.start);
                    beatVar.getDynamicObject()->setProperty ("end", beat.end);
                    beatsArray.add (beatVar);
                }

                barVar.getDynamicObject()->setProperty ("beats", beatsArray);
                sectionBars.add (barVar);
            }
        }

        section.getDynamicObject()->setProperty ("bars", sectionBars);
        return section;
    }
};
