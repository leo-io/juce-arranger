#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>
#include <optional>
#include <cmath>
#include "SongAnalysis.h"
#include "Theme.h"

//==============================================================================
// Data structures for the bar grid model

enum class LabelDetail { NumberAndLabel, NumberOnly, SparseNumber };

struct Bar
{
    int          index1Based;    // 1-based bar number
    double       startTime;      // seconds
    double       endTime;        // seconds (clamped to total length for last bar)
    juce::String segmentLabel;   // from analysis.segmentAt(startTime)
    juce::Colour colour;         // from analysis.colourForLabel(segmentLabel)
    bool         isSegmentStart; // true iff label differs from previous bar's
};

//==============================================================================
// BarGridModel: pure data model for the bar-selection grid
// This is header-only, message-thread-only, no locks needed.

class BarGridModel
{
public:
    std::vector<Bar> bars;
    int barsPerRow = 8;

    //==========================================================================
    // Rebuild: construct the bar list from analysis data

    void rebuild (const SongAnalysis& analysis, int newBarsPerRow, double totalLengthSeconds)
    {
        bars.clear();
        barsPerRow = juce::jmax (1, newBarsPerRow);

        // Determine bar boundaries
        std::vector<double> barStarts;

        if (!analysis.downbeats.empty())
        {
            // Primary: use downbeats as bar boundaries
            barStarts = analysis.downbeats;
        }
        else if (analysis.bpm > 0)
        {
            // Fallback: synthesize from BPM and beat positions
            int beatsPerBar = 4; // default 4/4

            if (!analysis.beatPositions.empty())
            {
                // Infer beats per bar from the max beat position seen
                int maxBeatPos = 0;
                for (int pos : analysis.beatPositions)
                    maxBeatPos = juce::jmax (maxBeatPos, pos);
                beatsPerBar = juce::jmax (1, maxBeatPos + 1);
            }

            double beatLengthSeconds = 60.0 / analysis.bpm;
            double barLengthSeconds = beatLengthSeconds * beatsPerBar;

            for (double t = 0.0; t < totalLengthSeconds; t += barLengthSeconds)
                barStarts.push_back (t);
        }
        else
        {
            // Placeholder: no bar data
            return;
        }

        // Build bars from bar starts
        for (size_t i = 0; i < barStarts.size(); ++i)
        {
            Bar bar;
            bar.startTime = barStarts[i];
            bar.index1Based = (int)i + 1;

            // Compute end time
            if (i + 1 < barStarts.size())
                bar.endTime = barStarts[i + 1];
            else
                bar.endTime = totalLengthSeconds; // Last bar extends to end

            // Get segment info at the start of this bar
            const Segment* seg = analysis.segmentAt (bar.startTime);
            if (seg != nullptr)
                bar.segmentLabel = seg->label;
            else
                bar.segmentLabel = juce::String();

            bar.colour = analysis.colourForLabel (bar.segmentLabel);

            // Check if this is the start of a segment run
            bar.isSegmentStart = (i == 0) || (bar.segmentLabel != bars[i - 1].segmentLabel);

            bars.push_back (bar);
        }

        // Clear selection when rebuilding
        clearSelection();
    }

    //==========================================================================
    // Layout methods

    int cellWidth (int viewportWidth) const
    {
        return juce::jmax (1, viewportWidth / juce::jmax (1, barsPerRow));
    }

    int rowCount() const
    {
        if (bars.empty())
            return 1;
        return (int)std::ceil (bars.size() / (double)barsPerRow);
    }

    int requiredHeight() const
    {
        return rowCount() * Spacing::rowHeight;
    }

    juce::Rectangle<int> cellBounds (int barIndexZeroBased, int viewportWidth) const
    {
        if (barIndexZeroBased < 0 || barIndexZeroBased >= (int)bars.size())
            return juce::Rectangle<int>();

        int cw = cellWidth (viewportWidth);
        int col = barIndexZeroBased % barsPerRow;
        int row = barIndexZeroBased / barsPerRow;

        return juce::Rectangle<int> (col * cw, row * Spacing::rowHeight, cw, Spacing::rowHeight);
    }

