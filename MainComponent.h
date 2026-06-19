#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "DemoUtilities.h"
#include "TimelineComponent.h"
#include "LogConsole.h"
#include "Theme.h"

class MainComponent : public juce::Component,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::AudioDeviceManager audioDeviceManager;
    juce::AudioFormatManager formatManager;
    juce::TimeSliceThread thread  { "audio file preview" };

    juce::URL currentAudioFile;
    juce::AudioSourcePlayer audioSourcePlayer;
    juce::AudioTransportSource transportSource;
    std::unique_ptr<juce::AudioFormatReaderSource> currentAudioFileSource;

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::TextButton openFileButton { "Open JSON File..." };
    juce::TextButton logToggleButton { "Log" };

    std::unique_ptr<TimelineComponent> timeline;
    juce::Label zoomLabel                     { {}, "Zoom:" };
    juce::Slider zoomSlider                   { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::ToggleButton followTransportButton  { "Follow Transport" };
    juce::TextButton startStopButton          { "Play/Stop" };

    juce::Label statusLabel;
    SongAnalysis currentAnalysis;

    std::unique_ptr<LogConsole> logConsole;

    class StatusUpdateTimer : public juce::Timer
    {
    public:
        std::function<void()> callback;
        void timerCallback() override
        {
            if (callback)
                callback();
        }
    };
    std::unique_ptr<StatusUpdateTimer> statusTimer;

    void showAudioResource (juce::URL resource);
    bool loadURLIntoTransport (const juce::URL& audioURL);
    void startOrStop();
    void updateFollowTransportState();
    void openAudioFile();
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void updateStatus();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
