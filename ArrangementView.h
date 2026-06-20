#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "BarGridModel.h"
#include "BarGridComponent.h"
#include "SongAnalysis.h"
#include "Theme.h"

//==============================================================================
/**
    ArrangementView is a container Component that provides a wrapped bar-selection
    grid (BarGridComponent inside a juce::Viewport).

    It provides:
      - A header row showing song name, time signature, BPM, bar count and a
        colour legend for segment labels.
      - A toolbar with zoom stepper ([-] zoom level [+] ) and a selection
        readout label.
      - Follow-transport: during playback, the currently-playing bar is
        highlighted in the grid, the playhead line sweeps across the cells,
        and the viewport scrolls to follow with look-ahead.

    This class is a juce::ChangeBroadcaster — it re-broadcasts grid selection
    changes so that MainComponent can react (e.g. update the global status label
    and persist zoom level).
*/
class ArrangementView : public juce::Component,
                        public juce::ChangeBroadcaster,
                        private juce::ChangeListener,
                        private juce::Timer
{
public:
    ArrangementView (juce::AudioFormatManager& formatManager,
                     juce::AudioTransportSource& transport,
                     juce::TimeSliceThread& worker);
    ~ArrangementView() override;

    void setURL (const juce::URL& url);
    void setAnalysis (const SongAnalysis& analysis);
    void setZoomLevel (int level);
    void setFollowsTransport (bool shouldFollow) noexcept { followTransport = shouldFollow; }
    bool getFollowsTransport() const noexcept { return followTransport; }
    int getZoomLevel() const noexcept { return zoomLevel; }
    juce::Range<int> getSelection() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::AudioTransportSource& transportSource;

    SongAnalysis analysis;
    juce::String songName;
    int zoomLevel = 1;
    int maxZoomLevel = 4;
    bool followTransport = false;

    // User-scroll suppression tracking
    juce::uint32 lastUserScrollMs = 0;
    int lastSetViewY = 0;

    // children — declaration order ensures safe destruction
    BarGridModel model;
    juce::Viewport gridViewport;
    std::unique_ptr<BarGridComponent> grid;

    // Zoom chip toolbar
    juce::Label zoomChip;
    juce::Label selectionReadout;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void layoutGrid();
    void updateSelectionReadout();
    void updateZoomControls();
    void zoomAroundAnchor (int newLevel, int anchorBar, int cursorYInView);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};
