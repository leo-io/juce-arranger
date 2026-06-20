#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "DemoUtilities.h"
#include "Arrangement.h"

class TimelineComponent : public juce::Component,
                          public juce::ChangeListener,
                          public juce::FileDragAndDropTarget,
                          public juce::ChangeBroadcaster,
                          private juce::ScrollBar::Listener,
                          private juce::Timer
{
public:
    TimelineComponent (juce::AudioFormatManager& formatManager,
                       juce::AudioTransportSource& source,
                       juce::Slider& slider);

    ~TimelineComponent() override;

    void setURL (const juce::URL& url);
    void setArrangement (const Arrangement& arrangement);

    juce::URL getLastDroppedFile() const noexcept { return lastFileDropped; }

    void setZoomFactor (double amount);
    void setRange (juce::Range<double> newRange);
    void setFollowsTransport (bool shouldFollow);

    void paint (juce::Graphics& g) override;
    void resized() override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    bool isInterestedInFileDrag (const juce::StringArray& /*files*/) override;
    void filesDropped (const juce::StringArray& files, int /*x*/, int /*y*/) override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;

private:
    juce::AudioTransportSource& transportSource;
    juce::Slider& zoomSlider;
    juce::ScrollBar scrollbar  { false };

    juce::AudioThumbnailCache thumbnailCache  { 5 };
    juce::AudioThumbnail thumbnail;
    juce::Range<double> visibleRange;
    bool isFollowingTransport = false;
    juce::URL lastFileDropped;

    juce::DrawableRectangle currentPositionMarker;

    Arrangement arrangement;

    float timeToX (const double time) const;
    double xToTime (const float x) const;
    bool canMoveTransport() const noexcept;

    void scrollBarMoved (juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override;
    void timerCallback() override;
    void updateCursorPosition();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineComponent)
};