    //==========================================================================
    // Hit testing

    std::optional<int> barIndexAtExact (juce::Point<int> p, int viewportWidth) const
    {
        if (bars.empty())
            return std::nullopt;

        int cw = cellWidth (viewportWidth);
        if (cw <= 0)
            return std::nullopt;

        int col = p.x / cw;
        int row = p.y / Spacing::rowHeight;

        if (col < 0 || col >= barsPerRow || row < 0)
            return std::nullopt;

        int barIndex = row * barsPerRow + col;
        if (barIndex >= (int)bars.size())
            return std::nullopt; // Trailing empty cell in last row

        return barIndex;
    }

    int barIndexAtClamped (juce::Point<int> p, int viewportWidth) const
    {
        if (bars.empty())
            return 0;

        auto exact = barIndexAtExact (p, viewportWidth);
        if (exact.has_value())
            return exact.value();

        // Clamp to nearest valid bar
        int cw = cellWidth (viewportWidth);
        int col = juce::jlimit (0, barsPerRow - 1, p.x / juce::jmax (1, cw));
        int row = juce::jlimit (0, rowCount() - 1, p.y / Spacing::rowHeight);

        int barIndex = row * barsPerRow + col;
        return juce::jlimit (0, (int)bars.size() - 1, barIndex);
    }

    //==========================================================================
    // Label density

    LabelDetail labelDetailFor (int cellWidthPx) const
    {
        if (cellWidthPx >= 64)
            return LabelDetail::NumberAndLabel;
        if (cellWidthPx >= 36)
            return LabelDetail::NumberOnly;
        return LabelDetail::SparseNumber;
    }

    //==========================================================================
    // Selection API

    std::optional<juce::Range<int>> selection;

    void setAnchor (int barIndex)
    {
        barIndex = juce::jlimit (0, (int)bars.size() - 1, barIndex);
        selection = juce::Range<int> (barIndex, barIndex + 1); // Range is [start, end), so +1
    }

    void extendTo (int barIndex)
    {
        barIndex = juce::jlimit (0, (int)bars.size() - 1, barIndex);

        if (!selection.has_value())
        {
            setAnchor (barIndex);
            return;
        }

        auto sel = selection.value();
        // Extend from anchor (sel.getStart()) to barIndex, handling both directions
        int start = sel.getStart();
        int end = barIndex + 1; // Range is [start, end)

        if (end < start)
            std::swap (start, end);

        selection = juce::Range<int> (start, end);
    }

    void moveBy (int delta, bool extend)
    {
        if (bars.empty())
            return;

        int current = 0;
        if (selection.has_value())
            current = selection.value().getStart();

        int next = juce::jlimit (0, (int)bars.size() - 1, current + delta);

        if (extend)
            extendTo (next);
        else
            setAnchor (next);
    }

    void selectFirst()
    {
        if (!bars.empty())
            setAnchor (0);
    }

    void selectLast()
    {
        if (!bars.empty())
            setAnchor ((int)bars.size() - 1);
    }

    void clearSelection()
    {
        selection.reset();
    }

    bool isSelected (int barIndex) const
    {
        if (!selection.has_value())
            return false;
        auto sel = selection.value();
        return barIndex >= sel.getStart() && barIndex < sel.getEnd();
    }

    juce::Range<double> selectionTimeRange() const
    {
        if (!selection.has_value() || bars.empty())
            return juce::Range<double> (0.0, 0.0);

        auto sel = selection.value();
        int first = sel.getStart();
        int last = juce::jmax (0, sel.getEnd() - 1); // Convert from exclusive to inclusive

        if (first >= (int)bars.size() || last >= (int)bars.size())
            return juce::Range<double> (0.0, 0.0);

        return juce::Range<double> (bars[first].startTime, bars[last].endTime);
    }

#if JUCE_UNIT_TESTS

