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
    BarGridModel& getModel() noexcept { return model; }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    BarGridModel& model;
    juce::AudioTransportSource& transportSource;
    int contentWidth = 0;
    std::optional<int> hoveredBar;

    void updateAccessibilityDescription();
    void scrollSelectedIntoView();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BarGridComponent)
};
