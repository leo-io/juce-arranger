#include "MonoThumbnailComponent.h"
#include "DemoUtilities.h"

//==============================================================================
// DownmixWorker — TimeSliceClient that reads blocks, averages channels to mono,
// and feeds them into the shared mono AudioThumbnail.
//==============================================================================
void MonoThumbnailComponent::DownmixWorker::setReader (std::unique_ptr<juce::AudioFormatReader> newReader)
{
    reader = std::move (newReader);
    nextBlockStart = 0;
    totalSamples = reader != nullptr ? reader->lengthInSamples : 0;
}

std::unique_ptr<juce::AudioFormatReader> MonoThumbnailComponent::DownmixWorker::releaseReader()
{
    return std::move (reader);
}

int MonoThumbnailComponent::DownmixWorker::useTimeSlice()
{
    if (reader == nullptr || nextBlockStart >= totalSamples)
        return -1;

    const int numChannels = (int) reader->numChannels;
    constexpr int blockSize = 32768;
    const auto samplesRemaining = totalSamples - nextBlockStart;
    const int samplesToRead = (int) juce::jmin ((juce::int64) blockSize, samplesRemaining);

    // Read all channels into a temporary multi-channel buffer
    juce::AudioBuffer<float> readBuffer (numChannels, samplesToRead);
    std::vector<float*> channelPtrs ((size_t) numChannels);

    for (int ch = 0; ch < numChannels; ++ch)
        channelPtrs[(size_t) ch] = readBuffer.getWritePointer (ch);

    reader->read (channelPtrs.data(), numChannels, nextBlockStart, samplesToRead);

    // Downmix to a 1-channel buffer: (sum of all channels) / numChannels
    juce::AudioBuffer<float> monoBuffer (1, samplesToRead);
    auto* dest = monoBuffer.getWritePointer (0);

    if (numChannels == 1)
    {
        juce::FloatVectorOperations::copy (dest, readBuffer.getReadPointer (0), samplesToRead);
    }
    else
    {
        // Start with channel 0
        juce::FloatVectorOperations::copy (dest, readBuffer.getReadPointer (0), samplesToRead);
        // Accumulate remaining channels
        for (int ch = 1; ch < numChannels; ++ch)
            juce::FloatVectorOperations::add (dest, readBuffer.getReadPointer (ch), samplesToRead);
        // Average
        juce::FloatVectorOperations::multiply (dest, 1.0f / (float) numChannels, samplesToRead);
    }

    monoThumb.addBlock (nextBlockStart, monoBuffer, 0, samplesToRead);
    nextBlockStart += samplesToRead;

    return 0; // request another slice immediately
}

//==============================================================================
// MonoThumbnailComponent
//==============================================================================
MonoThumbnailComponent::MonoThumbnailComponent (juce::AudioFormatManager& fmtMgr,
                                                juce::AudioTransportSource& transport,
                                                juce::TimeSliceThread& workerThread)
    : formatManager (fmtMgr),
      transportSource (transport),
      worker (workerThread),
      monoThumb (512, fmtMgr, cache),
      downmixWorker (monoThumb)
{
    monoThumb.addChangeListener (this);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (40);
}

MonoThumbnailComponent::~MonoThumbnailComponent()
{
    stopTimer();
    worker.removeTimeSliceClient (&downmixWorker);
    downmixWorker.releaseReader();
    monoThumb.removeChangeListener (this);
}

void MonoThumbnailComponent::setSource (const juce::URL& url)
{
    // Stop any in-flight downmix before touching the reader / thumbnail
    worker.removeTimeSliceClient (&downmixWorker);
    downmixWorker.releaseReader();

    // Try to open the audio file through the format manager
    if (auto inputSource = makeInputSource (url))
    {
        if (auto stream = inputSource->createInputStream())
        {
            if (auto* reader = formatManager.createReaderFor (std::unique_ptr<juce::InputStream> (stream)))
            {
                const auto sampleRate  = reader->sampleRate;
                const auto totalSamps  = reader->lengthInSamples;
                totalLength = (double) totalSamps / sampleRate;

                monoThumb.reset (1, sampleRate, totalSamps);
                downmixWorker.setReader (std::unique_ptr<juce::AudioFormatReader> (reader));
                worker.addTimeSliceClient (&downmixWorker);
                repaint();
                return;
            }
        }
    }

    // Fallback — empty / unreadable file
    totalLength = 0.0;
    monoThumb.reset (1, 44100.0, 0);
    repaint();
}

