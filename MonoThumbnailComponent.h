#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "SongAnalysis.h"
#include "Theme.h"

//==============================================================================
/**
    A full-width waveform strip showing a single downmixed (mono) overview of
    the loaded audio file.  Provides downbeat ticks with sparse bar labels,
    click-to-seek with downbeat snapping, an optional play cursor, and a
    cross-highlight overlay driven by an external bar selection.

    The downmix is generated off the message thread on a shared TimeSliceThread
    (the same background worker used by TimelineComponent), streaming data into
    a 1-channel AudioThumbnail and repainting as it fills.
*/
class MonoThumbnailComponent : public juce::Component,
                               public juce::ChangeListener,
                               private juce::Timer
{
public:
    MonoThumbnailComponent (juce::AudioFormatManager& formatManager,
                            juce::AudioTransportSource& transport,
                            juce::TimeSliceThread& worker);
    ~MonoThumbnailComponent() override;

    void setSource (const juce::URL& url);
    void setAnalysis (const SongAnalysis& analysis);
    void setHighlightRange (juce::Range<double> timeRange);
    void clearHighlight();

    void paint (juce::Graphics&) override;
    void resized() override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    //==============================================================================
    /** Reads audio blocks, downmixes to mono, and feeds the thumbnail. */
    class DownmixWorker : public juce::TimeSliceClient
    {
    public:
        explicit DownmixWorker (juce::AudioThumbnail& thumb) : monoThumb (thumb) {}

        void setReader (std::unique_ptr<juce::AudioFormatReader> newReader);
        std::unique_ptr<juce::AudioFormatReader> releaseReader();
        bool hasMore() const noexcept { return reader != nullptr && nextBlockStart < totalSamples; }

        int useTimeSlice() override;

    private:
        juce::AudioThumbnail& monoThumb;
        std::unique_ptr<juce::AudioFormatReader> reader;
        juce::int64 nextBlockStart = 0;
        juce::int64 totalSamples = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DownmixWorker)
    };

    //==============================================================================
    juce::AudioFormatManager& formatManager;
    juce::AudioTransportSource& transportSource;
    juce::TimeSliceThread& worker;
    juce::AudioThumbnailCache cache { 1 };
    juce::AudioThumbnail monoThumb;
    SongAnalysis analysis;
    juce::Range<double> highlight;
    DownmixWorker downmixWorker;

    double totalLength = 0.0;
    double currentPlayPosition = -1.0;

    //==============================================================================
    double timeToX (double time) const noexcept;
    double xToTime (float x) const noexcept;
    double snapToDownbeat (double time) const noexcept;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonoThumbnailComponent)
};
