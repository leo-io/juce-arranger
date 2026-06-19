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
    const juce::Colour playhead        { 0xffff5c5c }; // transport playhead line/triangle (warm red)
    const juce::Colour playheadDim     { 0x80ff5c5c }; // stopped-state seek cursor (50% alpha)
    const juce::Colour gridStrong      { 0xff52565c }; // bold section-boundary divider (2 px)
    const juce::Colour zoomChipBg      { 0xff2a2c30 }; // zoom stepper chip background (= surfaceElevated)
}

namespace Spacing
{
    constexpr int pad            = 4;
    constexpr int gap            = 6;
    constexpr int divider        = 2;
    constexpr int rowHeight      = 28;   // bar-grid row height (used by later plans)
    constexpr int monoStrip      = 72;   // mono waveform strip height (used by later plans)
    constexpr int header         = 24;   // arrangement header height (used by later plans)
    constexpr int toolbar        = 22;   // grid toolbar height (used by later plans)
    constexpr int playheadWidth  = 2;    // playhead line thickness (logical px)
    constexpr int playheadCap    = 7;    // triangle cap base width
    constexpr int minCellPx      = 6;    // cell width floor before density downgrade
    constexpr int sectionDivider = 2;    // bold divider weight at section boundaries
}
