#include "TimelineComponent.h"

TimelineComponent::TimelineComponent (juce::AudioFormatManager& formatManager,
                                      juce::AudioTransportSource& source,
                                      juce::Slider& slider)
    : transportSource (source),
      zoomSlider (slider),
      thumbnail (512, formatManager, thumbnailCache)
{
    thumbnail.addChangeListener (this);

    addAndMakeVisible (scrollbar);
    scrollbar.setRangeLimits (visibleRange);
    scrollbar.setAutoHide (false);
    scrollbar.addListener (this);

    currentPositionMarker.setFill (juce::Colours::white.withAlpha (0.85f));
    addAndMakeVisible (currentPositionMarker);
}

TimelineComponent::~TimelineComponent()
{
    scrollbar.removeListener (this);
    thumbnail.removeChangeListener (this);
}

void TimelineComponent::setURL (const juce::URL& url)
{
    if (auto inputSource = makeInputSource (url))
    {
        thumbnail.setSource (inputSource.release());

        juce::Range<double> newRange (0.0, thumbnail.getTotalLength());
        scrollbar.setRangeLimits (newRange);
        setRange (newRange);

        startTimerHz (40);
    }
}

void TimelineComponent::setArrangement (const Arrangement& newArrangement)
{
    arrangement = newArrangement;
    repaint();
}

void TimelineComponent::setZoomFactor (double amount)
{
    if (thumbnail.getTotalLength() > 0)
    {
        auto newScale = juce::jmax (0.001, thumbnail.getTotalLength() * (1.0 - juce::jlimit (0.0, 0.99, amount)));
        auto timeAtCentre = xToTime ((float) getWidth() / 2.0f);

        setRange ({ timeAtCentre - newScale * 0.5, timeAtCentre + newScale * 0.5 });
    }
}

void TimelineComponent::setRange (juce::Range<double> newRange)
{
    visibleRange = newRange;
    scrollbar.setCurrentRange (visibleRange);
    updateCursorPosition();
    repaint();
}

void TimelineComponent::setFollowsTransport (bool shouldFollow)
{
    isFollowingTransport = shouldFollow;
}

void TimelineComponent::paint (juce::Graphics& g)
{
    try
    {
        g.fillAll (juce::Colours::darkgrey);

        auto thumbArea = getLocalBounds();
        thumbArea.removeFromBottom (scrollbar.getHeight() + 4);

        if (thumbnail.getTotalLength() > 0.0)
        {
            for (const auto& segment : arrangement.segments)
            {
                if (segment.end <= visibleRange.getStart() || segment.start >= visibleRange.getEnd())
                    continue;

                float x1 = timeToX (segment.start);
                float x2 = timeToX (segment.end);
                x1 = juce::jlimit (0.0f, (float)thumbArea.getWidth(), x1);
                x2 = juce::jlimit (0.0f, (float)thumbArea.getWidth(), x2);

                juce::Colour segColour = arrangement.colourForLabel (segment.label);
                g.setColour (segColour.withAlpha (0.25f));
                g.fillRect (x1, (float)thumbArea.getY(), x2 - x1, (float)thumbArea.getHeight());

                if (x2 - x1 > 50.0f)
                {
                    g.setColour (juce::Colours::white);
                    g.setFont (11.0f);
                    g.drawText (segment.label,
                               juce::Rectangle<int> ((int)x1 + 2, thumbArea.getY() + 2, (int)(x2 - x1 - 4), 20),
                               juce::Justification::topLeft, true);
                }
            }

            g.setColour (juce::Colours::lightblue);
            thumbnail.drawChannels (g, thumbArea.reduced (2),
                                   visibleRange.getStart(), visibleRange.getEnd(), 1.0f);

            int barNumber = 1;
            for (const auto& downbeat : arrangement.downbeats)
            {
                if (downbeat < visibleRange.getStart() || downbeat > visibleRange.getEnd())
                {
                    if (downbeat > visibleRange.getEnd())
                        break;
                    barNumber++;
                    continue;
                }

                float xPos = timeToX (downbeat);

                g.setColour (juce::Colours::white.withAlpha (0.4f));
                g.drawVerticalLine ((int)xPos, (float) thumbArea.getY(), (float) thumbArea.getBottom());

                float pixelsPerBar = timeToX (downbeat + 1.0) - xPos;
                if (pixelsPerBar > 30.0f)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.6f));
                    g.setFont (10.0f);
                    g.drawText (juce::String (barNumber),
                               juce::Rectangle<int> ((int)xPos - 15, thumbArea.getBottom() - 15, 30, 12),
                               juce::Justification::centred, false);
                }

                barNumber++;
            }
        }
        else
        {
            g.setColour (juce::Colours::white);
            g.setFont (14.0f);
            g.drawFittedText ("(No JSON file selected)", getLocalBounds(), juce::Justification::centred, 2);
        }
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("TimelineComponent::paint: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("TimelineComponent::paint: unknown exception"); }
}

