#include "MainComponent.h"

MainComponent::MainComponent()
{
    addAndMakeVisible (zoomLabel);
    zoomLabel.setFont (juce::FontOptions (15.00f, juce::Font::plain));
    zoomLabel.setJustificationType (juce::Justification::centredRight);
    zoomLabel.setEditable (false, false, false);
    zoomLabel.setColour (juce::TextEditor::textColourId, juce::Colours::black);
    zoomLabel.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0x00000000));

    addAndMakeVisible (followTransportButton);
    followTransportButton.onClick = [this] {
        try { updateFollowTransportState(); }
        catch (const std::exception& e) { juce::Logger::writeToLog ("followTransportButton.onClick: " + juce::String (e.what())); }
        catch (...) { juce::Logger::writeToLog ("followTransportButton.onClick: unknown exception"); }
    };

    addAndMakeVisible (openFileButton);
    openFileButton.onClick = [this] {
        try { openAudioFile(); }
        catch (const std::exception& e) { juce::Logger::writeToLog ("openFileButton.onClick: " + juce::String (e.what())); }
        catch (...) { juce::Logger::writeToLog ("openFileButton.onClick: unknown exception"); }
    };

    addAndMakeVisible (zoomSlider);
    zoomSlider.setRange (0, 1, 0);
    zoomSlider.onValueChange = [this] {
        try
        {
            if (timeline)
                timeline->setZoomFactor (zoomSlider.getValue());
        }
        catch (const std::exception& e) { juce::Logger::writeToLog ("zoomSlider.onValueChange: " + juce::String (e.what())); }
        catch (...) { juce::Logger::writeToLog ("zoomSlider.onValueChange: unknown exception"); }
    };
    zoomSlider.setSkewFactor (2);

    timeline = std::make_unique<TimelineComponent> (formatManager, transportSource, zoomSlider);
    addAndMakeVisible (timeline.get());
    timeline->addChangeListener (this);

    addAndMakeVisible (startStopButton);
    startStopButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff79ed7f));
    startStopButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    startStopButton.onClick = [this] {
        try { startOrStop(); }
        catch (const std::exception& e) { juce::Logger::writeToLog ("startStopButton.onClick: " + juce::String (e.what())); }
        catch (...) { juce::Logger::writeToLog ("startStopButton.onClick: unknown exception"); }
    };

    addAndMakeVisible (statusLabel);
    statusLabel.setFont (juce::FontOptions (13.00f, juce::Font::plain));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setEditable (false, false, false);
    statusLabel.setText ("Ready", juce::dontSendNotification);

    // Audio setup
    formatManager.registerBasicFormats();
    thread.startThread (juce::Thread::Priority::normal);

    audioDeviceManager.initialise (0, 2, nullptr, true, {}, nullptr);
    audioDeviceManager.addAudioCallback (&audioSourcePlayer);
    audioSourcePlayer.setSource (&transportSource);

    logConsole = std::make_unique<LogConsole>();
    addAndMakeVisible (logConsole.get());

    setOpaque (true);
    setSize (1100, 650);

    // Start status update timer
    statusTimer = std::make_unique<MainComponent::StatusUpdateTimer>();
    statusTimer->callback = [this] {
        try { updateStatus(); }
        catch (const std::exception& e) { juce::Logger::writeToLog ("statusTimer.callback: " + juce::String (e.what())); }
        catch (...) { juce::Logger::writeToLog ("statusTimer.callback: unknown exception"); }
    };
    statusTimer->startTimer (100);
}

MainComponent::~MainComponent()
{
    transportSource.setSource (nullptr);
    audioSourcePlayer.setSource (nullptr);
    audioDeviceManager.removeAudioCallback (&audioSourcePlayer);

    if (timeline)
        timeline->removeChangeListener (this);
}