void MonoThumbnailComponent::setAnalysis (const SongAnalysis& newAnalysis)
{
    analysis = newAnalysis;
    repaint();
}

void MonoThumbnailComponent::setHighlightRange (juce::Range<double> timeRange)
{
    highlight = timeRange;
    repaint();
}

void MonoThumbnailComponent::clearHighlight()
{
    highlight = {};
    repaint();
}

//==============================================================================
// Paint
//==============================================================================
void MonoThumbnailComponent::paint (juce::Graphics& g)
{
    try
    {
        g.fillAll (Palette::surface);

        const auto bounds = getLocalBounds().reduced (Spacing::pad);

        if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
            return;

        // ---- Empty state ----
        if (totalLength <= 0.0)
        {
            g.setColour (Palette::textSecondary);
            g.setFont (14.0f);
            g.drawFittedText ("(No audio file loaded)", getLocalBounds(),
                              juce::Justification::centred, 2);
            return;
        }

        // ---- Waveform (downmixed mono channel) ----
        g.setColour (Palette::waveform);
        monoThumb.drawChannel (g, bounds, 0.0, totalLength, 0, 1.0f);

        // ---- Cross-highlight overlay ----
        if (! highlight.isEmpty())
        {
            const auto sx = timeToX (highlight.getStart());
            const auto ex = timeToX (highlight.getEnd());
            g.setColour (Palette::accent.withAlpha (0.18f));
            g.fillRect ((float) sx, (float) bounds.getY(),
                        (float) (ex - sx), (float) bounds.getHeight());
        }

        // ---- Downbeat ticks + sparse bar labels ----
        {
            int barNum = 1;
            for (size_t i = 0; i < analysis.downbeats.size(); ++i)
            {
                const double db = analysis.downbeats[i];
                if (db < 0.0 || db > totalLength)
                {
                    ++barNum;
                    continue;
                }

                const auto xPos = (float) timeToX (db);

                // Vertical tick
                g.setColour (Palette::divider);
                g.drawVerticalLine ((int) xPos, (float) bounds.getY(),
                                    (float) bounds.getBottom());

                // Sparse label — show every 4th bar (1, 5, 9, …)
                if ((barNum % 4) == 1 || barNum == 1)
                {
                    float pixelsPerBar = 30.0f;
                    if (i + 1 < analysis.downbeats.size())
                    {
                        const double nextDb = analysis.downbeats[i + 1];
                        pixelsPerBar = (float) (timeToX (nextDb) - xPos);
                    }
                    else if (analysis.downbeats.size() > 1)
                    {
                        const double avgInterval = (analysis.downbeats.back()
                                                    - analysis.downbeats.front())
                                                   / (analysis.downbeats.size() - 1);
                        pixelsPerBar = (float) (timeToX (db + avgInterval) - xPos);
                    }

                    if (pixelsPerBar > 30.0f)
                    {
                        g.setColour (Palette::textSecondary);
                        g.setFont (10.0f);
                        g.drawText (juce::String (barNum),
                                    juce::Rectangle<int> ((int) xPos - 15,
                                                          bounds.getBottom() - 15,
                                                          30, 12),
                                    juce::Justification::centred, false);
                    }
                }

                ++barNum;
            }
        }

        // ---- Loading / progress affordance ----
        if (! monoThumb.isFullyLoaded())
        {
            const auto progress = monoThumb.getProportionComplete();
            if (progress >= 0.0f && progress < 1.0f)
            {
                auto progressBounds = bounds;
                g.setColour (Palette::accent.withAlpha (0.6f));
                g.setFont (11.0f);
                g.drawFittedText (juce::String::formatted ("Loading waveform\u2026 %d%%",
                                                            juce::roundToInt (progress * 100.0f)),
                                  progressBounds.removeFromTop (20), juce::Justification::topRight, 1);
            }
        }

        // ---- Play cursor ----
        if (currentPlayPosition >= 0.0 && currentPlayPosition <= totalLength)
        {
            const auto cursorX = (float) timeToX (currentPlayPosition);
            g.setColour (Palette::textPrimary);
            g.drawVerticalLine ((int) cursorX, (float) bounds.getY(),
                                (float) bounds.getBottom());
        }
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog ("MonoThumbnailComponent::paint: " + juce::String (e.what()));
    }
    catch (...)
    {
        juce::Logger::writeToLog ("MonoThumbnailComponent::paint: unknown exception");
    }
}