    //==========================================================================
    // Unit tests for BarGridModel

    class UnitTestBarGridModel : public juce::UnitTest
    {
    public:
        UnitTestBarGridModel() : juce::UnitTest ("BarGridModel", "grid") {}

        void runTest() override
        {
            testRebuildWithDownbeats();
            testRebuildFallbackSynthesis();
            testRebuildPlaceholder();
            testCellLayout();
            testBarIndexAtExact();
            testBarIndexAtClamped();
            testLabelDetail();
            testSelection();
        }

    private:
        void testRebuildWithDownbeats()
        {
            beginTest ("rebuild with downbeats");

            SongAnalysis analysis;
            analysis.bpm = 120.0;
            analysis.downbeats = { 0.0, 0.5, 1.0, 1.5 };
            analysis.segments.push_back ({ 0.0, 2.0, "verse" });

            BarGridModel model;
            model.rebuild (analysis, 8, 2.0);

            expect (model.bars.size() == 4, "Should have 4 bars from 4 downbeats");
            expect (model.bars[0].startTime == 0.0, "Bar 0 starts at 0");
            expect (model.bars[0].endTime == 0.5, "Bar 0 ends at 0.5");
            expect (model.bars[3].endTime == 2.0, "Last bar ends at total length");
            expect (model.bars[0].isSegmentStart, "Bar 0 is segment start");
            expect (!model.bars[1].isSegmentStart, "Bar 1 is not segment start (same label)");
        }

        void testRebuildFallbackSynthesis()
        {
            beginTest ("rebuild fallback synthesis");

            SongAnalysis analysis;
            analysis.bpm = 120.0;
            analysis.beatPositions = { 0, 1, 2, 3 }; // 4/4
            // No downbeats - will synthesize

            BarGridModel model;
            model.rebuild (analysis, 8, 2.0); // 2 seconds = 2 bars at 120 BPM

            expect (!model.bars.empty(), "Fallback should synthesize bars");
            expect (model.bars.size() >= 2, "Should have at least 2 bars");
        }

        void testRebuildPlaceholder()
        {
            beginTest ("rebuild placeholder (no data)");

            SongAnalysis analysis;
            analysis.bpm = 0.0; // No BPM - placeholder

            BarGridModel model;
            model.rebuild (analysis, 8, 10.0);

            expect (model.bars.empty(), "Should have no bars with no BPM and no downbeats");
        }

        void testCellLayout()
        {
            beginTest ("cell layout");

            BarGridModel model;
            model.barsPerRow = 4;
            for (int i = 0; i < 12; ++i)
            {
                Bar b;
                b.index1Based = i + 1;
                b.startTime = (double)i;
                b.endTime = (double)(i + 1);
                b.colour = juce::Colours::white;
                model.bars.push_back (b);
            }

            expect (model.cellWidth (400) == 100, "Cell width with 400px and 4 bars per row");
            expect (model.rowCount() == 3, "3 rows for 12 bars with 4 per row");
            expect (model.requiredHeight() == 3 * Spacing::rowHeight, "Required height");

            auto bounds = model.cellBounds (0, 400);
            expect (bounds.getX() == 0 && bounds.getY() == 0 && bounds.getWidth() == 100, "Cell 0 bounds");

            auto bounds5 = model.cellBounds (5, 400);
            expect (bounds5.getY() == Spacing::rowHeight, "Cell 5 is in row 1");
        }

