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
enum class GridMode { WholeSong, Section, Fixed };

struct RowSpan
{
    int firstBar = 0;
    int barCount = 1;
};

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
    GridMode mode = GridMode::Fixed;
    int zoomLevel = 1;
    std::vector<RowSpan> rowSpans;

    //==========================================================================
    // Rebuild: construct the bar list from analysis data (no barsPerRow — set via applyZoom)

    void rebuild (const SongAnalysis& analysis, double totalLengthSeconds)
    {
        bars.clear();

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

        // Clear selection and rowSpans when rebuilding
        clearSelection();
        rowSpans.clear();
        mode = GridMode::Fixed;
        barsPerRow = juce::jmax (1, bars.size() < 8 ? (int)bars.size() : 8);
    }

    //==========================================================================
    // Helper: find bars in a segment given a bar's segment label

    struct SegmentSpan
    {
        int firstBar = 0;
        int barCount = 1;
        juce::String label;
    };

    SegmentSpan getSegmentSpanForBar (int barIndexZeroBased) const
    {
        if (barIndexZeroBased < 0 || barIndexZeroBased >= (int)bars.size())
            return { 0, 1, juce::String() };

        const juce::String& label = bars[barIndexZeroBased].segmentLabel;

        // Find first bar with this label
        int firstBar = barIndexZeroBased;
        while (firstBar > 0 && bars[firstBar - 1].segmentLabel == label)
            --firstBar;

        // Find last bar with this label
        int lastBar = barIndexZeroBased;
        while (lastBar < (int)bars.size() - 1 && bars[lastBar + 1].segmentLabel == label)
            ++lastBar;

        return { firstBar, lastBar - firstBar + 1, label };
    }

    //==========================================================================
    // Layout methods

    int maxBarsInAnyRow() const
    {
        int m = 1;
        for (const auto& s : rowSpans)
            m = juce::jmax (m, s.barCount);
        return m;
    }

    int cellWidth (int viewportWidth) const
    {
        return juce::jmax (1, viewportWidth / juce::jmax (1, barsPerRow));
    }

    int cellWidthForBarSection (int barIndexZeroBased, int viewportWidth) const
    {
        auto span = getSegmentSpanForBar (barIndexZeroBased);
        return juce::jmax (1, viewportWidth / juce::jmax (1, span.barCount));
    }

    int rowCount() const
    {
        if (bars.empty())
            return 1;

        if (mode == GridMode::Section && !rowSpans.empty())
            return (int)rowSpans.size();

        return (int)std::ceil (bars.size() / (double)barsPerRow);
    }

    int requiredHeight() const
    {
        return rowCount() * Spacing::rowHeight;
    }

    int cellWidthForRow (int rowIndex, int viewportWidth) const
    {
        if (mode == GridMode::Section && rowIndex >= 0 && rowIndex < (int)rowSpans.size())
            return juce::jmax (1, viewportWidth / juce::jmax (1, rowSpans[rowIndex].barCount));

        return cellWidth (viewportWidth);
    }

    juce::Rectangle<int> cellBounds (int barIndexZeroBased, int viewportWidth) const
    {
        if (barIndexZeroBased < 0 || barIndexZeroBased >= (int)bars.size())
            return juce::Rectangle<int>();

        if (mode == GridMode::Section && !rowSpans.empty())
        {
            // Find which row this bar belongs to
            for (size_t r = 0; r < rowSpans.size(); ++r)
            {
                const auto& span = rowSpans[r];
                if (barIndexZeroBased >= span.firstBar && barIndexZeroBased < span.firstBar + span.barCount)
                {
                    int maxBars = maxBarsInAnyRow();
                    int col = barIndexZeroBased - span.firstBar;
                    int cw = juce::jmax (1, viewportWidth / juce::jmax (1, maxBars));
                    return juce::Rectangle<int> (col * cw, (int)r * Spacing::rowHeight, cw, Spacing::rowHeight);
                }
            }
            return juce::Rectangle<int>();
        }

        // Fixed and WholeSong: uniform cell width
        int bpRow = juce::jmax (1, barsPerRow);
        int cw  = juce::jmax (1, viewportWidth / bpRow);
        int col = barIndexZeroBased % bpRow;
        int row = barIndexZeroBased / bpRow;
        return juce::Rectangle<int> (col * cw, row * Spacing::rowHeight, cw, Spacing::rowHeight);
    }

    //==========================================================================
    // Hit testing

    std::optional<int> barIndexAtExact (juce::Point<int> p, int viewportWidth) const
    {
        if (bars.empty())
            return std::nullopt;

        int row = p.y / Spacing::rowHeight;

        if (mode == GridMode::Section && !rowSpans.empty())
        {
            if (row < 0 || row >= (int)rowSpans.size())
                return std::nullopt;

            int maxBars = maxBarsInAnyRow();
            int cw = juce::jmax (1, viewportWidth / juce::jmax (1, maxBars));
            const auto& span = rowSpans[row];
            int col = p.x / cw;

            if (col < 0 || col >= span.barCount)
                return std::nullopt;

            int barIndex = span.firstBar + col;
            if (barIndex >= (int)bars.size())
                return std::nullopt;

            return barIndex;
        }

        // WholeSong and Fixed: hit testing via cellBounds
        if (row < 0 || row >= rowCount())
            return std::nullopt;

        // For each bar in this row, check if p.x falls within it
        int rowStartBar = row * barsPerRow;
        int rowEndBar = juce::jmin ((row + 1) * barsPerRow, (int)bars.size());

        for (int barIdx = rowStartBar; barIdx < rowEndBar; ++barIdx)
        {
            auto bounds = cellBounds (barIdx, viewportWidth);
            if (bounds.contains (p.x, p.y))
                return barIdx;
        }

        return std::nullopt;
    }

    int barIndexAtClamped (juce::Point<int> p, int viewportWidth) const
    {
        if (bars.empty())
            return 0;

        auto exact = barIndexAtExact (p, viewportWidth);
        if (exact.has_value())
            return exact.value();

        int row = juce::jlimit (0, rowCount() - 1, p.y / Spacing::rowHeight);

        if (mode == GridMode::Section && !rowSpans.empty())
        {
            if (row >= (int)rowSpans.size())
                row = (int)rowSpans.size() - 1;

            int maxBars = maxBarsInAnyRow();
            int cw = juce::jmax (1, viewportWidth / juce::jmax (1, maxBars));
            const auto& span = rowSpans[row];
            int col = juce::jlimit (0, span.barCount - 1, p.x / cw);

            int barIndex = span.firstBar + col;
            return juce::jlimit (0, (int)bars.size() - 1, barIndex);
        }

        // WholeSong and Fixed: clamp to nearest valid bar
        int rowStartBar = row * barsPerRow;
        int rowEndBar = juce::jmin ((row + 1) * barsPerRow, (int)bars.size());

        // Find closest bar in this row
        int closestBar = rowStartBar;
        int closestDist = std::abs (p.x - cellBounds (rowStartBar, viewportWidth).getCentreX());

        for (int barIdx = rowStartBar + 1; barIdx < rowEndBar; ++barIdx)
        {
            int dist = std::abs (p.x - cellBounds (barIdx, viewportWidth).getCentreX());
            if (dist < closestDist)
            {
                closestDist = dist;
                closestBar = barIdx;
            }
        }

        return juce::jlimit (0, (int)bars.size() - 1, closestBar);
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
    // Zoom / mode helpers

    /** Median section length in bars, used to derive zoom step sizes. */
    int medianSectionBars (const SongAnalysis& analysis) const
    {
        if (analysis.segments.empty() || analysis.downbeats.empty())
            return 8;

        std::vector<int> sectionBars;
        for (const auto& seg : analysis.segments)
        {
            int count = 0;
            for (double db : analysis.downbeats)
            {
                if (db >= seg.start && db < seg.end)
                    ++count;
            }
            if (count > 0)
                sectionBars.push_back (count);
        }

        if (sectionBars.empty())
            return 8;

        std::sort (sectionBars.begin(), sectionBars.end());
        return sectionBars[sectionBars.size() / 2];
    }

    /** Maximum zoom level, where one bar fills one row. */
    int maxZoomLevel (const SongAnalysis& analysis) const
    {
        if (bars.empty())
            return 0;
        int S = medianSectionBars (analysis);
        if (S <= 1)
            return 2; // Song + Section + Bar
        return 1 + (int)std::ceil (std::log2 ((double)S));
    }

    /** Human-readable name for a zoom level (used in the toolbar chip). */
    static juce::String zoomLevelName (int level, int maxLevel)
    {
        if (level == 0) return "Song";
        if (level == 1) return "Section";
        if (level >= maxLevel) return "Bar";

        // Intermediate levels: 1/2, 1/4, 1/8, ... Section
        int denom = 1 << (level - 1);
        return "1/" + juce::String (denom) + " Section";
    }

    /** Apply a zoom level, setting mode, barsPerRow, and rowSpans accordingly. */
    void applyZoom (const SongAnalysis& analysis, int level, double /*totalLen*/)
    {
        zoomLevel = juce::jmax (0, level);
        int maxLvl = maxZoomLevel (analysis);
        zoomLevel = juce::jmin (maxLvl, zoomLevel);
        rowSpans.clear();

        if (bars.empty())
        {
            mode = GridMode::Fixed;
            barsPerRow = 8;
            return;
        }

        if (zoomLevel == 0)
        {
            // WholeSong: one row, all bars
            mode = GridMode::WholeSong;
            barsPerRow = (int)bars.size();
        }
        else if (zoomLevel == 1)
        {
            // Section-aligned ragged rows
            mode = GridMode::Section;
            barsPerRow = 0; // not used in Section mode
            buildRowSpans (analysis);
        }
        else
        {
            // Fixed bars-per-row: ceil(S / 2^(level-1))
            mode = GridMode::Fixed;
            int S = medianSectionBars (analysis);
            if (S <= 0) S = 8;
            int exponent = zoomLevel - 1;
            barsPerRow = juce::jmax (1, (int)std::ceil (S / std::pow (2.0, exponent)));
        }
    }

    /** Build rowSpans from analysis segments for Section mode. */
    void buildRowSpans (const SongAnalysis& analysis)
    {
        rowSpans.clear();

        if (bars.empty())
        {
            rowSpans.push_back ({ 0, 1 });
            return;
        }

        if (!analysis.segments.empty())
        {
            for (const auto& seg : analysis.segments)
            {
                int firstBar = -1;
                int lastBar  = -1;

                for (size_t i = 0; i < bars.size(); ++i)
                {
                    // Bar overlaps the segment if its start is within range
                    if (bars[i].startTime >= seg.start
                        && (i == 0 || bars[i - 1].startTime < seg.end)
                        && bars[i].startTime < seg.end)
                    {
                        if (firstBar == -1)
                            firstBar = (int)i;
                        lastBar = (int)i;
                    }
                }

                if (firstBar >= 0 && lastBar >= firstBar)
                    rowSpans.push_back ({ firstBar, lastBar - firstBar + 1 });
            }
        }

        // Fallback: divide bars evenly (8 per row)
        if (rowSpans.empty())
        {
            const int perRow = 8;
            for (int i = 0; i < (int)bars.size(); i += perRow)
                rowSpans.push_back ({ i, juce::jmin (perRow, (int)bars.size() - i) });
        }
    }

    /** Row index for a given bar index (0-based). */
    int rowOfBar (int barIndexZeroBased) const
    {
        if (barIndexZeroBased < 0 || barIndexZeroBased >= (int)bars.size())
            return 0;

        if (mode == GridMode::Section && !rowSpans.empty())
        {
            for (size_t i = 0; i < rowSpans.size(); ++i)
            {
                if (barIndexZeroBased >= rowSpans[i].firstBar
                    && barIndexZeroBased < rowSpans[i].firstBar + rowSpans[i].barCount)
                    return (int)i;
            }
            return 0;
        }

        return barIndexZeroBased / juce::jmax (1, barsPerRow);
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
            testApplyZoom();
            testMedianSectionBars();
            testZoomLevelNames();
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
            model.rebuild (analysis, 2.0);
            model.barsPerRow = 8; // set display param directly for tests

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
            model.rebuild (analysis, 2.0); // 2 seconds = 2 bars at 120 BPM
            model.barsPerRow = 8;

            expect (!model.bars.empty(), "Fallback should synthesize bars");
            expect (model.bars.size() >= 2, "Should have at least 2 bars");
        }

        void testRebuildPlaceholder()
        {
            beginTest ("rebuild placeholder (no data)");

            SongAnalysis analysis;
            analysis.bpm = 0.0; // No BPM - placeholder

            BarGridModel model;
            model.rebuild (analysis, 10.0);

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

        void testApplyZoom()
        {
            beginTest ("applyZoom");

            SongAnalysis analysis;
            analysis.bpm = 120.0;
            analysis.downbeats = { 0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5,
                                   4.0, 4.5, 5.0, 5.5, 6.0, 6.5, 7.0, 7.5 };
            analysis.segments.push_back ({ 0.0, 4.0, "verse" });  // 8 bars
            analysis.segments.push_back ({ 4.0, 8.0, "chorus" }); // 8 bars

            BarGridModel model;
            model.rebuild (analysis, 8.0);
            expect (model.bars.size() == 16, "16 bars from 16 downbeats");

            // Level 0: WholeSong
            model.applyZoom (analysis, 0, 8.0);
            expect (model.mode == GridMode::WholeSong, "Level 0 = WholeSong");
            expect (model.barsPerRow == 16, "16 bars per row for 16-bar song");

            // Level 1: Section
            model.applyZoom (analysis, 1, 8.0);
            expect (model.mode == GridMode::Section, "Level 1 = Section");
            expect (model.rowSpans.size() == 2, "2 rows for 2 segments");
            if (model.rowSpans.size() >= 2)
            {
                expect (model.rowSpans[0].barCount == 8, "First section 8 bars");
                expect (model.rowSpans[1].barCount == 8, "Second section 8 bars");
            }

            // Level 2: Fixed, ceil(S/2) = 4 bars/row
            model.applyZoom (analysis, 2, 8.0);
            expect (model.mode == GridMode::Fixed, "Level 2 = Fixed");
            expect (model.barsPerRow == 4, "4 bars per row for S=8 at level 2");

            // Level N (max): Bar mode
            int maxLvl = model.maxZoomLevel (analysis);
            model.applyZoom (analysis, maxLvl, 8.0);
            expect (model.mode == GridMode::Fixed, "Max level = Fixed");
            expect (model.barsPerRow == 1, "Max level bar-per-row = 1");
        }

        void testMedianSectionBars()
        {
            beginTest ("medianSectionBars");

            SongAnalysis analysis;
            analysis.bpm = 120.0;
            // 16 quarter-note downbeats = 16 bars
            for (int i = 0; i < 16; ++i)
                analysis.downbeats.push_back (i * 0.5);

            // Two sections: 8 bars and 8 bars → median = 8
            analysis.segments.push_back ({ 0.0, 4.0, "verse" });
            analysis.segments.push_back ({ 4.0, 8.0, "chorus" });

            BarGridModel model;
            model.rebuild (analysis, 8.0);

            int median = model.medianSectionBars (analysis);
            expect (median == 8, "Median section bars = 8 for two 8-bar sections");

            // Empty segments → default 8
            SongAnalysis emptyAnalysis;
            emptyAnalysis.bpm = 120.0;
            int defaultMedian = model.medianSectionBars (emptyAnalysis);
            expect (defaultMedian == 8, "Default median = 8 with no segments");
        }

        void testZoomLevelNames()
        {
            beginTest ("zoomLevelNames");

            expect (BarGridModel::zoomLevelName (0, 4) == "Song");
            expect (BarGridModel::zoomLevelName (1, 4) == "Section");
            expect (BarGridModel::zoomLevelName (4, 4) == "Bar");

            // Level 2 for S=8 → 1/2 Section
            auto name2 = BarGridModel::zoomLevelName (2, 4);
            expect (name2.contains ("Section"), "Level 2 name contains 'Section'");
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