void MonoThumbnailComponent::resized()
{
    repaint();
}

//==============================================================================
// ChangeListener — thumbnail has new data available
//==============================================================================
void MonoThumbnailComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    try
    {
        repaint();
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog ("MonoThumbnailComponent::changeListenerCallback: "
                                  + juce::String (e.what()));
    }
    catch (...)
    {
        juce::Logger::writeToLog ("MonoThumbnailComponent::changeListenerCallback: unknown exception");
    }
}

//==============================================================================
// Mouse — click-to-seek with downbeat snapping
//==============================================================================
void MonoThumbnailComponent::mouseDown (const juce::MouseEvent& e)
{
    try
    {
        if (totalLength <= 0.0)
            return;

        const double time = juce::jlimit (0.0, totalLength, xToTime ((float) e.x));
        transportSource.setPosition (snapToDownbeat (time));
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog ("MonoThumbnailComponent::mouseDown: " + juce::String (e.what()));
    }
    catch (...)
    {
        juce::Logger::writeToLog ("MonoThumbnailComponent::mouseDown: unknown exception");
    }
}

void MonoThumbnailComponent::mouseUp (const juce::MouseEvent&)
{
    // Single click seeks only — does NOT auto-start (double-click does).
}

void MonoThumbnailComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    try
    {
        if (totalLength <= 0.0)
            return;

        const double time = juce::jlimit (0.0, totalLength, xToTime ((float) e.x));
        transportSource.setPosition (snapToDownbeat (time));
        transportSource.start();
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog ("MonoThumbnailComponent::mouseDoubleClick: "
                                  + juce::String (e.what()));
    }
    catch (...)
    {
        juce::Logger::writeToLog ("MonoThumbnailComponent::mouseDoubleClick: unknown exception");
    }
}

//==============================================================================
// Timer (40 Hz) — updates play-cursor position
//==============================================================================
void MonoThumbnailComponent::timerCallback()
{
    try
    {
        if (transportSource.isPlaying() || isMouseButtonDown())
        {
            currentPlayPosition = transportSource.getCurrentPosition();
            repaint();
        }
        else
        {
            // Keep position visible for a moment after playback stops
            if (currentPlayPosition >= 0.0)
            {
                currentPlayPosition = transportSource.getCurrentPosition();
                repaint();
            }
        }
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog ("MonoThumbnailComponent::timerCallback: " + juce::String (e.what()));
    }
    catch (...)
    {
        juce::Logger::writeToLog ("MonoThumbnailComponent::timerCallback: unknown exception");
    }
}

//==============================================================================
// Coordinate helpers
//==============================================================================
double MonoThumbnailComponent::timeToX (double time) const noexcept
{
    const auto bounds = getLocalBounds().reduced (Spacing::pad);

    if (totalLength <= 0.0 || bounds.getWidth() <= 0)
        return 0.0;

    return (double) bounds.getX()
           + (time / totalLength) * (double) bounds.getWidth();
}

double MonoThumbnailComponent::xToTime (float x) const noexcept
{
    const auto bounds = getLocalBounds().reduced (Spacing::pad);

    if (totalLength <= 0.0 || bounds.getWidth() <= 0)
        return 0.0;

    const double proportion = ((double) x - (double) bounds.getX())
                              / (double) bounds.getWidth();
    return juce::jlimit (0.0, totalLength, proportion * totalLength);
}

double MonoThumbnailComponent::snapToDownbeat (double time) const noexcept
{
    if (analysis.downbeats.empty())
        return time;

    double best = analysis.downbeats[0];
    double bestDist = std::abs (time - best);

    for (const auto& db : analysis.downbeats)
    {
        const double dist = std::abs (time - db);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = db;
        }
    }

    return best;
}