void TimelineComponent::resized()
{
    try
    {
        scrollbar.setBounds (getLocalBounds().removeFromBottom (14).reduced (2));
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("TimelineComponent::resized: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("TimelineComponent::resized: unknown exception"); }
}

void TimelineComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    try
    {
        repaint();
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("TimelineComponent::changeListenerCallback: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("TimelineComponent::changeListenerCallback: unknown exception"); }
}

bool TimelineComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    try
    {
        if (files.size() > 0)
        {
            auto ext = juce::File (files[0]).getFileExtension().toLowerCase();
            return ext == ".json";
        }
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("TimelineComponent::isInterestedInFileDrag: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("TimelineComponent::isInterestedInFileDrag: unknown exception"); }
    return false;
}

void TimelineComponent::filesDropped (const juce::StringArray& files, int /*x*/, int /*y*/)
{
    try
    {
        lastFileDropped = juce::URL (juce::File (files[0]));
        sendChangeMessage();
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("TimelineComponent::filesDropped: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("TimelineComponent::filesDropped: unknown exception"); }
}

void TimelineComponent::mouseDown (const juce::MouseEvent& e)
{
    try
    {
        mouseDrag (e);
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("TimelineComponent::mouseDown: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("TimelineComponent::mouseDown: unknown exception"); }
}

void TimelineComponent::mouseDrag (const juce::MouseEvent& e)
{
    try
    {
        if (canMoveTransport())
            transportSource.setPosition (juce::jmax (0.0, xToTime ((float) e.x)));
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("TimelineComponent::mouseDrag: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("TimelineComponent::mouseDrag: unknown exception"); }
}

void TimelineComponent::mouseUp (const juce::MouseEvent&)
{
    try
    {
        transportSource.start();
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("TimelineComponent::mouseUp: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("TimelineComponent::mouseUp: unknown exception"); }
}

void TimelineComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    try
    {
        if (thumbnail.getTotalLength() > 0.0)
        {
            auto newStart = visibleRange.getStart() - wheel.deltaX * (visibleRange.getLength()) / 10.0;
            newStart = juce::jlimit (0.0, juce::jmax (0.0, thumbnail.getTotalLength() - (visibleRange.getLength())), newStart);

            if (canMoveTransport())
                setRange ({ newStart, newStart + visibleRange.getLength() });

            if (! juce::approximatelyEqual (wheel.deltaY, 0.0f))
                zoomSlider.setValue (zoomSlider.getValue() - wheel.deltaY);

            repaint();
        }
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("TimelineComponent::mouseWheelMove: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("TimelineComponent::mouseWheelMove: unknown exception"); }
}

float TimelineComponent::timeToX (const double time) const
{
    if (visibleRange.getLength() <= 0)
        return 0;

    return (float) getWidth() * (float) ((time - visibleRange.getStart()) / visibleRange.getLength());
}

double TimelineComponent::xToTime (const float x) const
{
    return (x / (float) getWidth()) * (visibleRange.getLength()) + visibleRange.getStart();
}

bool TimelineComponent::canMoveTransport() const noexcept
{
    return ! (isFollowingTransport && transportSource.isPlaying());
}

void TimelineComponent::scrollBarMoved (juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart)
{
    try
    {
        if (scrollBarThatHasMoved == &scrollbar)
            if (! (isFollowingTransport && transportSource.isPlaying()))
                setRange (visibleRange.movedToStartAt (newRangeStart));
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("TimelineComponent::scrollBarMoved: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("TimelineComponent::scrollBarMoved: unknown exception"); }
}

void TimelineComponent::timerCallback()
{
    try
    {
        if (canMoveTransport())
            updateCursorPosition();
        else
            setRange (visibleRange.movedToStartAt (transportSource.getCurrentPosition() - (visibleRange.getLength() / 2.0)));
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("TimelineComponent::timerCallback: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("TimelineComponent::timerCallback: unknown exception"); }
}

void TimelineComponent::updateCursorPosition()
{
    try
    {
        currentPositionMarker.setVisible (transportSource.isPlaying() || isMouseButtonDown());

        auto thumbArea = getLocalBounds();
        thumbArea.removeFromBottom (scrollbar.getHeight() + 4);

        currentPositionMarker.setRectangle (juce::Rectangle<float> (timeToX (transportSource.getCurrentPosition()) - 0.75f,
                                                                     (float)thumbArea.getY(),
                                                                     1.5f,
                                                                     (float)thumbArea.getHeight()));
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("TimelineComponent::updateCursorPosition: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("TimelineComponent::updateCursorPosition: unknown exception"); }
}
