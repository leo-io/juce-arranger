#pragma once
#include <juce_graphics/juce_graphics.h>

namespace Palette
{
    const juce::Colour surface         { 0xff1e1f22 }; // view backgrounds (replaces darkgrey)
    const juce::Colour surfaceElevated { 0xff2a2c30 }; // strips / toolbars
    const juce::Colour divider         { 0xff3a3d42 }; // separators, cell outlines
    const juce::Colour waveform        { 0xff8ab4f8 }; // mono curve (calmer than lightblue)
    const juce::Colour textPrimary     { 0xffe8eaed }; // primary labels
    const juce::Colour textSecondary   { 0xff9aa0a6 }; // secondary labels
    const juce::Colour accent          { 0xff7cb8ff }; // selection / highlight
}

namespace Spacing
{
    constexpr int pad       = 4;
    constexpr int gap       = 6;
    constexpr int divider   = 2;
    constexpr int rowHeight = 28;   // bar-grid row height (used by later plans)
    constexpr int monoStrip = 72;   // mono waveform strip height (used by later plans)
    constexpr int header    = 24;   // arrangement header height (used by later plans)
    constexpr int toolbar   = 22;   // grid toolbar height (used by later plans)
}
