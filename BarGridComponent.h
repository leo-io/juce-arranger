#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "BarGridModel.h"

class BarGridComponent : public juce::Component,
                         public juce::ChangeBroadcaster
{
public:
    BarGridComponent (BarGridModel& model, juce::AudioTransportSource& transport);
    ~BarGridComponent() override = default;

    void layoutForWidth (int viewportWidth);
    void refresh();
    std::optional<int> getHoveredBar() const noexcept { return hoveredBar; }
    void setPlayingBar (std::optional<int> barIndex);
    std::optional<int> getPlayingBar() const noexcept { return playingBar; }
    void setPlayheadPosition (std::optional<double> timeSeconds);
    std::optional<double> getPlayheadPosition() const noexcept { return playheadTime; }
    juce::Rectangle<int> playheadCellRect (int viewportWidth) const;
    BarGridModel& getModel() noexcept { return model; }

    // Zoom request: delta (+1 = zoom in, -1 = zoom out), anchorBar = bar to anchor
    std::function<void(int delta, int anchorBar)> onZoomRequest;

    // Rename request: segment index (into Arrangement::segments) and proposed new name
    std::function<void(int segmentIndex, juce::String newName)> onSegmentRename;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    BarGridModel& model;
    juce::AudioTransportSource& transportSource;
    int contentWidth = 0;
    std::optional<int> hoveredBar;
    std::optional<int> playingBar;
    std::optional<double> playheadTime;

    void updateAccessibilityDescription();
    void scrollSelectedIntoView();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BarGridComponent)
};
