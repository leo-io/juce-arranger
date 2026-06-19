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

        int cw = model.cellWidth (contentWidth);
        auto detail = model.labelDetailFor (cw);

        // Draw each bar cell
        for (size_t i = 0; i < model.bars.size(); ++i)
        {
            const auto& bar = model.bars[i];
            auto cellBounds = model.cellBounds ((int)i, contentWidth);

            // Fill cell with segment colour (40% alpha)
            g.setColour (bar.colour.withAlpha (0.40f));
            g.fillRect (cellBounds);

            // Cell outline (1px divider)
            g.setColour (Palette::divider);
            g.drawRect (cellBounds, 1);

            // Label colour: contrasting with the cell fill
            juce::Colour labelColour = bar.colour.withAlpha (0.40f).contrasting (0.6f);
            g.setColour (labelColour);
            g.setFont (10.0f);

            // Draw bar number based on density
            bool drawNumber = false;
            if (detail == LabelDetail::NumberAndLabel || detail == LabelDetail::NumberOnly)
            {
                drawNumber = true;
            }
            else if (detail == LabelDetail::SparseNumber && bar.index1Based % 4 == 1)
            {
                drawNumber = true;
            }

            if (drawNumber)
            {
                auto numberBounds = cellBounds.reduced (2);
                g.drawText (juce::String (bar.index1Based),
                           numberBounds,
                           juce::Justification::topLeft, false);
            }

            // Draw segment label only on first bar of run, and only if detail allows
            if (bar.isSegmentStart && detail == LabelDetail::NumberAndLabel && !bar.segmentLabel.isEmpty())
            {
                auto labelBounds = cellBounds.reduced (2);
                labelBounds.removeFromTop (12); // Leave space for the bar number
                g.drawText (bar.segmentLabel,
                           labelBounds,
                           juce::Justification::topLeft, true);
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

        // Selection rendering: one outline per row-span
        if (model.selection.has_value())
        {
            auto sel = model.selection.value();
            int firstBar = sel.getStart();
            int lastBar = sel.getEnd() - 1;

            // Group selected bars by row and draw per-row outline
            for (int row = 0; row < model.rowCount(); ++row)
            {
                int rowStart = row * model.barsPerRow;
                int rowEnd = rowStart + model.barsPerRow;

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
        int barIndex = model.barIndexAtClamped (e.getPosition(), contentWidth);

        if (e.mods.isShiftDown() && model.selection.has_value())
        {
            model.extendTo (barIndex);
        }
        else
        {
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