        void testBarIndexAtExact()
        {
            beginTest ("barIndexAtExact");

            BarGridModel model;
            model.barsPerRow = 4;
            for (int i = 0; i < 10; ++i)
            {
                Bar b;
                b.index1Based = i + 1;
                b.startTime = (double)i;
                b.endTime = (double)(i + 1);
                b.colour = juce::Colours::white;
                model.bars.push_back (b);
            }

            auto idx0 = model.barIndexAtExact (juce::Point<int> (50, 14), 400);
            expect (idx0.has_value() && idx0.value() == 0, "Point in first cell");

            auto idx5 = model.barIndexAtExact (juce::Point<int> (50, 14 + Spacing::rowHeight), 400);
            expect (idx5.has_value() && idx5.value() == 4, "Point in second row, first cell");

            // Trailing cell in partial last row should return nullopt
            auto idxTrailing = model.barIndexAtExact (juce::Point<int> (300, 14 + 2 * Spacing::rowHeight), 400);
            expect (!idxTrailing.has_value(), "Trailing empty cell returns nullopt");
        }

        void testBarIndexAtClamped()
        {
            beginTest ("barIndexAtClamped");

            BarGridModel model;
            model.barsPerRow = 4;
            for (int i = 0; i < 10; ++i)
            {
                Bar b;
                b.index1Based = i + 1;
                b.startTime = (double)i;
                b.endTime = (double)(i + 1);
                b.colour = juce::Colours::white;
                model.bars.push_back (b);
            }

            auto idx = model.barIndexAtClamped (juce::Point<int> (50, 14), 400);
            expect (idx == 0, "Clamped at valid cell");

            auto idxOut = model.barIndexAtClamped (juce::Point<int> (-100, 14), 400);
            expect (idxOut >= 0 && idxOut < 10, "Out-of-bounds clamped to valid range");

            auto idxTrailing = model.barIndexAtClamped (juce::Point<int> (300, 14 + 2 * Spacing::rowHeight), 400);
            expect (idxTrailing >= 0 && idxTrailing < 10, "Trailing cell clamped to last valid bar");
        }

        void testLabelDetail()
        {
            beginTest ("labelDetailFor");

            BarGridModel model;

            expect (model.labelDetailFor (100) == LabelDetail::NumberAndLabel, "Wide cells show label");
            expect (model.labelDetailFor (50) == LabelDetail::NumberOnly, "Medium cells show number only");
            expect (model.labelDetailFor (20) == LabelDetail::SparseNumber, "Narrow cells show sparse numbers");
        }

        void testSelection()
        {
            beginTest ("selection API");

            BarGridModel model;
            for (int i = 0; i < 10; ++i)
            {
                Bar b;
                b.index1Based = i + 1;
                b.startTime = (double)i;
                b.endTime = (double)(i + 1);
                b.colour = juce::Colours::white;
                b.isSegmentStart = (i == 0);
                model.bars.push_back (b);
            }

            model.setAnchor (3);
            expect (model.isSelected (3), "Bar 3 is selected after setAnchor");
            expect (!model.isSelected (2), "Bar 2 is not selected");

            model.extendTo (6);
            expect (model.isSelected (3) && model.isSelected (4) && model.isSelected (5) && model.isSelected (6),
                    "Range 3-6 is selected after extendTo");
            expect (!model.isSelected (2) && !model.isSelected (7), "Outside range not selected");

            model.clearSelection();
            expect (!model.isSelected (3), "Selection cleared");

            model.selectFirst();
            expect (model.isSelected (0), "First bar selected");

            model.selectLast();
            expect (model.isSelected (9), "Last bar selected");

            model.setAnchor (2);
            model.moveBy (3, false);
            expect (model.isSelected (5) && !model.isSelected (2), "moveBy without extend");

            model.setAnchor (2);
            model.moveBy (3, true);
            expect (model.isSelected (2) && model.isSelected (3) && model.isSelected (4) && model.isSelected (5),
                    "moveBy with extend");

            auto timeRange = model.selectionTimeRange();
            expect (timeRange.getStart() == 2.0 && timeRange.getEnd() == 6.0,
                    "selectionTimeRange covers selected bars");
        }
    };

    static UnitTestBarGridModel unitTestBarGridModel;

#endif
};

#if JUCE_UNIT_TESTS
BarGridModel::UnitTestBarGridModel BarGridModel::unitTestBarGridModel;
#endif