void MainComponent::paint (juce::Graphics& g)
{
    try
    {
        g.fillAll (getUIColourIfAvailable (juce::LookAndFeel_V4::ColourScheme::UIColour::windowBackground));
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("MainComponent::paint: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("MainComponent::paint: unknown exception"); }
}

void MainComponent::resized()
{
    try
    {
        auto r = getLocalBounds().reduced (4);

        auto logArea = r.removeFromBottom (170);
        if (logConsole)
            logConsole->setBounds (logArea);

        auto controls = r.removeFromBottom (90);

        r.removeFromBottom (6);

        if (timeline)
            timeline->setBounds (r.removeFromBottom (140));

        auto controlRightBounds = controls.removeFromRight (controls.getWidth() / 3);
        openFileButton.setBounds (controlRightBounds.removeFromTop (30).reduced (10));
        statusLabel.setBounds (controlRightBounds);

        auto zoom = controls.removeFromTop (25);
        zoomLabel .setBounds (zoom.removeFromLeft (50));
        zoomSlider.setBounds (zoom);

        followTransportButton.setBounds (controls.removeFromTop (25));
        startStopButton.setBounds (controls);
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("MainComponent::resized: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("MainComponent::resized: unknown exception"); }
}

void MainComponent::showAudioResource (juce::URL resource)
{
    juce::URL audioURL = resource;

    // If the resource is a JSON file, extract the audio path from it
    if (resource.getLocalFile().getFileExtension().toLowerCase() == ".json")
    {
        auto jsonFile = resource.getLocalFile();
        if (jsonFile.existsAsFile())
        {
            auto jsonText = jsonFile.loadFileAsString();
            auto json = juce::JSON::parse (jsonText);

            if (json.isObject() || json.isArray())
            {
                // Try to get path field
                auto pathVar = json.getProperty ("path", juce::var());
                auto audioPath = pathVar.toString();

                if (!audioPath.isEmpty())
                {
                    auto jsonDir = jsonFile.getParentDirectory();

                    // Resolve the path as written: absolute, or relative to the JSON file.
                    auto audioFile = juce::File::isAbsolutePath (audioPath)
                                         ? juce::File (audioPath)
                                         : jsonDir.getChildFile (audioPath);

                    // Common case: the JSON embeds an absolute path produced on another
                    // machine/folder, so it won't exist here. Fall back to the .wav of the
                    // same name sitting next to the JSON file. juce::File extracts the bare
                    // filename whether the stored path used '\' or '/' separators.
                    if (! audioFile.existsAsFile())
                    {
                        auto fileName = audioPath.replaceCharacter ('\\', '/')
                                                 .fromLastOccurrenceOf ("/", false, false);
                        auto sibling = jsonDir.getChildFile (fileName);
                        if (sibling.existsAsFile())
                            audioFile = sibling;
                    }

                    audioURL = juce::URL (audioFile);
                }
                else
                {
                    juce::NativeMessageBox::showAsync (juce::MessageBoxOptions()
                                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                      .withTitle ("Error")
                                                      .withMessage ("JSON file has empty or missing 'path' field"),
                                                     nullptr);
                    return;
                }
            }
            else
            {
                juce::NativeMessageBox::showAsync (juce::MessageBoxOptions()
                                                  .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                  .withTitle ("Error")
                                                  .withMessage ("Invalid JSON file format"),
                                                 nullptr);
                return;
            }
        }
        else
        {
            juce::NativeMessageBox::showAsync (juce::MessageBoxOptions()
                                              .withIconType (juce::MessageBoxIconType::WarningIcon)
                                              .withTitle ("Error")
                                              .withMessage ("Cannot read JSON file"),
                                             nullptr);
            return;
        }

        // Load analysis from the JSON file
        currentAnalysis = SongAnalysis::fromJsonFile (jsonFile);
    }
    else
    {
        // If loading a WAV file directly, look for associated JSON
        auto jsonFile = resource.getLocalFile().withFileExtension ("json");
        if (jsonFile.existsAsFile())
        {
            currentAnalysis = SongAnalysis::fromJsonFile (jsonFile);
        }
        else
        {
            currentAnalysis = SongAnalysis();
        }
    }

    auto audioFile = audioURL.getLocalFile();
    if (!audioFile.existsAsFile())
    {
        juce::NativeMessageBox::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("Error")
                                          .withMessage ("Audio file not found at:\n" + audioFile.getFullPathName()),
                                         nullptr);
        return;
    }

    if (!loadURLIntoTransport (audioURL))
    {
        juce::NativeMessageBox::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("Error")
                                          .withMessage ("Failed to load audio file at:\n" + audioFile.getFullPathName()),
                                         nullptr);
        return;
    }

    currentAudioFile = std::move (audioURL);
    zoomSlider.setValue (0, juce::dontSendNotification);

    if (timeline)
        timeline->setURL (currentAudioFile);

    if (timeline)
        timeline->setAnalysis (currentAnalysis);
}

bool MainComponent::loadURLIntoTransport (const juce::URL& audioURL)
{
    transportSource.stop();
    transportSource.setSource (nullptr);
    currentAudioFileSource.reset();

    const auto source = makeInputSource (audioURL);

    if (source == nullptr)
        return false;

    auto stream = juce::rawToUniquePtr (source->createInputStream());

    if (stream == nullptr)
        return false;

    auto reader = juce::rawToUniquePtr (formatManager.createReaderFor (std::move (stream)));

    if (reader == nullptr)
        return false;

    currentAudioFileSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);

    transportSource.setSource (currentAudioFileSource.get(),
                               32768,
                               &thread,
                               currentAudioFileSource->getAudioFormatReader()->sampleRate);

    return true;
}

void MainComponent::startOrStop()
{
    if (transportSource.isPlaying())
    {
        transportSource.stop();
    }
    else
    {
        transportSource.setPosition (0);
        transportSource.start();
    }
}

void MainComponent::updateFollowTransportState()
{
    if (timeline)
        timeline->setFollowsTransport (followTransportButton.getToggleState());
}

void MainComponent::openAudioFile()
{
    try
    {
        if (fileChooser == nullptr)
        {
            fileChooser = std::make_unique<juce::FileChooser> ("Select a JSON file...",
                                                               juce::File(),
                                                               "*.json");

            fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this] (const juce::FileChooser& fc) mutable
                                      {
                                          try
                                          {
                                              if (fc.getURLResults().size() > 0)
                                              {
                                                  auto u = fc.getURLResult();
                                                  showAudioResource (std::move (u));
                                              }
                                          }
                                          catch (const std::exception& e) { juce::Logger::writeToLog ("openAudioFile.launchAsync callback: " + juce::String (e.what())); }
                                          catch (...) { juce::Logger::writeToLog ("openAudioFile.launchAsync callback: unknown exception"); }

                                          fileChooser = nullptr;
                                      });
        }
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("MainComponent::openAudioFile: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("MainComponent::openAudioFile: unknown exception"); }
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    try
    {
        if (source == timeline.get())
            showAudioResource (juce::URL (timeline->getLastDroppedFile()));
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("MainComponent::changeListenerCallback: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("MainComponent::changeListenerCallback: unknown exception"); }
}

void MainComponent::updateStatus()
{
    try
    {
        if (!currentAudioFile.isEmpty())
        {
            auto position = transportSource.getCurrentPosition();
            juce::String text;

            if (currentAnalysis.bpm > 0)
            {
                text += juce::String::formatted ("BPM: %.0f", currentAnalysis.bpm);

                const Segment* seg = currentAnalysis.segmentAt (position);
                if (seg != nullptr)
                    text += " • " + seg->label;

                int barNum = currentAnalysis.barNumberAt (position);
                if (barNum > 0)
                    text += juce::String::formatted (" • Bar %d", barNum);
            }
            else
            {
                text = juce::String::formatted ("Time: %.2f s", position);
            }

            statusLabel.setText (text, juce::dontSendNotification);
        }
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("MainComponent::updateStatus: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("MainComponent::updateStatus: unknown exception"); }
}
