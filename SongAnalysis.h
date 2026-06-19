#pragma once

#include <juce_core/juce_core.h>
#include <vector>

struct Segment
{
    double start = 0.0;
    double end = 0.0;
    juce::String label;
};

class SongAnalysis
{
public:
    double bpm = 0.0;
    std::vector<double> beats;
    std::vector<double> downbeats;
    std::vector<int> beatPositions;
    std::vector<Segment> segments;

    static SongAnalysis fromJsonFile (const juce::File& jsonFile)
    {
        SongAnalysis result;

        if (!jsonFile.existsAsFile())
            return result;

        auto json = juce::JSON::parse (jsonFile);

        if (!json.isObject())
            return result;

        result.bpm = json.getProperty ("bpm", 0.0);

        if (auto beatsVar = json["beats"])
        {
            if (auto* beatsArray = beatsVar.getArray())
            {
                for (const auto& beatVar : *beatsArray)
                    result.beats.push_back ((double)beatVar);
            }
        }

        if (auto downbeatsVar = json["downbeats"])
        {
            if (auto* downbeatsArray = downbeatsVar.getArray())
            {
                for (const auto& downbeatVar : *downbeatsArray)
                    result.downbeats.push_back ((double)downbeatVar);
            }
        }

        if (auto positionsVar = json["beat_positions"])
        {
            if (auto* positionsArray = positionsVar.getArray())
            {
                for (const auto& posVar : *positionsArray)
                    result.beatPositions.push_back ((int)posVar);
            }
        }

        if (auto segmentsVar = json["segments"])
        {
            if (auto* segmentsArray = segmentsVar.getArray())
            {
                for (const auto& segVar : *segmentsArray)
                {
                    if (segVar.isObject())
                    {
                        Segment seg;
                        seg.start = segVar.getProperty ("start", 0.0);
                        seg.end = segVar.getProperty ("end", 0.0);
                        seg.label = segVar.getProperty ("label", "").toString();
                        result.segments.push_back (seg);
                    }
                }
            }
        }

        return result;
    }

    juce::Colour colourForLabel (const juce::String& label) const
    {
        if (label == "chorus")  return juce::Colour (0xff7cb8ff);  // light blue
        if (label == "verse")   return juce::Colour (0xff7cff7c);  // light green
        if (label == "intro")   return juce::Colour (0xffffff7c);  // light yellow
        if (label == "inst")    return juce::Colour (0xffff7c7c);  // light red
        if (label == "solo")    return juce::Colour (0xffff7cff);  // light magenta
        if (label == "start")   return juce::Colour (0xffcccccc);  // light grey
        if (label == "end")     return juce::Colour (0xffaaaaaa);  // grey

        // Hash-based colour for unknown labels
        auto hash = (uint32_t)juce::String (label).hashCode();
        return juce::Colour::fromHSV ((hash % 360) / 360.0f, 0.4f, 0.8f, 1.0f);
    }

    int barNumberAt (double timeInSeconds) const
    {
        if (downbeats.empty())
            return 0;

        size_t barIndex = 0;
        for (size_t i = 0; i < downbeats.size(); ++i)
        {
            if (downbeats[i] <= timeInSeconds)
                barIndex = i;
            else
                break;
        }
        return (int) barIndex + 1;
    }

    const Segment* segmentAt (double timeInSeconds) const
    {
        for (const auto& seg : segments)
        {
            if (timeInSeconds >= seg.start && timeInSeconds < seg.end)
                return &seg;
        }
        return nullptr;
    }
};
