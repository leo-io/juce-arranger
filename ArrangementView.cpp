#include "ArrangementView.h"

//==============================================================================
// Constructor

ArrangementView::ArrangementView (juce::AudioFormatManager& formatManager,
                                  juce::AudioTransportSource& transport,
                                  juce::TimeSliceThread& worker)
    : transportSource (transport)
{
    (void) formatManager;
    (void) worker;

    // Build grid and place inside viewport (non-owning — unique_ptr owns the grid)
    grid = std::make_unique<BarGridComponent> (model, transportSource);
    gridViewport.setViewedComponent (grid.get(), false);
    gridViewport.setScrollBarsShown (true, false);  // vertical only
    addAndMakeVisible (gridViewport);

    // Listen for grid selection/hover changes
    grid->addChangeListener (this);

    // Zoom chip label
    addAndMakeVisible (zoomChip);
    zoomChip.setFont (juce::FontOptions (11.0f, juce::Font::plain));
    zoomChip.setJustificationType (juce::Justification::centred);
    zoomChip.setEditable (false, false, false);
    zoomChip.setColour (juce::Label::backgroundColourId, Palette::zoomChipBg);
    zoomChip.setColour (juce::Label::textColourId, Palette::textSecondary);
    zoomChip.setText ("Zoom: Segment", juce::dontSendNotification);

    // Wire grid zoom request (Ctrl+wheel, keyboard)
    grid->onZoomRequest = [this] (int delta, int anchorBar)
    {
        try
        {
            int newLevel = zoomLevel + delta;
            // Handle the Ctrl+0 "reset to arrangement overview" case
            if (delta <= -100)
                newLevel = 0;
            newLevel = juce::jlimit (0, maxZoomLevel, newLevel);
            if (newLevel != zoomLevel)
            {
                auto viewArea = gridViewport.getViewArea();
                int cursorYInView = viewArea.getHeight() / 2; // fallback: middle of view
                zoomAroundAnchor (newLevel, anchorBar, cursorYInView);
                sendChangeMessage();
            }
        }
        catch (const std::exception& e) { juce::Logger::writeToLog ("ArrangementView::onZoomRequest: " + juce::String (e.what())); }
        catch (...) { juce::Logger::writeToLog ("ArrangementView::onZoomRequest: unknown exception"); }
    };

    addAndMakeVisible (selectionReadout);
    selectionReadout.setFont (juce::FontOptions (12.0f, juce::Font::plain));
    selectionReadout.setJustificationType (juce::Justification::centredRight);
    selectionReadout.setEditable (false, false, false);
    selectionReadout.setText ("Selection: none", juce::dontSendNotification);
    selectionReadout.setColour (juce::Label::textColourId, Palette::textSecondary);

    // Set initial zoom
    setZoomLevel (1);

    // Start 40 Hz timer for follow-transport
    startTimerHz (40);
}

//==============================================================================
// Destructor

ArrangementView::~ArrangementView()
{
    stopTimer();

    if (grid)
    {
        grid->removeChangeListener (this);
        gridViewport.setViewedComponent (nullptr, false);
    }
}

//==============================================================================
// Public API

void ArrangementView::setURL (const juce::URL& url)
{
    // Store the filename as a fallback; overridden by arrangement.name in setArrangement
    arrangementName = url.getLocalFile().getFileNameWithoutExtension();
    repaint();
}

void ArrangementView::setArrangement (const Arrangement& newArrangement)
{
    arrangement = newArrangement;

    // Prefer the name embedded in the JSON; keep the filename fallback set in setURL
    if (!arrangement.name.isEmpty())
        arrangementName = arrangement.name;

    model.rebuild (arrangement, transportSource.getLengthInSeconds());
    model.applyZoom (arrangement, zoomLevel, transportSource.getLengthInSeconds());

    maxZoomLevel = model.maxZoomLevel (arrangement);
    zoomLevel = juce::jmin (zoomLevel, maxZoomLevel);

    if (grid)
        grid->refresh();

    updateZoomControls();
    layoutGrid();
    repaint();
}

void ArrangementView::setZoomLevel (int level)
{
    level = juce::jlimit (0, maxZoomLevel, level);

    if (zoomLevel == level)
        return;

    zoomLevel = level;

    model.applyZoom (arrangement, zoomLevel, transportSource.getLengthInSeconds());

    if (grid)
        grid->refresh();

    updateZoomControls();
    layoutGrid();
    repaint();
}

juce::Range<int> ArrangementView::getSelection() const
{
    if (model.selection.has_value())
        return model.selection.value();

    return juce::Range<int> (0, 0);
}

//==============================================================================
// zoomAroundAnchor — change zoom level keeping a reference bar visually stable

