#pragma once

#include <juce_core/juce_core.h>
#include <vector>

struct Segment
{
    double start = 0.0;
    double end = 0.0;
    juce::String label;
    juce::String audioSource;
    double localBpm = 0.0;
    juce::String localKey;
};

class Arrangement
{
public:
    juce::String name;
    juce::String globalKey;
    double bpm = 0.0;
    std::vector<double> beats;
    std::vector<double> downbeats;
    std::vector<int> beatPositions;
    std::vector<Segment> segments;

    static Arrangement fromJsonFile (const juce::File& jsonFile);

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

//==============================================================================
// Parser helper — needs Arrangement to be fully defined first.
//==============================================================================
#include "ArrangementJson.h"

inline Arrangement Arrangement::fromJsonFile (const juce::File& jsonFile)
{
    Arrangement result;

    if (!jsonFile.existsAsFile())
        return result;

    auto json = juce::JSON::parse (jsonFile.loadFileAsString());

    if (!json.isObject())
        return result;

    ArrangementJson::parseInto (json, result);
    return result;
}
