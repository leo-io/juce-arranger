#include "BarGridComponent.h"

//==============================================================================
// Constructor

BarGridComponent::BarGridComponent (BarGridModel& model, juce::AudioTransportSource& transport)
    : model (model), transportSource (transport)
{
    setWantsKeyboardFocus (true);
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
    setTitle ("Bar selection grid");
    updateAccessibilityDescription();
}

//==============================================================================
// Layout and sizing

void BarGridComponent::layoutForWidth (int viewportWidth)
{
    try
    {
        contentWidth = viewportWidth;
        if (contentWidth <= 0)
            contentWidth = 1;

        setSize (contentWidth, juce::jmax (Spacing::rowHeight, model.requiredHeight()));
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("BarGridComponent::layoutForWidth: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("BarGridComponent::layoutForWidth: unknown exception"); }
}

void BarGridComponent::refresh()
{
    try
    {
        setSize (contentWidth, juce::jmax (Spacing::rowHeight, model.requiredHeight()));
        updateAccessibilityDescription();
        repaint();
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("BarGridComponent::refresh: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("BarGridComponent::refresh: unknown exception"); }
}

//==============================================================================
// setPlayingBar — used by ArrangementView follow-transport

void BarGridComponent::setPlayingBar (std::optional<int> barIndex)
{
    try
    {
        if (barIndex.has_value())
        {
            int clamped = juce::jlimit (0, (int)model.bars.size() - 1, barIndex.value());
            if (clamped != barIndex.value())
                barIndex = clamped;
        }

        if (playingBar != barIndex)
        {
            playingBar = barIndex;
            repaint();
        }
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("BarGridComponent::setPlayingBar: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("BarGridComponent::setPlayingBar: unknown exception"); }
}

//==============================================================================
// setPlayheadPosition — smooth transport position for playhead line

void BarGridComponent::setPlayheadPosition (std::optional<double> timeSeconds)
{
    try
    {
        if (playheadTime != timeSeconds)
        {
            // Save old rect for targeted repaint
            auto oldRect = playheadCellRect (contentWidth);

            playheadTime = timeSeconds;

            // Repaint old + new playhead locations
            auto newRect = playheadCellRect (contentWidth);
            auto dirtyRect = oldRect.getUnion (newRect);
            if (!dirtyRect.isEmpty())
                repaint (dirtyRect.expanded (Spacing::playheadWidth + 2, 0));
            else
                repaint();
        }
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("BarGridComponent::setPlayheadPosition: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("BarGridComponent::setPlayheadPosition: unknown exception"); }
}

//==============================================================================
// playheadCellRect — bounding rect of the playhead line for dirty-rect repaint

juce::Rectangle<int> BarGridComponent::playheadCellRect (int viewportWidth) const
{
    if (!playheadTime.has_value() || model.bars.empty())
        return {};

    double pos = playheadTime.value();

    for (size_t i = 0; i < model.bars.size(); ++i)
    {
        if (pos >= model.bars[i].startTime && pos < model.bars[i].endTime)
        {
            auto cellBounds = model.cellBounds ((int)i, viewportWidth);

            const auto& bar = model.bars[i];
            double barDuration = bar.endTime - bar.startTime;
            double fraction = (barDuration > 0.0) ? ((pos - bar.startTime) / barDuration) : 0.0;
            fraction = juce::jlimit (0.0, 1.0, fraction);

            int x = cellBounds.getX() + (int)(cellBounds.getWidth() * fraction);
            int lineW = (transportSource.isPlaying() ? Spacing::playheadWidth : 1);
            return { x - lineW - 2, cellBounds.getY(), lineW * 2 + 4, cellBounds.getHeight() };
        }
    }

    return {};
}

//==============================================================================
// Paint

void BarGridComponent::paint (juce::Graphics& g)
{
    try
    {
        // Fill background
        g.fillAll (Palette::surface);

        // Early exit if no bars
        if (model.bars.empty())
        {
            g.setColour (Palette::textPrimary);
            g.setFont (14.0f);
            g.drawFittedText ("No bar data", getLocalBounds(), juce::Justification::centred, 2);
            return;
        }

        bool isSegmentMode = (model.mode == GridMode::Segment);
        bool isWholeArrangement = (model.mode == GridMode::WholeArrangement);

        // Draw each bar cell
        for (size_t i = 0; i < model.bars.size(); ++i)
        {
            const auto& bar = model.bars[i];
            auto cellBounds = model.cellBounds ((int)i, contentWidth);
            auto segSpan = model.getSegmentSpanForBar ((int)i);

            // Fill cell with segment colour (40% alpha)
            g.setColour (bar.colour.withAlpha (0.40f));
            g.fillRect (cellBounds);

            bool isLargeSegment = segSpan.barCount >= 4;

            if (isWholeArrangement || isSegmentMode)
            {
                // No bar outlines or segment dividers — pure colour fills only
            }
            else
            {
                // Skip cell outlines for large segments (4+ bars)
                if (!isLargeSegment)
                {
                    g.setColour (Palette::divider);
                    g.drawRect (cellBounds, 1);
                }

                // Bold divider at segment boundaries
                if (bar.isSegmentStart)
                {
                    g.setColour (Palette::gridStrong);
                    g.fillRect (cellBounds.getX(), cellBounds.getY(),
                                Spacing::segmentDivider, cellBounds.getHeight());
                }
            }

            // Label colour: contrasting with the cell fill
            juce::Colour labelColour = bar.colour.withAlpha (0.40f).contrasting (0.6f);
            g.setColour (labelColour);

            // Determine label density: in Segment mode use per-row cell width
            int perCellWidth = cellBounds.getWidth();
            auto detail = model.labelDetailFor (perCellWidth);

            // Draw bar number based on density (never in WholeArrangement or Segment mode)
            bool drawNumber = false;
            if (!isWholeArrangement && !isSegmentMode)
            {
                if (detail == LabelDetail::NumberAndLabel || detail == LabelDetail::NumberOnly)
                {
                    drawNumber = true;
                }
                else if (detail == LabelDetail::SparseNumber)
                {
                    drawNumber = (bar.index1Based % 4 == 1);
                }
            }

            if (drawNumber)
            {
                g.setFont (isWholeArrangement ? 8.0f : 10.0f);
                auto numberBounds = cellBounds.reduced (2);
                g.drawText (juce::String (bar.index1Based),
                           numberBounds,
                           juce::Justification::topLeft, false);
            }

        }

        // Unified segment-label pass: one prominent label per segment per row, all modes
        g.setFont (Spacing::segmentLabelFont);
        for (size_t i = 0; i < model.bars.size(); ++i)
        {
            const auto& bar = model.bars[i];
            if (!bar.isSegmentStart && i != 0)
                continue;
            if (bar.segmentLabel.isEmpty())
                continue;

            // Walk forward to find the last bar of this segment on the same row
            auto startBounds = model.cellBounds ((int)i, contentWidth);
            int rowY = startBounds.getY();
            int runEnd = (int)i;

            for (size_t j = i + 1; j < model.bars.size(); ++j)
            {
                if (model.bars[j].isSegmentStart)
                    break;
                auto jBounds = model.cellBounds ((int)j, contentWidth);
                if (jBounds.getY() != rowY)
                    break;
                runEnd = (int)j;
            }

            auto endBounds = model.cellBounds (runEnd, contentWidth);
            auto runRect = juce::Rectangle<int> (startBounds.getX(), rowY,
                                                 endBounds.getRight() - startBounds.getX(),
                                                 startBounds.getHeight());

            juce::Colour labelColour = bar.colour.withAlpha (0.40f).contrasting (0.6f);
            g.setColour (labelColour);
            g.drawText (bar.segmentLabel, runRect.reduced (Spacing::pad, 0),
                        juce::Justification::centredLeft, true);

            // If the segment continues on further rows, emit a label on each subsequent row
            size_t k = (size_t)runEnd + 1;
            while (k < model.bars.size() && !model.bars[k].isSegmentStart)
            {
                auto kBounds = model.cellBounds ((int)k, contentWidth);
                int newRowY = kBounds.getY();
                int rowRunEnd = (int)k;

                for (size_t m = k + 1; m < model.bars.size(); ++m)
                {
                    if (model.bars[m].isSegmentStart)
                        break;
                    auto mBounds = model.cellBounds ((int)m, contentWidth);
                    if (mBounds.getY() != newRowY)
                        break;
                    rowRunEnd = (int)m;
                }

                auto rowEndBounds = model.cellBounds (rowRunEnd, contentWidth);
                auto rowRunRect = juce::Rectangle<int> (kBounds.getX(), newRowY,
                                                        rowEndBounds.getRight() - kBounds.getX(),
                                                        kBounds.getHeight());
                g.setColour (labelColour);
                g.drawText (bar.segmentLabel, rowRunRect.reduced (Spacing::pad, 0),
                            juce::Justification::centredLeft, true);

                k = (size_t)rowRunEnd + 1;
            }
        }

        // Hover overlay
        if (hoveredBar.has_value())
        {
            int hoverIdx = hoveredBar.value();
            if (hoverIdx >= 0 && hoverIdx < (int)model.bars.size())
            {
                auto cellBounds = model.cellBounds (hoverIdx, contentWidth);
                g.setColour (Palette::accent.withAlpha (0.15f));
                g.fillRect (cellBounds);
                g.setColour (Palette::accent);
                g.drawRect (cellBounds, 1);
            }
        }

        // Selection rendering: one outline per row-span, for the active range and
        // every additional multi-select range.
        auto drawRangeOutline = [&] (int firstBar, int lastBar)
        {
            // Group selected bars by row and draw per-row outline
            for (int row = 0; row < model.rowCount(); ++row)
            {
                int rowStart, rowEnd;
                if (isSegmentMode && !model.rowSpans.empty() && row < (int)model.rowSpans.size())
                {
                    rowStart = model.rowSpans[row].firstBar;
                    rowEnd = rowStart + model.rowSpans[row].barCount;
                }
                else
                {
                    rowStart = row * model.barsPerRow;
                    rowEnd = rowStart + model.barsPerRow;
                }

                int selStartInRow = juce::jmax (rowStart, firstBar);
                int selEndInRow = juce::jmin (rowEnd - 1, lastBar);

                if (selStartInRow <= selEndInRow && selStartInRow >= rowStart && selEndInRow < rowEnd)
                {
                    auto startBounds = model.cellBounds (selStartInRow, contentWidth);
                    auto endBounds = model.cellBounds (selEndInRow, contentWidth);

                    // Compute the union rectangle for this row's selected span
                    int left = startBounds.getX();
                    int top = startBounds.getY();
                    int right = endBounds.getRight();
                    int bottom = endBounds.getBottom();

                    juce::Rectangle<int> selRect (left, top, right - left, bottom - top);

                    // Draw rounded outline (2px accent stroke)
                    g.setColour (Palette::accent);
                    g.drawRect (selRect, 2);

                    // Optional: draw a faint inner glow
                    g.setColour (Palette::accent.withAlpha (0.2f));
                    g.drawRect (selRect.reduced (1), 1);
                }
            }
        };

        if (model.selection.has_value())
        {
            auto sel = model.selection.value();
            drawRangeOutline (sel.getStart(), sel.getEnd() - 1);
        }

        for (const auto& extra : model.extraSelections)
            drawRangeOutline (extra.getStart(), extra.getEnd() - 1);

        // Playing-bar highlight (follow-transport) — soft fill only, no left-edge bar
        if (playingBar.has_value() && transportSource.isPlaying())
        {
            int pbIdx = playingBar.value();
            if (pbIdx >= 0 && pbIdx < (int)model.bars.size())
            {
                auto cellBounds = model.cellBounds (pbIdx, contentWidth);
                // Subtle background fill to indicate "this row is live"
                g.setColour (Palette::accent.withAlpha (0.25f));
                g.fillRect (cellBounds);
            }
        }

        //==========================================================================
        // Playhead line + triangle cap (drawn last, above everything)
        //==========================================================================
        if (playheadTime.has_value())
        {
            double pos = playheadTime.value();

            for (size_t i = 0; i < model.bars.size(); ++i)
            {
                if (pos >= model.bars[i].startTime && pos < model.bars[i].endTime)
                {
                    const auto& bar = model.bars[i];
                    auto cellBounds = model.cellBounds ((int)i, contentWidth);

                    double barDuration = bar.endTime - bar.startTime;
                    double fraction = (barDuration > 0.0) ? ((pos - bar.startTime) / barDuration) : 0.0;
                    fraction = juce::jlimit (0.0, 1.0, fraction);

                    int x = cellBounds.getX() + (int)(cellBounds.getWidth() * fraction);

                    bool isPlaying = transportSource.isPlaying();
                    juce::Colour playheadColour = isPlaying ? Palette::playhead : Palette::playheadDim;
                    int lineWidth = isPlaying ? Spacing::playheadWidth : 1;

                    // Vertical line
                    g.setColour (playheadColour);
                    g.fillRect (x - lineWidth / 2, cellBounds.getY(), lineWidth, cellBounds.getHeight());

                    // Triangle cap at top (playing only)
                    if (isPlaying)
                    {
                        juce::Path cap;
                        float cx = (float)x;
                        float topY = (float)cellBounds.getY();
                        float halfBase = Spacing::playheadCap / 2.0f;
                        cap.addTriangle (cx - halfBase, topY,
                                         cx + halfBase, topY,
                                         cx,            topY + (float)Spacing::playheadCap);
                        g.fillPath (cap);
                    }
                    break;
                }
            }
        }

        // Focus ring: when this component has keyboard focus
        if (hasKeyboardFocus (true))
        {
            g.setColour (Palette::accent);
            g.drawRect (getLocalBounds(), 2);
        }
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("BarGridComponent::paint: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("BarGridComponent::paint: unknown exception"); }
}

//==============================================================================
// Mouse handlers

void BarGridComponent::mouseEnter (const juce::MouseEvent& e)
{
    try
    {
        mouseMove (e);
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("BarGridComponent::mouseEnter: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("BarGridComponent::mouseEnter: unknown exception"); }
}

void BarGridComponent::mouseMove (const juce::MouseEvent& e)
{
    try
    {
        auto newHovered = model.barIndexAtExact (e.getPosition(), contentWidth);
        if (newHovered != hoveredBar)
        {
            hoveredBar = newHovered;
            repaint();
            updateAccessibilityDescription();
            sendChangeMessage();
        }
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("BarGridComponent::mouseMove: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("BarGridComponent::mouseMove: unknown exception"); }
}

void BarGridComponent::mouseExit (const juce::MouseEvent&)
{
    try
    {
        if (hoveredBar.has_value())
        {
            hoveredBar.reset();
            repaint();
            updateAccessibilityDescription();
            sendChangeMessage();
        }
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("BarGridComponent::mouseExit: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("BarGridComponent::mouseExit: unknown exception"); }
}

void BarGridComponent::mouseDown (const juce::MouseEvent& e)
{
    try
    {
        if (e.mods.isPopupMenu())
        {
            auto hitBar = model.barIndexAtExact (e.getPosition(), contentWidth);
            if (hitBar.has_value())
            {
                int barIdx = hitBar.value();
                int segIdx = (barIdx >= 0 && barIdx < (int)model.bars.size())
                                 ? model.bars[barIdx].segmentIndex : -1;
                juce::String currentLabel = (barIdx >= 0 && barIdx < (int)model.bars.size())
                                                ? model.bars[barIdx].segmentLabel : juce::String();

                if (segIdx >= 0)
                {
                    juce::PopupMenu menu;
                    menu.addItem (1, "Rename segment…");
                    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this, segIdx, currentLabel] (int result)
                        {
                            if (result != 1 || !onSegmentRename)
                                return;

                            auto* alertWindow = new juce::AlertWindow ("Rename Segment",
                                                                       "Enter new segment name:",
                                                                       juce::MessageBoxIconType::NoIcon);
                            alertWindow->addTextEditor ("name", currentLabel);
                            alertWindow->addButton ("OK",     1, juce::KeyPress (juce::KeyPress::returnKey));
                            alertWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                            alertWindow->enterModalState (true, juce::ModalCallbackFunction::create (
                                [this, alertWindow, segIdx] (int modalResult)
                                {
                                    if (modalResult == 1 && onSegmentRename)
                                    {
                                        auto newName = alertWindow->getTextEditorContents ("name");
                                        onSegmentRename (segIdx, newName);
                                    }
                                    delete alertWindow;
                                }), false);
                        });
                }
            }
            return;
        }

        int barIndex = model.barIndexAtClamped (e.getPosition(), contentWidth);

        if (e.mods.isCommandDown())
        {
            // Ctrl/Cmd+click: toggle this bar in the multi-selection
            model.toggleAt (barIndex);
        }
        else if (e.mods.isShiftDown() && model.selection.has_value())
        {
            model.extendTo (barIndex);
        }
        else
        {
            // Plain click: start a fresh single selection
            model.clearExtraSelections();
            model.setAnchor (barIndex);
        }

        repaint();
        sendChangeMessage();
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("BarGridComponent::mouseDown: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("BarGridComponent::mouseDown: unknown exception"); }
}

void BarGridComponent::mouseDrag (const juce::MouseEvent& e)
{
    try
    {
        int barIndex = model.barIndexAtClamped (e.getPosition(), contentWidth);
        model.extendTo (barIndex);

        // Auto-scroll within parent Viewport
        if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
        {
            viewport->autoScroll (e.x, e.y, 20, 8);
        }

        repaint();
        sendChangeMessage();
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("BarGridComponent::mouseDrag: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("BarGridComponent::mouseDrag: unknown exception"); }
}

void BarGridComponent::mouseUp (const juce::MouseEvent&)
{
    try
    {
        // Move the play cursor only — do NOT call transportSource.start()
        if (model.selection.has_value() && !model.bars.empty())
        {
            auto sel = model.selection.value();
            int firstBar = sel.getStart();
            if (firstBar >= 0 && firstBar < (int)model.bars.size())
            {
                transportSource.setPosition (model.bars[firstBar].startTime);
            }
        }

        sendChangeMessage();
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("BarGridComponent::mouseUp: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("BarGridComponent::mouseUp: unknown exception"); }
}

void BarGridComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    try
    {
        // Double-click: seek AND start playback
        if (model.selection.has_value() && !model.bars.empty())
        {
            auto sel = model.selection.value();
            int firstBar = sel.getStart();
            if (firstBar >= 0 && firstBar < (int)model.bars.size())
            {
                transportSource.setPosition (model.bars[firstBar].startTime);
                transportSource.start();
            }
        }

        sendChangeMessage();
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("BarGridComponent::mouseDoubleClick: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("BarGridComponent::mouseDoubleClick: unknown exception"); }
}

//==============================================================================
// Mouse wheel — Ctrl+scroll for zoom

void BarGridComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    try
    {
        if (e.mods.isCtrlDown())
        {
            // Determine zoom direction: negative deltaY = zoom in (towards user),
            // positive deltaY = zoom out.
            int delta = (wheel.deltaY < 0) ? 1 : -1;
            if (wheel.isReversed)
                delta = -delta;

            int anchorBar = model.barIndexAtClamped (e.getPosition(), contentWidth);

            if (onZoomRequest)
                onZoomRequest (delta, anchorBar);

            return; // Consumed
        }

        // Not Ctrl: let the parent Viewport handle scrolling
        Component::mouseWheelMove (e, wheel);
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("BarGridComponent::mouseWheelMove: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("BarGridComponent::mouseWheelMove: unknown exception"); }
}

//==============================================================================
// Keyboard handlers

bool BarGridComponent::keyPressed (const juce::KeyPress& key)
{
    try
    {
        if (model.bars.empty())
            return false;

        bool handled = false;

        if (key.isKeyCode (juce::KeyPress::leftKey))
        {
            model.moveBy (-1, key.getModifiers().isShiftDown());
            handled = true;
        }
        else if (key.isKeyCode (juce::KeyPress::rightKey))
        {
            model.moveBy (1, key.getModifiers().isShiftDown());
            handled = true;
        }
        else if (key.isKeyCode (juce::KeyPress::homeKey))
        {
            model.selectFirst();
            handled = true;
        }
        else if (key.isKeyCode (juce::KeyPress::endKey))
        {
            model.selectLast();
            handled = true;
        }
        else if (key.isKeyCode (juce::KeyPress::escapeKey))
        {
            model.clearSelection();
            handled = true;
        }
        else if (key.isKeyCode (juce::KeyPress::spaceKey) || key.isKeyCode (juce::KeyPress::returnKey))
        {
            // Seek and start playback
            if (model.selection.has_value())
            {
                auto sel = model.selection.value();
                int firstBar = sel.getStart();
                if (firstBar >= 0 && firstBar < (int)model.bars.size())
                {
                    transportSource.setPosition (model.bars[firstBar].startTime);
                    transportSource.start();
                }
            }
            handled = true;
        }
        else if (key.isKeyCode ('=') && key.getModifiers().isCtrlDown())
        {
            // Ctrl+= zoom in, anchored at playhead or selection
            int anchorBar = (playingBar.has_value() ? playingBar.value()
                            : (model.selection.has_value() ? model.selection->getStart() : 0));
            if (onZoomRequest)
                onZoomRequest (1, anchorBar);
            handled = true;
        }
        else if (key.isKeyCode ('-') && key.getModifiers().isCtrlDown())
        {
            // Ctrl+- zoom out, anchored at playhead or selection
            int anchorBar = (playingBar.has_value() ? playingBar.value()
                            : (model.selection.has_value() ? model.selection->getStart() : 0));
            if (onZoomRequest)
                onZoomRequest (-1, anchorBar);
            handled = true;
        }
        else if (key.isKeyCode ('0') && key.getModifiers().isCtrlDown())
        {
            // Ctrl+0 jump to Arrangement overview (level 0)
            int anchorBar = (playingBar.has_value() ? playingBar.value()
                            : (model.selection.has_value() ? model.selection->getStart() : 0));
            // We request a delta that goes to level 0. Since we don't know current level,
            // we use a special approach: fire zoom out many times OR just set directly.
            // Instead, let the ArrangementView handle this via a special call.
            // For now, use delta = -100 as a "reset to 0" signal.
            if (onZoomRequest)
                onZoomRequest (-100, anchorBar);
            handled = true;
        }
        else if (key.isKeyCode (juce::KeyPress::pageDownKey))
        {
            model.moveBy (model.barsPerRow, key.getModifiers().isShiftDown());
            handled = true;
        }
        else if (key.isKeyCode (juce::KeyPress::pageUpKey))
        {
            model.moveBy (-model.barsPerRow, key.getModifiers().isShiftDown());
            handled = true;
        }

        if (handled)
        {
            scrollSelectedIntoView();
            repaint();
            updateAccessibilityDescription();
            sendChangeMessage();
            return true;
        }
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("BarGridComponent::keyPressed: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("BarGridComponent::keyPressed: unknown exception"); }

    return false;
}

//==============================================================================
// Helper methods

void BarGridComponent::updateAccessibilityDescription()
{
    try
    {
        juce::String desc ("Bar selection grid");

        if (model.selection.has_value())
        {
            auto sel = model.selection.value();
            int firstBar = sel.getStart();
            int lastBar = sel.getEnd() - 1;

            if (firstBar >= 0 && firstBar < (int)model.bars.size())
            {
                auto& bar = model.bars[firstBar];
                desc = juce::String::formatted ("Bar %d", bar.index1Based);
                if (!bar.segmentLabel.isEmpty())
                    desc += ", " + bar.segmentLabel;

                if (firstBar != lastBar && lastBar >= 0 && lastBar < (int)model.bars.size())
                {
                    auto& lastBarData = model.bars[lastBar];
                    desc += juce::String::formatted (" to Bar %d", lastBarData.index1Based);
                }
            }
        }
        else if (hoveredBar.has_value())
        {
            int idx = hoveredBar.value();
            if (idx >= 0 && idx < (int)model.bars.size())
            {
                auto& bar = model.bars[idx];
                desc = juce::String::formatted ("Bar %d", bar.index1Based);
                if (!bar.segmentLabel.isEmpty())
                    desc += ", " + bar.segmentLabel;
            }
        }

        getAccessibilityHandler()->notifyAccessibilityEvent (juce::AccessibilityEvent::textChanged);
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("BarGridComponent::updateAccessibilityDescription: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("BarGridComponent::updateAccessibilityDescription: unknown exception"); }
}

void BarGridComponent::scrollSelectedIntoView()
{
    try
    {
        if (!model.selection.has_value() || model.bars.empty())
            return;

        auto sel = model.selection.value();
        int firstBar = sel.getStart();

        if (firstBar >= 0 && firstBar < (int)model.bars.size())
        {
            auto cellBounds = model.cellBounds (firstBar, contentWidth);

            if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
            {
                auto viewBounds = viewport->getViewArea();
                if (!viewBounds.intersects (cellBounds))
                {
                    // Cell is outside view, scroll to bring it into view
                    int newY = cellBounds.getY();
                    viewport->setViewPosition (0, newY);
                }
            }
        }
    }
    catch (const std::exception& e) { juce::Logger::writeToLog ("BarGridComponent::scrollSelectedIntoView: " + juce::String (e.what())); }
    catch (...) { juce::Logger::writeToLog ("BarGridComponent::scrollSelectedIntoView: unknown exception"); }
}