void ArrangementView::zoomAroundAnchor (int newLevel, int anchorBar, int /*cursorYInView*/)
{
    if (newLevel == zoomLevel)
        return;

    // Record old anchor position
    auto oldBounds = model.cellBounds (anchorBar, gridViewport.getMaximumVisibleWidth());
    auto viewArea = gridViewport.getViewArea();
    int anchorScreenY = oldBounds.getY() - viewArea.getY();

    // Apply new zoom level
    newLevel = juce::jlimit (0, maxZoomLevel, newLevel);
    zoomLevel = newLevel;

    model.applyZoom (arrangement, zoomLevel, transportSource.getLengthInSeconds());

    if (grid)
        grid->refresh();

    updateZoomControls();
    layoutGrid();

    // Compute new view position to keep anchor bar at similar screen Y
    auto newBounds = model.cellBounds (anchorBar, gridViewport.getMaximumVisibleWidth());
    int newY = newBounds.getY() - anchorScreenY;
    gridViewport.setViewPosition (0, juce::jmax (0, newY));
    lastSetViewY = juce::jmax (0, newY);

    repaint();
}

//==============================================================================
// Paint

void ArrangementView::paint (juce::Graphics& g)
{
    try
    {
        g.fillAll (Palette::surface);

        auto bounds = getLocalBounds();

        // --- Header band ---
        auto headerBounds = bounds.removeFromTop (Spacing::header);

        g.setColour (Palette::surfaceElevated);
        g.fillRect (headerBounds);

        // Left: "ARRANGEMENT · "arrangement_name""
        auto leftHeader = headerBounds.removeFromLeft (headerBounds.getWidth() / 2);
        g.setColour (Palette::textPrimary);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        juce::String headerText = "ARRANGEMENT";
        if (! arrangementName.isEmpty())
            headerText += " \u00b7 \"" + arrangementName + "\"";
        g.drawText (headerText, leftHeader.reduced (Spacing::pad, 0),
                    juce::Justification::centredLeft, true);

        // Right: time signature | BPM | bar count | colour legend
        auto rightHeader = headerBounds.reduced (Spacing::pad, 0);

        // Time signature
        int beatsPerBar = 4;
        if (!arrangement.beatPositions.empty())
        {
            int maxBeatPos = 0;
            for (int pos : arrangement.beatPositions)
                maxBeatPos = juce::jmax (maxBeatPos, pos);
            beatsPerBar = maxBeatPos + 1;
        }
        juce::String infoText = juce::String (beatsPerBar) + "/4";

        // BPM
        if (arrangement.bpm > 0.0)
            infoText += juce::String::formatted ("   %.0f BPM", arrangement.bpm);

        // Bar count
        int barCount = (int) model.bars.size();
        infoText += "   " + juce::String (barCount) + " bar"
                     + (barCount != 1 ? "s" : "");

        // Draw info text
        auto infoArea = rightHeader.removeFromRight (rightHeader.getWidth() / 2);
        g.setColour (Palette::textSecondary);
        g.setFont (juce::FontOptions (11.0f, juce::Font::plain));
        g.drawText (infoText, infoArea, juce::Justification::centredRight, true);

        // Colour legend — collect unique segment labels
        auto legendArea = rightHeader;
        if (!arrangement.segments.empty())
        {
            std::vector<juce::String> uniqueLabels;
            for (const auto& seg : arrangement.segments)
            {
                if (! seg.label.isEmpty())
                {
                    bool found = false;
                    for (const auto& existing : uniqueLabels)
                    {
                        if (existing == seg.label)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (! found)
                        uniqueLabels.push_back (seg.label);
                }
            }

            if (! uniqueLabels.empty())
            {
                // Draw colour swatches + labels from right to left
                constexpr int swatchSize = 10;
                constexpr int swatchGap = 4;
                int totalSwatchWidth = (int)uniqueLabels.size() * (swatchSize + swatchGap) - swatchGap;

                int legendX = legendArea.getRight() - totalSwatchWidth;
                if (legendX < legendArea.getX())
                    legendX = legendArea.getX();

                for (size_t i = 0; i < uniqueLabels.size(); ++i)
                {
                    const auto& label = uniqueLabels[i];
                    juce::Colour swatchColour = arrangement.colourForLabel (label);

                    auto swatchRect = juce::Rectangle<int> (legendX,
                                                            legendArea.getCentreY() - swatchSize / 2,
                                                            swatchSize, swatchSize);
                    g.setColour (swatchColour);
                    g.fillRect (swatchRect);

                    if (swatchRect.getRight() + 2 + 40 < legendArea.getRight())
                    {
                        g.setColour (Palette::textSecondary);
                        g.setFont (juce::FontOptions (9.0f, juce::Font::plain));
                        g.drawText (label, swatchRect.translated (swatchSize + 2, 0).withWidth (40),
                                    juce::Justification::centredLeft, true);
                    }

                    legendX += swatchSize + swatchGap;
                }
            }
        }
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("ArrangementView::paint: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("ArrangementView::paint: unknown exception"); }
}

//==============================================================================
// Resized

void ArrangementView::resized()
{
    try
    {
        auto r = getLocalBounds();

        // 1. Header
        r.removeFromTop (Spacing::header);

        // 2. Toolbar
        auto toolbarBounds = r.removeFromTop (Spacing::toolbar).reduced (Spacing::pad, 0);

        // Toolbar left: zoom chip
        const int chipWidth = 130;
        const int chipHeight = 20;
        auto chipArea = toolbarBounds.removeFromLeft (chipWidth);
        chipArea.removeFromTop ((toolbarBounds.getHeight() - chipHeight) / 2);
        zoomChip.setBounds (chipArea.removeFromLeft (chipWidth));

        // Toolbar right: selection readout
        selectionReadout.setBounds (toolbarBounds);

        // 3. Viewport (remaining space)
        gridViewport.setBounds (r);
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("ArrangementView::resized: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("ArrangementView::resized: unknown exception"); }

    layoutGrid();
}

//==============================================================================
// Layout grid — recompute content width so viewport scrolls vertically only

void ArrangementView::layoutGrid()
{
    try
    {
        auto w = gridViewport.getMaximumVisibleWidth();
        if (w > 0 && grid)
            grid->layoutForWidth (w);
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("ArrangementView::layoutGrid: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("ArrangementView::layoutGrid: unknown exception"); }
}

//==============================================================================
// changeListenerCallback — react to grid selection/hover changes

void ArrangementView::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    try
    {
        if (source == grid.get())
        {
            updateSelectionReadout();
            sendChangeMessage();  // re-broadcast to MainComponent
        }
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("ArrangementView::changeListenerCallback: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("ArrangementView::changeListenerCallback: unknown exception"); }
}

//==============================================================================
// Selection readout

void ArrangementView::updateSelectionReadout()
{
    try
    {
        if (model.selection.has_value() && ! model.bars.empty())
        {
            auto sel = model.selection.value();
            int firstBar = sel.getStart();
            int lastBar = sel.getEnd() - 1;

            if (firstBar >= 0 && firstBar < (int)model.bars.size()
                && lastBar >= 0 && lastBar < (int)model.bars.size())
            {
                auto timeRange = model.selectionTimeRange();
                double duration = timeRange.getLength();

                selectionReadout.setText (
                    juce::String::formatted ("Selection: %d\u2013%d (%.1f s)",
                                             model.bars[firstBar].index1Based,
                                             model.bars[lastBar].index1Based,
                                             duration),
                    juce::dontSendNotification);
                return;
            }
        }

        selectionReadout.setText ("Selection: none", juce::dontSendNotification);
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("ArrangementView::updateSelectionReadout: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("ArrangementView::updateSelectionReadout: unknown exception"); }
}

//==============================================================================
// Timer (40 Hz) — follow-transport and playhead

void ArrangementView::timerCallback()
{
    try
    {
        double pos = transportSource.getCurrentPosition();

        // Always push playhead position (shows dim cursor when stopped)
        if (grid)
            grid->setPlayheadPosition (pos);

        // Detect manual user scroll by comparing viewport Y with our last set value
        int currentViewY = gridViewport.getViewPosition().y;
        if (currentViewY != lastSetViewY && !transportSource.isPlaying())
        {
            lastUserScrollMs = juce::Time::getMillisecondCounter();
            lastSetViewY = currentViewY;
        }

        if (! followTransport || ! transportSource.isPlaying())
        {
            // Clear playing bar highlight if stopped
            if (grid && grid->getPlayingBar().has_value())
                grid->setPlayingBar (std::nullopt);
            return;
        }

        int barNum = arrangement.barNumberAt (pos);  // 1-based

        if (barNum > 0 && barNum <= (int)model.bars.size())
        {
            int barIndex = barNum - 1;  // 0-based

            // Update playing bar highlight only when bar changes
            if (grid && grid->getPlayingBar() != barIndex)
                grid->setPlayingBar (barIndex);

            // Auto-scroll with look-ahead and user-scroll suppression
            auto now = juce::Time::getMillisecondCounter();
            if (now - lastUserScrollMs < 750)
                return;

            auto cellBounds = model.cellBounds (barIndex, gridViewport.getMaximumVisibleWidth());
            auto viewBounds = gridViewport.getViewArea();

            if (! viewBounds.intersects (cellBounds))
            {
                // Position playing row ~⅓ from top (look-ahead)
                int viewH = viewBounds.getHeight();
                int newY = cellBounds.getY() - viewH / 3;
                newY = juce::jmax (0, newY);

                gridViewport.setViewPosition (0, newY);
                lastSetViewY = newY;
            }
        }
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("ArrangementView::timerCallback: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("ArrangementView::timerCallback: unknown exception"); }
}

//==============================================================================
// Zoom controls — enable/disable stepper and update chip label

void ArrangementView::updateZoomControls()
{
    zoomChip.setText ("Zoom: " + BarGridModel::zoomLevelName (zoomLevel, maxZoomLevel),
                      juce::dontSendNotification);
}
