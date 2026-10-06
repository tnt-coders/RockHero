#include "keybinds/editor_command_registry.h"
#include "shared/editor_theme.h"
#include "tab/tab_view.h"
#include "timeline/timeline_cursor.h"
#include "timeline/timeline_ruler.h"
#include "timeline/track_viewport.h"

#include <algorithm>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/testing/tuning_fixtures.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>
#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

using rock_hero::common::core::testing::anchor;

// Records the ruler's chip intents so a click can be asserted without standing up the whole editor
// view, which is what would otherwise have to answer this interface.
struct RecordingRulerListener final : TimelineRuler::Listener
{
    void onSongSectionSelected(std::optional<common::core::GridPosition> position) override
    {
        last_selected = position;
        select_count += 1;
    }

    void onTempoAnchorSelected(common::core::GridPosition position) override
    {
        last_selected_tempo_anchor = position;
    }

    void onTempoAnchorMoveRequested(
        common::core::GridPosition position, std::int64_t milliseconds) override
    {
        last_moved_tempo_anchor = position;
        last_moved_tempo_milliseconds = milliseconds;
        move_count += 1;
    }

    void onTempoAnchorDragReleased() override
    {
        release_count += 1;
    }

    void onTimeSignatureSelected(int measure) override
    {
        last_selected_signature_measure = measure;
    }

    void onSongSectionInsertPromptRequested(common::core::GridPosition position) override
    {
        insert_count += 1;
        last_insert_position = position;
    }

    void onTempoAnchorInsertRequested(common::core::GridPosition position) override
    {
        last_tempo_insert_position = position;
    }

    void onTimeSignatureInsertPromptRequested(
        const core::InsertTimeSignatureTarget& target) override
    {
        last_signature_insert = target;
    }

    void onScaleTempoPromptRequested() override
    {
        scale_tempo_count += 1;
    }

    void onSelectionRestateRequested() override
    {
        restate_count += 1;
    }

    void onSelectionDeleteRequested() override
    {
        delete_count += 1;
    }

    void onSelectionMoveRequested(bool later) override
    {
        last_move_later = later;
    }

    // Last position reported through onSongSectionSelected(), empty after a deselect.
    std::optional<common::core::GridPosition> last_selected{};

    // Number of onSongSectionSelected() calls, deselects included.
    int select_count{0};

    // Counts of the menu-only intents, and the position the insert was asked for.
    int insert_count{0};
    std::optional<common::core::GridPosition> last_insert_position{};
    int delete_count{0};

    // Position the tempo menu last asked an anchor for, and how often a restate was asked for.
    std::optional<common::core::GridPosition> last_tempo_insert_position{};
    int restate_count{0};

    // Insert the signature menu last asked a time signature for.
    std::optional<core::InsertTimeSignatureTarget> last_signature_insert{};

    // How often the tempo menu asked for the Scale Tempo prompt.
    int scale_tempo_count{0};

    // Direction last reported through onSelectionMoveRequested().
    std::optional<bool> last_move_later{};

    // Anchor beat and measure last reported by a tempo or time-signature chip click.
    std::optional<common::core::GridPosition> last_selected_tempo_anchor{};
    std::optional<int> last_selected_signature_measure{};

    // Anchor and time a tempo-chip drag last asked for, how many drag events asked, and how many
    // drags reported their release.
    std::optional<common::core::GridPosition> last_moved_tempo_anchor{};
    std::optional<std::int64_t> last_moved_tempo_milliseconds{};
    int move_count{0};
    int release_count{0};
};

// Builds a one-measure 4/4 map for viewport-grid rendering checks.
[[nodiscard]] common::core::TempoMap makeOneMeasureTempoMap(std::int64_t measure_milliseconds)
{
    return common::core::TempoMap{
        std::vector{
            common::core::TimeSignatureChange{
                .measure = 1,
                .meter = {.numerator = 4, .denominator = 4},
            },
        },
        std::vector{
            anchor("1:1", 0),
            anchor("2:1", measure_milliseconds),
        },
    };
}

// A chart carrying one bare note head on the lowest string at a time: at the very first beat, the
// figure the leftmost scroll position has to draw whole.
[[nodiscard]] std::shared_ptr<const common::core::ChartViewState> makeSingleNoteChart(
    const double start_seconds)
{
    common::core::ChartViewState chart;
    chart.open_strings = common::core::testing::standardTuning();
    chart.notes = {
        common::core::NoteViewState{
            .start_seconds = start_seconds,
            .ring_end_seconds = start_seconds + 0.5,
            .ink_end_seconds = start_seconds,
            .string = 1,
            .fret = 3,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
    };
    return std::make_shared<const common::core::ChartViewState>(std::move(chart));
}

// Builds three 60 BPM measures, 4/4 then 3/4 from measure 2, ending on the terminal at 4:1 (10 s):
// measure 2's downbeat is at 4 s and measure 3's at 7 s.
[[nodiscard]] common::core::TempoMap makeMeterChangeTempoMap()
{
    return common::core::TempoMap{
        std::vector{
            common::core::TimeSignatureChange{
                .measure = 1,
                .meter = {.numerator = 4, .denominator = 4},
            },
            common::core::TimeSignatureChange{
                .measure = 2,
                .meter = {.numerator = 3, .denominator = 4},
            },
        },
        std::vector{anchor("1:1", 0), anchor("4:1", 10000)},
    };
}

// Lays a ruler over the meter-change map across 401 px, 40 px per second: the signature chips
// stand at x = 0 (measure 1) and x = 160 (measure 2), and measure 3's downbeat at x = 280.
void showMeterChangeMap(TimelineRuler& ruler)
{
    constexpr common::core::TimeRange window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{10.0},
    };
    const common::core::TempoMap tempo_map = makeMeterChangeTempoMap();
    constexpr common::core::Fraction grid_note_value{1, 4};
    ruler.setBounds(0, 0, 401, g_timeline_ruler_height);
    ruler.setTimelineView(window, ruler.getWidth(), 0);
    ruler.setGrid(tempo_map, grid_note_value);
    ruler.setGridLines(
        core::visibleTempoGridLines(
            tempo_map, grid_note_value, window, ruler.getWidth(), 0, ruler.getWidth()));
    ruler.setProjectLoaded(true);
}

// The rows of a plain popup menu in order: each row's text and whether it is enabled.
[[nodiscard]] std::vector<std::pair<std::string, bool>> menuRows(const juce::PopupMenu& menu)
{
    std::vector<std::pair<std::string, bool>> rows;
    juce::PopupMenu::MenuItemIterator iterator{menu};
    while (iterator.next())
    {
        const juce::PopupMenu::Item& item = iterator.getItem();
        rows.emplace_back(item.text.toStdString(), item.isEnabled);
    }
    return rows;
}

// Whether any pixel of a region differs between two snapshots of one component.
[[nodiscard]] bool regionDiffers(
    const juce::Image& before, const juce::Image& after, const juce::Rectangle<int> region)
{
    for (int x = region.getX(); x < region.getRight(); ++x)
    {
        for (int y = region.getY(); y < region.getBottom(); ++y)
        {
            if (before.getPixelAt(x, y) != after.getPixelAt(x, y))
            {
                return true;
            }
        }
    }
    return false;
}

// Returns a y coordinate that lands on the 1px-on row of the timeline grid dot pattern.
[[nodiscard]] int gridDotYAtOrAfter(int y) noexcept
{
    return y + y % 2;
}

// Counts snapshot pixels differing from the sampled background inside a region, isolating label
// glyphs for the ruler pinning checks.
[[nodiscard]] int countGlyphPixels(
    const juce::Image& image, juce::Rectangle<int> region, juce::Colour background)
{
    int glyph_pixels = 0;
    for (int x = region.getX(); x < region.getRight(); ++x)
    {
        for (int y = region.getY(); y < region.getBottom(); ++y)
        {
            if (image.getPixelAt(x, y) != background)
            {
                ++glyph_pixels;
            }
        }
    }

    return glyph_pixels;
}

// Canvas column of a timeline time, from the canvas's own two facts: the range its width
// represents and that width. The range comes from the viewport rather than from the pushed state
// because the canvas reaches a gutter further left than the timeline does, so the first beat's
// note head has canvas to draw its left half on; every mapping onto canvas columns uses it.
[[nodiscard]] float canvasXForTime(
    const TrackViewport& track_viewport, const juce::Component& track_content, double seconds)
{
    const auto x = cursorXForTimelinePosition(
        common::core::TimePosition{seconds},
        track_viewport.canvasTimeline(),
        track_content.getWidth());
    REQUIRE(x.has_value());
    return x.value_or(0.0f);
}

// The same column rounded to the pixel the grid, the cursor, and the caret all draw in.
[[nodiscard]] int canvasColumnForTime(
    const TrackViewport& track_viewport, const juce::Component& track_content, double seconds)
{
    return static_cast<int>(std::round(canvasXForTime(track_viewport, track_content, seconds)));
}

} // namespace

// Verifies ruler measure ticks span the whole body while beat ticks stay in the bottom band,
// with the chip rows above staying tick-free.
TEST_CASE("TimelineRuler draws full measure and short beat ticks", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    constexpr common::core::TimeRange one_measure_window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{4.0},
    };
    const common::core::TempoMap tempo_map = makeOneMeasureTempoMap(4000);
    constexpr common::core::Fraction grid_note_value{1, 4};
    TimelineRuler ruler;
    ruler.setBounds(0, 0, 101, g_timeline_ruler_height);
    ruler.setTimelineView(one_measure_window, ruler.getWidth(), 0);
    ruler.setGrid(tempo_map, grid_note_value);
    ruler.setGridLines(
        core::visibleTempoGridLines(
            tempo_map, grid_note_value, one_measure_window, ruler.getWidth(), 0, ruler.getWidth()));
    ruler.setProjectLoaded(true);

    const juce::Image image = ruler.createComponentSnapshot(ruler.getLocalBounds());

    // One measure across 101px maps seconds * 25: the measure tick at x = 0 spans the whole
    // ruler body (y = 50) while the beat tick at x = 75 fills only the bottom band (y = 62),
    // leaving the body's upper rows at that column as plain ruler chrome.
    CHECK(image.getPixelAt(0, 50) == editorTheme().grid_measure);
    CHECK(image.getPixelAt(75, 50) == editorTheme().timeline_ruler_background);
    CHECK(image.getPixelAt(75, 62) == editorTheme().grid_measure);
}

// Verifies subdivision ticks draw shorter than beat ticks so beats stay readable on fine grids.
TEST_CASE("TimelineRuler draws shorter subdivision ticks", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    constexpr common::core::TimeRange one_measure_window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{4.0},
    };
    const common::core::TempoMap tempo_map = makeOneMeasureTempoMap(4000);
    constexpr common::core::Fraction grid_note_value{1, 8};
    TimelineRuler ruler;
    ruler.setBounds(0, 0, 201, g_timeline_ruler_height);
    ruler.setTimelineView(one_measure_window, ruler.getWidth(), 0);
    ruler.setGrid(tempo_map, grid_note_value);
    ruler.setGridLines(
        core::visibleTempoGridLines(
            tempo_map, grid_note_value, one_measure_window, ruler.getWidth(), 0, ruler.getWidth()));
    ruler.setProjectLoaded(true);

    const juce::Image image = ruler.createComponentSnapshot(ruler.getLocalBounds());

    // The half-beat subdivision at 3.5s (x = 175) fills only the shorter bottom band while the
    // beat at 3.0s (x = 150) also fills the taller 10px beat band above it; both columns sit far
    // from any chips so only tick pixels can land on the probes.
    CHECK(image.getPixelAt(150, 60) == editorTheme().grid_measure);
    CHECK(image.getPixelAt(175, 60) == editorTheme().timeline_ruler_background);
    CHECK(image.getPixelAt(175, 64) == editorTheme().grid_measure);
}

// Verifies the song's section names draw as filled chips in the header's top chip row,
// each chip's left edge on its section start, and nowhere a section does not begin.
TEST_CASE("TimelineRuler draws section chips in the header", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    constexpr common::core::TimeRange one_measure_window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{4.0},
    };
    const common::core::TempoMap tempo_map = makeOneMeasureTempoMap(4000);
    constexpr common::core::Fraction grid_note_value{1, 4};
    TimelineRuler ruler;
    ruler.setBounds(0, 0, 401, g_timeline_ruler_height);
    ruler.setTimelineView(one_measure_window, ruler.getWidth(), 0);
    ruler.setGrid(tempo_map, grid_note_value);
    ruler.setGridLines(
        core::visibleTempoGridLines(
            tempo_map, grid_note_value, one_measure_window, ruler.getWidth(), 0, ruler.getWidth()));
    ruler.setProjectLoaded(true);
    ruler.setSectionLabels(
        std::vector<RulerSectionLabel>{
            RulerSectionLabel{
                .seconds = 1.0,
                .position = common::core::GridPosition{.measure = 1, .beat = 2},
                .name = "Verse",
                .selected = false,
            },
        });

    const juce::Image image = ruler.createComponentSnapshot(ruler.getLocalBounds());

    // One measure across 401px maps seconds * 100, so the "Verse" chip's left edge sits at
    // x = 100; the probe sits in the chip's left margin at the row's mid-height, clear of the
    // centered glyphs and the rounded corners.
    CHECK(image.getPixelAt(102, 7) == editorTheme().section_chip);
    // The chip drops a dotted leader in its own color down to the ruler body; the dot pattern
    // anchors at the chip's bottom edge (y = 13, stride 2), so y = 21 is an on-dot row inside
    // the tempo row the leader crosses.
    CHECK(image.getPixelAt(100, 21) == editorTheme().section_chip);
    // No section starts near the left edge, so that stretch of the row stays chip-free.
    const juce::Colour row_background = image.getPixelAt(390, 7);
    CHECK(countGlyphPixels(image, juce::Rectangle<int>{4, 2, 40, 11}, row_background) == 0);
}

// The tab lane's spans stand their label chips on the ruler's bottom edge, each at its own span's
// start in its span's color, rising no higher than the chip's own height above that edge.
TEST_CASE("TimelineRuler stands span chips on its bottom edge", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    constexpr common::core::TimeRange one_measure_window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{4.0},
    };
    const common::core::TempoMap tempo_map = makeOneMeasureTempoMap(4000);
    constexpr common::core::Fraction grid_note_value{1, 4};
    TimelineRuler ruler;
    ruler.setBounds(0, 0, 401, g_timeline_ruler_height);
    ruler.setTimelineView(one_measure_window, ruler.getWidth(), 0);
    ruler.setGrid(tempo_map, grid_note_value);
    ruler.setGridLines(
        core::visibleTempoGridLines(
            tempo_map, grid_note_value, one_measure_window, ruler.getWidth(), 0, ruler.getWidth()));
    ruler.setProjectLoaded(true);
    ruler.setSpanStarts(
        std::vector<RulerSpanStart>{
            RulerSpanStart{.seconds = 1.0, .arpeggio = false},
            RulerSpanStart{.seconds = 2.0, .arpeggio = true},
        });

    const juce::Image image = ruler.createComponentSnapshot(ruler.getLocalBounds());

    // One measure across 401px maps seconds * 100: the chord chip's corner at x = 100, the
    // arpeggio's at x = 200, both probed at their middles, clear of the beat ticks they cover.
    const int height =
        juce::roundToInt(common::ui::tabSpanChipHeight(common::ui::tabSpanChipFont()));
    const int middle_y = g_timeline_ruler_height - (height / 2);
    CHECK(image.getPixelAt(100 + (height / 2), middle_y) == common::ui::tabShapeMarkColor(false));
    CHECK(image.getPixelAt(200 + (height / 2), middle_y) == common::ui::tabShapeMarkColor(true));
    CHECK(
        image.getPixelAt(100 + (height / 2), g_timeline_ruler_height - height - 1) ==
        editorTheme().timeline_ruler_background);
}

// A chip is an object, not a position: clicking one reports the section it stands for and seeks
// nothing, which is what lets the section selection survive the cursor-move rule that clears it.
// Clicking off every chip falls back to the ruler's cursor placement.
TEST_CASE("TimelineRuler section chips report clicks by position", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    constexpr common::core::TimeRange one_measure_window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{4.0},
    };
    const common::core::TempoMap tempo_map = makeOneMeasureTempoMap(4000);
    constexpr common::core::Fraction grid_note_value{1, 4};
    constexpr common::core::GridPosition verse_position{.measure = 1, .beat = 2};

    TimelineRuler ruler;
    RecordingRulerListener listener;
    ruler.setBounds(0, 0, 401, g_timeline_ruler_height);
    ruler.setTimelineView(one_measure_window, ruler.getWidth(), 0);
    ruler.setGrid(tempo_map, grid_note_value);
    ruler.setGridLines(
        core::visibleTempoGridLines(
            tempo_map, grid_note_value, one_measure_window, ruler.getWidth(), 0, ruler.getWidth()));
    ruler.setProjectLoaded(true);
    ruler.setListener(listener);
    // The chip verbs are marker verbs, so the plane has to be OPEN for a click to reach one.
    ruler.setMarkerEditsEnabled(true);
    int placement_count = 0;
    ruler.setCursorPlacementCallback(
        [&placement_count](common::core::TimePosition) { placement_count += 1; });
    ruler.setSectionLabels(
        std::vector<RulerSectionLabel>{
            RulerSectionLabel{
                .seconds = 1.0,
                .position = verse_position,
                .name = "Verse",
                .selected = false,
            },
        });

    // The "Verse" chip's left edge sits at x = 100 on the section row (y = 2, 11px tall).
    ruler.mouseDown(makeMouseDownEvent(ruler, 104.0f, 7.0f));
    REQUIRE(listener.last_selected.has_value());
    if (listener.last_selected.has_value())
    {
        CHECK(*listener.last_selected == verse_position);
    }
    CHECK(placement_count == 0);

    // A double-click on the chip asks for the selection's restate, which the core publishes as the
    // selected section's rename.
    ruler.mouseDoubleClick(makeMouseDownEvent(ruler, 104.0f, 7.0f));
    CHECK(listener.restate_count == 1);

    // A click in the ruler body misses every chip and seeks.
    ruler.mouseDown(makeMouseDownEvent(ruler, 200.0f, 55.0f));
    CHECK(placement_count == 1);
    CHECK(listener.select_count == 1);

    // The tempo and signature chips are objects too: each names the marker it stands for and seeks
    // nothing. Both of this map's chips sit at x = 0, on the tempo row (y = 16) and the signature
    // row (y = 30).
    ruler.mouseDown(makeMouseDownEvent(ruler, 4.0f, 21.0f));
    CHECK(
        listener.last_selected_tempo_anchor ==
        std::optional{common::core::GridPosition{.measure = 1, .beat = 1}});
    ruler.mouseDown(makeMouseDownEvent(ruler, 4.0f, 35.0f));
    CHECK(listener.last_selected_signature_measure == std::optional{1});
    CHECK(placement_count == 1);

    // A double-click on the tempo chip asks for the selection's restate — the core's tempo prompt
    // — rather than naming a prompt of its own.
    ruler.mouseDoubleClick(makeMouseDownEvent(ruler, 4.0f, 21.0f));
    CHECK(listener.restate_count == 2);

    // With the marker plane closed, the double-click opens nothing.
    ruler.setMarkerEditsEnabled(false);
    ruler.mouseDoubleClick(makeMouseDownEvent(ruler, 4.0f, 21.0f));
    CHECK(listener.restate_count == 2);
}

// The tempo menu's "Add Tempo Anchor" reads the right-click at the placement quantum the core
// published, the slot the tempo chord would read: the grid line with snap on, the tick with it off.
TEST_CASE(
    "TimelineRuler tempo insert reads the click at the placement quantum", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    constexpr common::core::TimeRange one_measure_window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{4.0},
    };
    const common::core::TempoMap tempo_map = makeOneMeasureTempoMap(4000);

    TimelineRuler ruler;
    ruler.setBounds(0, 0, 401, g_timeline_ruler_height);
    ruler.setTimelineView(one_measure_window, ruler.getWidth(), 0);

    // x = 160 is 1.6 s, 0.6 of a beat into 1:2 at one beat per second.
    const juce::Point<float> click{160.0f, 21.0f};
    ruler.setGrid(tempo_map, common::core::Fraction{1, 8});
    CHECK(
        ruler.tempoInsertPositionAt(click) ==
        std::optional{common::core::GridPosition{
            .measure = 1, .beat = 2, .offset = common::core::Fraction{1, 2}
        }});

    ruler.setGrid(tempo_map, common::core::g_tick_quantum_note_value);
    CHECK(
        ruler.tempoInsertPositionAt(click) ==
        std::optional{common::core::GridPosition{
            .measure = 1, .beat = 2, .offset = common::core::Fraction{3, 5}
        }});
}

// A selected chip is placed before its neighbours, so on a dense map the neighbours give way
// instead of the selection: the keyboard's walk onto the tempo row must always show what it
// selected.
TEST_CASE("TimelineRuler never suppresses the selected tempo chip", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    constexpr common::core::TimeRange window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{4.0},
    };
    // An anchor on every beat, a tenth of a second apart: 10 px apart across 401 px, far closer
    // than one tempo chip is wide.
    std::vector<common::core::BeatAnchor> anchors;
    for (int index = 0; index <= 40; ++index)
    {
        anchors.push_back(
            common::core::BeatAnchor{
                .position = {.measure = (index / 4) + 1, .beat = (index % 4) + 1, .offset = {}},
                .milliseconds = std::int64_t{index} * 100,
            });
    }
    const common::core::TempoMap tempo_map{
        std::vector{
            common::core::TimeSignatureChange{
                .measure = 1, .meter = {.numerator = 4, .denominator = 4}
            },
        },
        std::move(anchors),
    };
    constexpr common::core::Fraction grid_note_value{1, 4};

    TimelineRuler ruler;
    RecordingRulerListener listener;
    ruler.setBounds(0, 0, 401, g_timeline_ruler_height);
    ruler.setTimelineView(window, ruler.getWidth(), 0);
    ruler.setGrid(tempo_map, grid_note_value);
    ruler.setGridLines(
        core::visibleTempoGridLines(
            tempo_map, grid_note_value, window, ruler.getWidth(), 0, ruler.getWidth()));
    ruler.setProjectLoaded(true);
    ruler.setListener(listener);
    // The chip verbs are marker verbs, so the plane has to be OPEN for a click to reach one.
    ruler.setMarkerEditsEnabled(true);

    // Unselected, the first chip covers the fourth anchor's column (x = 30), which draws no chip.
    constexpr common::core::GridPosition fourth_beat{.measure = 1, .beat = 4};
    ruler.mouseDown(makeMouseDownEvent(ruler, 33.0f, 21.0f));
    CHECK(
        listener.last_selected_tempo_anchor ==
        std::optional{common::core::GridPosition{.measure = 1, .beat = 1}});

    ruler.setSelectedTempoMapChips(fourth_beat, std::nullopt);
    ruler.mouseDown(makeMouseDownEvent(ruler, 33.0f, 21.0f));
    CHECK(listener.last_selected_tempo_anchor == std::optional{fourth_beat});
}

// A refused anchor move glows red just outside its chip — the refusal flash, on the ruler — and a
// flash naming an anchor that draws no chip glows nothing.
TEST_CASE("TimelineRuler glows a refused tempo chip", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    constexpr common::core::TimeRange one_measure_window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{4.0},
    };
    const common::core::TempoMap tempo_map = makeOneMeasureTempoMap(4000);
    constexpr common::core::Fraction grid_note_value{1, 4};
    TimelineRuler ruler;
    ruler.setBounds(0, 0, 401, g_timeline_ruler_height);
    ruler.setTimelineView(one_measure_window, ruler.getWidth(), 0);
    ruler.setGrid(tempo_map, grid_note_value);
    ruler.setGridLines(
        core::visibleTempoGridLines(
            tempo_map, grid_note_value, one_measure_window, ruler.getWidth(), 0, ruler.getWidth()));
    ruler.setProjectLoaded(true);
    // The 1:1 chip spans the tempo row (y 16 to 27) from x = 0; the probe is the pixel row just
    // below its bottom edge, where the glow is strongest and no leader runs.
    const auto probe = [&ruler] {
        return ruler.createComponentSnapshot(ruler.getLocalBounds()).getPixelAt(10, 27);
    };
    const juce::Colour plain = probe();
    REQUIRE(
        ruler.createComponentSnapshot(ruler.getLocalBounds()).getPixelAt(1, 21) ==
        editorTheme().tempo_chip);

    ruler.flashTempoAnchorRefusal(common::core::GridPosition{.measure = 3, .beat = 1});
    CHECK(probe() == plain);

    ruler.flashTempoAnchorRefusal(common::core::GridPosition{.measure = 1, .beat = 1});
    CHECK(probe().getRed() > plain.getRed());
}

// The signature menu offers the rows that apply where it opens: Add on a measure whose downbeat
// carries no time signature, Set and Delete over a chip. Every row is disabled while the marker
// plane is closed, and building the menu selects nothing.
TEST_CASE("TimelineRuler signature menu offers the rows that apply", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TimelineRuler ruler;
    RecordingRulerListener listener;
    showMeterChangeMap(ruler);
    ruler.setListener(listener);
    ruler.setMarkerEditsEnabled(true);

    using Rows = std::vector<std::pair<std::string, bool>>;
    const Rows on_chip{{"Set Time Signature...", true}, {"Delete Time Signature", true}};
    // The measure-2 chip, and the measure-1 chip: the downbeat each stands on already carries one.
    CHECK(menuRows(ruler.signatureMenuAt({164.0f, 35.0f})) == on_chip);
    CHECK(menuRows(ruler.signatureMenuAt({4.0f, 35.0f})) == on_chip);
    // Inside measure 3 (7.5 s), off every chip: only the insert.
    CHECK(
        menuRows(ruler.signatureMenuAt({300.0f, 35.0f})) == Rows{{"Add Time Signature...", true}});
    CHECK_FALSE(listener.last_selected_signature_measure.has_value());

    ruler.setMarkerEditsEnabled(false);
    CHECK(
        menuRows(ruler.signatureMenuAt({164.0f, 35.0f})) ==
        Rows{{"Set Time Signature...", false}, {"Delete Time Signature", false}});
    CHECK(
        menuRows(ruler.signatureMenuAt({300.0f, 35.0f})) == Rows{{"Add Time Signature...", false}});
}

// The tempo menu offers the rows that apply where it opens: Add at the clicked slot, Set and Delete
// over a chip, and Scale Tempo everywhere, since it acts on the whole song. Every row is disabled
// while the marker plane is closed, and building the menu selects nothing.
TEST_CASE("TimelineRuler tempo menu offers the rows that apply", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TimelineRuler ruler;
    RecordingRulerListener listener;
    showMeterChangeMap(ruler);
    ruler.setListener(listener);
    ruler.setMarkerEditsEnabled(true);

    using Rows = std::vector<std::pair<std::string, bool>>;
    // The 1:1 chip spans the tempo row (y 16 to 27) from x = 0.
    CHECK(
        menuRows(ruler.tempoMenuAt({4.0f, 21.0f})) == Rows{
                                                          {"Add Tempo Anchor", true},
                                                          {"Set Tempo...", true},
                                                          {"Delete Tempo Anchor", true},
                                                          {"Scale Tempo...", true},
                                                      });
    CHECK(
        menuRows(ruler.tempoMenuAt({300.0f, 21.0f})) ==
        Rows{{"Add Tempo Anchor", true}, {"Scale Tempo...", true}});
    CHECK_FALSE(listener.last_selected_tempo_anchor.has_value());
    CHECK(listener.scale_tempo_count == 0);

    ruler.setMarkerEditsEnabled(false);
    CHECK(
        menuRows(ruler.tempoMenuAt({300.0f, 21.0f})) ==
        Rows{{"Add Tempo Anchor", false}, {"Scale Tempo...", false}});
}

// A double-click on a signature chip asks for the selection's restate, the core's meter prompt, as
// the section and tempo chips do; with the marker plane closed it opens nothing.
TEST_CASE("TimelineRuler signature chip double-click restates", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TimelineRuler ruler;
    RecordingRulerListener listener;
    showMeterChangeMap(ruler);
    ruler.setListener(listener);
    ruler.setMarkerEditsEnabled(true);

    ruler.mouseDown(makeMouseDownEvent(ruler, 164.0f, 35.0f));
    CHECK(listener.last_selected_signature_measure == std::optional{2});
    ruler.mouseDoubleClick(makeMouseDownEvent(ruler, 164.0f, 35.0f));
    CHECK(listener.restate_count == 1);

    ruler.setMarkerEditsEnabled(false);
    ruler.mouseDoubleClick(makeMouseDownEvent(ruler, 164.0f, 35.0f));
    CHECK(listener.restate_count == 1);
}

// The renumber flash lights the measure numbers from the named measure on and leaves the ones
// before it as they were. The probes compare a snapshot with itself before the flash, so they
// hold on every platform's glyph rasterizer: each covers a number's text, clear of its tick.
TEST_CASE("TimelineRuler flashes the renumbered measure numbers", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TimelineRuler ruler;
    showMeterChangeMap(ruler);
    const juce::Image before = ruler.createComponentSnapshot(ruler.getLocalBounds());

    ruler.flashRenumberedMeasures(2);
    const juce::Image after = ruler.createComponentSnapshot(ruler.getLocalBounds());
    CHECK_FALSE(regionDiffers(before, after, juce::Rectangle<int>{2, 45, 16, 11}));
    CHECK(regionDiffers(before, after, juce::Rectangle<int>{162, 45, 16, 11}));
    CHECK(regionDiffers(before, after, juce::Rectangle<int>{282, 45, 16, 11}));
}

// A press on a tempo chip selects it, as a click does, and arms its drag: each event past JUCE's
// drag threshold asks for the anchor's press time plus the pointer's travel, grab-point relative,
// so the chip never jumps to the pointer. Inside the threshold the press stays a click, and the
// double-click still restates.
TEST_CASE("TimelineRuler tempo chip drag asks for the pointer's travel", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    constexpr common::core::TimeRange one_measure_window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{4.0},
    };
    // 401 px over 4 s is 100 px per second, so the 1:3 anchor at 2 s stands at x = 200.
    const common::core::TempoMap tempo_map{
        std::vector{
            common::core::TimeSignatureChange{
                .measure = 1, .meter = {.numerator = 4, .denominator = 4}
            },
        },
        std::vector{anchor("1:1", 0), anchor("1:3", 2000), anchor("2:1", 4000)},
    };
    constexpr common::core::Fraction grid_note_value{1, 4};
    constexpr common::core::GridPosition third_beat{.measure = 1, .beat = 3};

    TimelineRuler ruler;
    RecordingRulerListener listener;
    ruler.setBounds(0, 0, 401, g_timeline_ruler_height);
    ruler.setTimelineView(one_measure_window, ruler.getWidth(), 0);
    ruler.setGrid(tempo_map, grid_note_value);
    ruler.setGridLines(
        core::visibleTempoGridLines(
            tempo_map, grid_note_value, one_measure_window, ruler.getWidth(), 0, ruler.getWidth()));
    ruler.setProjectLoaded(true);
    ruler.setListener(listener);
    ruler.setMarkerEditsEnabled(true);
    int placement_count = 0;
    ruler.setCursorPlacementCallback(
        [&placement_count](common::core::TimePosition) { placement_count += 1; });

    SECTION("a drag past the threshold moves the anchor by the pointer's travel")
    {
        ruler.mouseDown(makeMouseDownEvent(ruler, 204.0f, 21.0f));
        CHECK(listener.last_selected_tempo_anchor == std::optional{third_beat});

        // 24 px right of the press is 0.24 s later, rounded once to whole milliseconds.
        ruler.mouseDrag(testing::makeMouseDragEvent(ruler, 228.0f, 21.0f, 204.0f, 21.0f));
        CHECK(listener.move_count == 1);
        CHECK(listener.last_moved_tempo_anchor == std::optional{third_beat});
        CHECK(listener.last_moved_tempo_milliseconds == std::optional<std::int64_t>{2240});

        // Off the chip row and left of the press: still the press time plus the travel.
        ruler.mouseDrag(testing::makeMouseDragEvent(ruler, 184.0f, 60.0f, 204.0f, 21.0f));
        CHECK(listener.move_count == 2);
        CHECK(listener.last_moved_tempo_milliseconds == std::optional<std::int64_t>{1800});

        // The release reports the drag's end once and disarms it; nothing moves or reports
        // afterwards.
        ruler.mouseUp(testing::makeMouseDragEvent(ruler, 184.0f, 60.0f, 204.0f, 21.0f));
        CHECK(listener.release_count == 1);
        ruler.mouseDrag(testing::makeMouseDragEvent(ruler, 250.0f, 21.0f, 204.0f, 21.0f));
        ruler.mouseUp(testing::makeMouseDragEvent(ruler, 250.0f, 21.0f, 204.0f, 21.0f));
        CHECK(listener.move_count == 2);
        CHECK(listener.release_count == 1);
        CHECK(placement_count == 0);
    }
    SECTION("a press released inside the threshold is a click")
    {
        ruler.mouseDown(makeMouseDownEvent(ruler, 204.0f, 21.0f));
        ruler.mouseDrag(makeMouseDownEvent(ruler, 205.0f, 21.0f));
        ruler.mouseUp(makeMouseDownEvent(ruler, 205.0f, 21.0f));
        CHECK(listener.last_selected_tempo_anchor == std::optional{third_beat});
        CHECK(listener.move_count == 0);
        // The release is reported all the same; the core knows nothing was held.
        CHECK(listener.release_count == 1);

        ruler.mouseDoubleClick(makeMouseDownEvent(ruler, 204.0f, 21.0f));
        CHECK(listener.restate_count == 1);
        CHECK(listener.move_count == 0);
        CHECK(placement_count == 0);
    }
    SECTION("with the marker plane closed the press seeks and arms nothing")
    {
        ruler.setMarkerEditsEnabled(false);
        ruler.mouseDown(makeMouseDownEvent(ruler, 204.0f, 21.0f));
        ruler.mouseDrag(testing::makeMouseDragEvent(ruler, 228.0f, 21.0f, 204.0f, 21.0f));
        ruler.mouseUp(testing::makeMouseDragEvent(ruler, 228.0f, 21.0f, 204.0f, 21.0f));
        CHECK(placement_count == 1);
        CHECK(listener.move_count == 0);
        CHECK(listener.release_count == 0);
        CHECK_FALSE(listener.last_selected_tempo_anchor.has_value());
    }
}

// Verifies the measure-number row pins the active measure to the left edge while scrolled,
// matching the pinned tempo and signature: a mid-measure scroll position would otherwise show
// no number until the next downbeat enters the view.
TEST_CASE("TimelineRuler pins the active measure number while scrolled", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    // Two 4/4 measures across an 801px canvas map content x to seconds * 100; a 401px ruler
    // scrolled to view x 200 puts the left edge at 2.0s, mid-measure 1, with measure 1's own
    // downbeat scrolled out of view.
    constexpr common::core::TimeRange two_measure_window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{8.0},
    };
    constexpr int content_width = 801;
    constexpr int view_x = 200;
    const common::core::TempoMap tempo_map{
        std::vector{
            common::core::TimeSignatureChange{
                .measure = 1, .meter = {.numerator = 4, .denominator = 4}
            },
        },
        std::vector{
            anchor("1:1", 0),
            anchor("3:1", 8000),
        },
    };
    TimelineRuler ruler;
    ruler.setBounds(0, 0, 401, g_timeline_ruler_height);
    ruler.setTimelineView(two_measure_window, content_width, view_x);
    ruler.setGrid(tempo_map, common::core::Fraction{1, 4});
    ruler.setGridLines(
        core::visibleTempoGridLines(
            tempo_map,
            common::core::Fraction{1, 4},
            two_measure_window,
            content_width,
            view_x,
            view_x + 401));
    ruler.setProjectLoaded(true);

    const juce::Image image = ruler.createComponentSnapshot(ruler.getLocalBounds());

    // The pinned "1" is the only glyph in the left label region of the measure-number row: the
    // nearest scrolling label (measure 2 at local x 200) cannot reach it, and the chip leaders
    // end above the ruler body. Compare against the ruler chrome sampled on the same row far
    // from any label or downbeat tick.
    const juce::Colour row_background = image.getPixelAt(390, 51);
    CHECK(countGlyphPixels(image, juce::Rectangle<int>{4, 46, 20, 10}, row_background) > 0);
}

// Verifies the pinned measure number yields to the incoming downbeat number: once the next
// downbeat scrolls close enough that pinning would suppress its number, the pin vanishes and
// the incoming number keeps scrolling until it reaches the left edge and becomes the new pin.
TEST_CASE(
    "TimelineRuler pinned measure number yields to the incoming downbeat", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    // Two 4/4 measures across an 801px canvas map content x to seconds * 100; view x 385 puts
    // measure 2's downbeat at local x 15, inside the collision distance of the pinned "1".
    constexpr common::core::TimeRange two_measure_window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{8.0},
    };
    constexpr int content_width = 801;
    constexpr int view_x = 385;
    const common::core::TempoMap tempo_map{
        std::vector{
            common::core::TimeSignatureChange{
                .measure = 1, .meter = {.numerator = 4, .denominator = 4}
            },
        },
        std::vector{
            anchor("1:1", 0),
            anchor("3:1", 8000),
        },
    };
    TimelineRuler ruler;
    ruler.setBounds(0, 0, 401, g_timeline_ruler_height);
    ruler.setTimelineView(two_measure_window, content_width, view_x);
    ruler.setGrid(tempo_map, common::core::Fraction{1, 4});
    ruler.setGridLines(
        core::visibleTempoGridLines(
            tempo_map,
            common::core::Fraction{1, 4},
            two_measure_window,
            content_width,
            view_x,
            view_x + 401));
    ruler.setProjectLoaded(true);

    const juce::Image image = ruler.createComponentSnapshot(ruler.getLocalBounds());

    // The pin region at the left inset must be empty because measure 2's number, anchored at
    // local x 15 and drawn from x 19, has taken over the row; the measure tick at x 15 spans the
    // body between the two sampled regions and touches neither.
    const juce::Colour row_background = image.getPixelAt(390, 51);
    CHECK(countGlyphPixels(image, juce::Rectangle<int>{4, 46, 10, 10}, row_background) == 0);
    CHECK(countGlyphPixels(image, juce::Rectangle<int>{19, 46, 16, 10}, row_background) > 0);
}

// Verifies the pinned tempo marking yields to an incoming tempo change's marking the same way
// the measure pin does.
TEST_CASE(
    "TimelineRuler pinned tempo marking yields to the incoming change", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    // Anchors at 0s (60bpm span), 4s (120bpm span), and a 6s terminal across an 801px canvas;
    // view x 376 puts the 4s tempo change at local x 24, inside the collision distance of the
    // much wider pinned marking.
    constexpr common::core::TimeRange timeline_window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{8.0},
    };
    constexpr int content_width = 801;
    constexpr int view_x = 376;
    const common::core::TempoMap tempo_map{
        std::vector{
            common::core::TimeSignatureChange{
                .measure = 1, .meter = {.numerator = 4, .denominator = 4}
            },
        },
        std::vector{
            anchor("1:1", 0),
            anchor("2:1", 4000),
            anchor("3:1", 6000),
        },
    };
    TimelineRuler ruler;
    ruler.setBounds(0, 0, 401, g_timeline_ruler_height);
    ruler.setTimelineView(timeline_window, content_width, view_x);
    ruler.setGrid(tempo_map, common::core::Fraction{1, 4});
    ruler.setGridLines(
        core::visibleTempoGridLines(
            tempo_map,
            common::core::Fraction{1, 4},
            timeline_window,
            content_width,
            view_x,
            view_x + 401));
    ruler.setProjectLoaded(true);

    const juce::Image image = ruler.createComponentSnapshot(ruler.getLocalBounds());

    // The tempo row's pin region must be empty because the incoming "♩=120.00" chip, anchored
    // at local x 24, has taken over the row; the sampled regions contain no leader columns, so
    // only chip pixels can land in them. The row background is the ruler chrome sampled right
    // of the incoming chip.
    const juce::Colour row_background = image.getPixelAt(390, 21);
    CHECK(countGlyphPixels(image, juce::Rectangle<int>{4, 16, 18, 11}, row_background) == 0);
    CHECK(countGlyphPixels(image, juce::Rectangle<int>{28, 16, 18, 11}, row_background) > 0);
}

// Verifies the pinned time signature yields to an incoming signature change's label the same way
// the measure pin does.
TEST_CASE(
    "TimelineRuler pinned time signature yields to the incoming change", "[ui][timeline-ruler]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;

    // A 4/4 measure then a 3/4 measure across an 801px canvas; view x 376 puts the signature
    // change's downbeat at 4s at local x 24, inside the collision distance of the pinned "4/4".
    constexpr common::core::TimeRange timeline_window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{8.0},
    };
    constexpr int content_width = 801;
    constexpr int view_x = 376;
    const common::core::TempoMap tempo_map{
        std::vector{
            common::core::TimeSignatureChange{
                .measure = 1, .meter = {.numerator = 4, .denominator = 4}
            },
            common::core::TimeSignatureChange{
                .measure = 2, .meter = {.numerator = 3, .denominator = 4}
            },
        },
        std::vector{
            anchor("1:1", 0),
            anchor("2:1", 4000),
            anchor("3:1", 7000),
        },
    };
    TimelineRuler ruler;
    ruler.setBounds(0, 0, 401, g_timeline_ruler_height);
    ruler.setTimelineView(timeline_window, content_width, view_x);
    ruler.setGrid(tempo_map, common::core::Fraction{1, 4});
    ruler.setGridLines(
        core::visibleTempoGridLines(
            tempo_map,
            common::core::Fraction{1, 4},
            timeline_window,
            content_width,
            view_x,
            view_x + 401));
    ruler.setProjectLoaded(true);

    const juce::Image image = ruler.createComponentSnapshot(ruler.getLocalBounds());

    // The signature row's pin region must be empty because the incoming "3/4" chip, anchored at
    // local x 24, has taken over the row; the sampled regions contain no leader columns (the
    // tempo leader at x 24 falls between them), so only chip pixels can land in them. The row
    // background is the ruler chrome sampled right of the incoming chip.
    const juce::Colour row_background = image.getPixelAt(390, 35);
    CHECK(countGlyphPixels(image, juce::Rectangle<int>{4, 30, 18, 11}, row_background) == 0);
    CHECK(countGlyphPixels(image, juce::Rectangle<int>{28, 30, 18, 11}, row_background) > 0);
}

// Verifies the transport strip readout shows the REAPER-style measure.beat.hundredths position
// with the timeline time for a loaded project, and falls back to plain time without one.
TEST_CASE("EditorView transport readout tracks the transport position", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1280, 800);
    auto& position_display =
        findRequiredDescendant<juce::Label>(view, "transport_position_display");
    CHECK(position_display.getText() == "0:00:000");

    // One 4/4 measure over 4.0s is one beat per second, so 1.5s sits halfway through measure 1
    // beat 2.
    auto state = makeLoadedEditorState(4.0);
    setSessionTempoMap(state, makeOneMeasureTempoMap(4000));
    transport.current_position = common::core::TimePosition{1.5};
    view.setState(state);

    CHECK(position_display.getText() == "1.2.50 / 0:01:500");
}

// Verifies a mid-span downbeat displays as its own measure start. The seconds-to-beat inverse of
// forward-mapped downbeat seconds can round to just under the whole beat (e.g. 3.9999...), and a
// readout that floored that raw would show 1.4.99 with the cursor exactly on measure 2.
TEST_CASE("EditorView position readout lands on measure starts", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1280, 800);

    // Two 4/4 measures over 7.3s: measure 2's downbeat is not an anchor, so its seconds resolve
    // through anchor-span interpolation (4 of 8 beats = 3.65s) and the inverse is inexact.
    const common::core::TempoMap two_measure_map{
        std::vector{
            common::core::TimeSignatureChange{
                .measure = 1, .meter = {.numerator = 4, .denominator = 4}
            },
        },
        std::vector{
            anchor("1:1", 0),
            anchor("3:1", 7300),
        },
    };
    auto state = makeLoadedEditorState(7.3);
    setSessionTempoMap(state, two_measure_map);
    transport.current_position = common::core::TimePosition{two_measure_map.secondsAtBeat(2, 1)};
    view.setState(state);

    auto& position_display =
        findRequiredDescendant<juce::Label>(view, "transport_position_display");
    CHECK(position_display.getText() == "2.1.00 / 0:03:650");
}

// Verifies the default zoom maps four seconds of timeline to the canonical width.
TEST_CASE("EditorView default zoom maps four seconds", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1280, 800);
    view.setState(makeLoadedEditorState(20.0));

    auto& track_viewport = findRequiredDescendant<juce::Component>(view, "track_viewport");
    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    auto& cursor_overlay = findRequiredDescendant<juce::Component>(view, "cursor_overlay");
    // The canvas is the timeline's 6320 zoomed pixels plus the gutter it keeps before time zero,
    // so the first beat's note head has canvas to draw its left half on. Rows still span the whole
    // canvas, gutter included, which is what keeps a head straddling time zero drawn and clickable
    // rather than clipped at a lane's edge.
    constexpr int timeline_pixels = 6320;
    const int gutter = timelineGutterWidth(viewport);
    CHECK(gutter > 0);
    CHECK(
        track_content.getBounds() ==
        juce::Rectangle<int>{
            0, 0, timeline_pixels + gutter, defaultUsableTrackViewportHeight(viewport)
        });
    CHECK(
        arrangement_view.getBounds() ==
        juce::Rectangle<int>{0, 0, timeline_pixels + gutter, defaultTrackHeight(viewport)});
    CHECK(cursor_overlay.getBounds() == track_content.getLocalBounds());
    CHECK(viewport.getViewWidth() <= track_viewport.getWidth());
    CHECK(viewport.getViewHeight() < track_content.getHeight());
}

// Verifies the leftmost scroll position draws the first beat's note head whole. A head is centered
// on its instant, so half of one at time zero hangs left of the timeline's own start; with the
// timeline flush against the canvas edge that half would have nowhere to draw and would clip away.
TEST_CASE("EditorView draws the first beat's head whole at leftmost scroll", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1600, 1000);
    auto state = makeLoadedEditorState(4.0);
    setSessionTempoMap(state, makeOneMeasureTempoMap(4000));
    state.tab = makeSingleNoteChart(0.0);
    view.setState(state);

    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& track_viewport = findRequiredDescendant<TrackViewport>(view, "track_viewport");
    auto& tab_view = findRequiredDescendant<juce::Component>(view, "tab_view");

    viewport.setViewPosition(0, 0);
    REQUIRE(viewport.getViewPositionX() == 0);

    // The canvas has to start before the timeline does, or the head's left half has nowhere to
    // draw however wide the head is.
    const int origin_x = canvasColumnForTime(track_viewport, track_content, 0.0);
    REQUIRE(origin_x > 0);
    // Off the string line the head straddles, but well inside the head's own height, so ink at
    // this row is the head and nothing else; nothing else draws left of the timeline's start.
    // The note sits on string 1 of the chart's six, and no minimum adds lanes below it.
    const int lane_y = static_cast<int>(std::round(tabLaneCenterY(1, 6, tab_view.getBounds())));
    const int sample_y = lane_y - 6;
    REQUIRE(sample_y > 0);

    const juce::Image image = track_content.createComponentSnapshot(track_content.getLocalBounds());
    // The canvas edge, which the head must not have reached.
    const juce::Colour lane_background = image.getPixelAt(1, sample_y);
    // Discrimination guard: the sampled row really does carry the head, so an empty scan below
    // means the head moved or clipped rather than that the row was picked wrong.
    REQUIRE(image.getPixelAt(origin_x, sample_y) != lane_background);

    int leftmost_head_x = origin_x;
    for (int x = 0; x < origin_x; ++x)
    {
        if (image.getPixelAt(x, sample_y) != lane_background)
        {
            leftmost_head_x = x;
            break;
        }
    }

    // Painted left of the instant it is centered on, and clear of the canvas edge: the whole head.
    CHECK(leftmost_head_x < origin_x);
    CHECK(leftmost_head_x > 0);
}

// Verifies a newly loaded project scrolls the timeline to the restored transport cursor.
TEST_CASE("EditorView project load centers restored cursor", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1280, 800);
    const auto state = makeLoadedEditorState(20.0);
    transport.current_position = common::core::TimePosition{15.0};
    view.setState(state);

    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& track_viewport = findRequiredDescendant<TrackViewport>(view, "track_viewport");
    const double screen_x =
        static_cast<double>(
            canvasXForTime(track_viewport, track_content, transport.current_position.seconds)) -
        static_cast<double>(viewport.getViewPositionX());
    CHECK(
        screen_x == Catch::Approx(static_cast<double>(viewport.getViewWidth()) / 2.0).margin(1.0));
}

// Verifies clearing a failed open's busy state does not recenter the still-loaded project.
TEST_CASE("EditorView failed open keeps viewport position", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1280, 800);
    const auto state = makeLoadedEditorState(20.0);
    view.setState(state);

    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    viewport.setViewPosition(0, 0);
    transport.current_position = common::core::TimePosition{15.0};

    auto opening_state = state;
    opening_state.busy = core::BusyViewState{
        .operation = core::BusyOperation::OpeningProject,
        .message = "Opening project...",
    };
    view.setState(opening_state);
    view.setState(state);

    CHECK(viewport.getViewPositionX() == 0);
}

// Verifies loading a different project over an open one recenters on the new project's cursor,
// recognized by a changed project_load_id rather than a content diff.
TEST_CASE("EditorView project reload centers on new load id", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1280, 800);

    auto first_load = makeLoadedEditorState(20.0);
    first_load.project_load_id = 1;
    transport.current_position = common::core::TimePosition{6.0};
    view.setState(first_load);

    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    viewport.setViewPosition(0, 0);

    auto second_load = makeLoadedEditorState(20.0);
    second_load.project_load_id = 2;
    transport.current_position = common::core::TimePosition{12.0};
    view.setState(second_load);

    auto& track_viewport = findRequiredDescendant<TrackViewport>(view, "track_viewport");
    const double screen_x =
        static_cast<double>(
            canvasXForTime(track_viewport, track_content, transport.current_position.seconds)) -
        static_cast<double>(viewport.getViewPositionX());
    CHECK(
        screen_x == Catch::Approx(static_cast<double>(viewport.getViewWidth()) / 2.0).margin(1.0));
}

// Verifies mouse wheel zoom scales the timeline content instead of seeking transport.
TEST_CASE("EditorView wheel zoom scales track width", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1280, 800);
    view.setState(makeLoadedEditorState(20.0));

    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    const int default_width = track_content.getWidth();

    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f),
        juce::MouseWheelDetails{
            .deltaX = 0.0f,
            .deltaY = 1.0f,
            .isReversed = false,
            .isSmooth = false,
            .isInertial = false,
        });

    CHECK(track_content.getWidth() > default_width);
    CHECK(controller.timeline_seek_count == 0);
}

// Verifies the tempo grid uses the same scaled content width as the timeline viewport.
TEST_CASE("EditorView tempo grid follows zoomed timeline width", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1280, 800);
    auto state = makeLoadedEditorState(4.0);
    setSessionTempoMap(state, makeOneMeasureTempoMap(4000));
    view.setState(state);

    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& track_viewport = findRequiredDescendant<TrackViewport>(view, "track_viewport");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");

    const auto grid_line_x = [&track_viewport, &track_content](double seconds) {
        return canvasColumnForTime(track_viewport, track_content, seconds);
    };

    const auto grid_line_brightness = [&track_content, &grid_line_x](double seconds, int y) {
        const int x = grid_line_x(seconds);
        const int background_x = std::min(track_content.getWidth() - 1, x + 12);

        const juce::Image image =
            track_content.createComponentSnapshot(track_content.getLocalBounds());
        return std::tuple{
            x,
            image.getPixelAt(x, y).getBrightness(),
            image.getPixelAt(background_x, y).getBrightness(),
        };
    };

    const int waveform_y = gridDotYAtOrAfter(arrangement_view.getHeight() / 2);
    const int lower_track_y = gridDotYAtOrAfter(arrangement_view.getBottom() + 20);
    const int lower_track_gap_y = lower_track_y + 1;
    REQUIRE(lower_track_gap_y < track_content.getHeight());
    const int default_width = track_content.getWidth();
    const auto [default_x, default_waveform_brightness, default_waveform_background] =
        grid_line_brightness(1.0, waveform_y);
    const auto [default_lower_x, default_lower_brightness, default_lower_background] =
        grid_line_brightness(1.0, lower_track_y);
    const auto [default_gap_x, default_gap_brightness, default_gap_background] =
        grid_line_brightness(1.0, lower_track_gap_y);
    CHECK(default_lower_x == default_x);
    CHECK(default_gap_x == default_x);
    CHECK(default_waveform_brightness > default_waveform_background);
    CHECK(std::abs(default_lower_brightness - default_lower_background) > 0.01f);
    CHECK(std::abs(default_gap_brightness - default_gap_background) < 0.01f);

    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f),
        juce::MouseWheelDetails{
            .deltaX = 0.0f,
            .deltaY = 1.0f,
            .isReversed = false,
            .isSmooth = false,
            .isInertial = false,
        });

    const auto [zoomed_x, zoomed_brightness, zoomed_background] =
        grid_line_brightness(1.0, lower_track_y);
    CHECK(track_content.getWidth() > default_width);
    CHECK(zoomed_x > default_x);
    CHECK(std::abs(zoomed_brightness - zoomed_background) > 0.01f);
}

// Verifies the content-level grid is below waveform drawing but above the waveform row background.
TEST_CASE("EditorView tempo grid draws behind the waveform", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1280, 800);
    auto state = makeLoadedEditorState(4.0);
    setSessionTempoMap(state, makeOneMeasureTempoMap(4000));
    view.setState(state);
    REQUIRE(thumbnail_factory.last_thumbnail != nullptr);
    thumbnail_factory.last_thumbnail->fill_color = juce::Colours::lightgreen;

    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    auto& track_viewport = findRequiredDescendant<TrackViewport>(view, "track_viewport");
    {
        const int line_x = canvasColumnForTime(track_viewport, track_content, 1.0);
        const int background_x = std::min(track_content.getWidth() - 1, line_x + 12);
        const int waveform_y = arrangement_view.getHeight() / 2;
        const int lower_track_y = gridDotYAtOrAfter(arrangement_view.getBottom() + 20);
        const int lower_track_gap_y = lower_track_y + 1;
        REQUIRE(lower_track_gap_y < track_content.getHeight());
        const juce::Image image =
            track_content.createComponentSnapshot(track_content.getLocalBounds());

        CHECK(image.getPixelAt(line_x, waveform_y) == juce::Colours::lightgreen);
        CHECK(
            std::abs(
                image.getPixelAt(line_x, lower_track_y).getBrightness() -
                image.getPixelAt(background_x, lower_track_y).getBrightness()) > 0.01f);
        CHECK(
            std::abs(
                image.getPixelAt(line_x, lower_track_gap_y).getBrightness() -
                image.getPixelAt(background_x, lower_track_gap_y).getBrightness()) < 0.01f);
        // The measure line at time zero, which is a gutter in from the canvas edge rather than
        // flush with it, so the first beat's note head has room to draw whole.
        const int origin_x = canvasColumnForTime(track_viewport, track_content, 0.0);
        CHECK(origin_x > 0);
        CHECK(
            std::abs(
                image.getPixelAt(origin_x, lower_track_y).getBrightness() -
                image.getPixelAt(origin_x + 1, lower_track_y).getBrightness()) > 0.01f);
    }
}

// Verifies the grid-snap indicator on the canvas: with snap off the lattice keeps HALF its
// contrast against every row band it crosses, and sinks into none of them. The tone row's
// background is lighter and bluer than the waveform row's, so one pre-mixed near-black cannot
// serve both — quieting composites against whatever is beneath for exactly that reason. The second
// push also pins the ordering in TrackViewport::setGrid: a bare toggle repeats the tempo map and
// the grid note value, so the dots have to requiet from a push that moves no line.
TEST_CASE("EditorView quiets the tempo grid while snap is off", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1280, 800);
    auto state = makeLoadedEditorState(4.0);
    setSessionTempoMap(state, makeOneMeasureTempoMap(4000));
    view.setState(state);

    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    // A sixteenth that is not a whole beat, so the sampled column carries the weakest
    // (subdivision) ink — the rank a wrong quieting ground buries first.
    auto& track_viewport = findRequiredDescendant<TrackViewport>(view, "track_viewport");
    {
        const int line_x = canvasColumnForTime(track_viewport, track_content, 0.25);
        const int background_x = std::min(track_content.getWidth() - 1, line_x + 12);
        const int waveform_y = gridDotYAtOrAfter(arrangement_view.getHeight() / 2);
        const int tone_row_y = gridDotYAtOrAfter(arrangement_view.getBottom() + 10);
        REQUIRE(tone_row_y < track_content.getHeight());

        // Signed on purpose: every grid ink is lighter than every band, so quieting must lower
        // this number without letting it reach zero. A ground-blind pre-mix drives it NEGATIVE on
        // the tone row, which an absolute-difference check would happily pass.
        const auto grid_contrast = [&track_content, line_x, background_x](int y) {
            const juce::Image image =
                track_content.createComponentSnapshot(track_content.getLocalBounds());
            return image.getPixelAt(line_x, y).getBrightness() -
                   image.getPixelAt(background_x, y).getBrightness();
        };

        const float waveform_snapped = grid_contrast(waveform_y);
        const float tone_row_snapped = grid_contrast(tone_row_y);
        REQUIRE(waveform_snapped > 0.0f);
        REQUIRE(tone_row_snapped > 0.0f);

        // The bare toggle: this push repeats the tempo map and the grid note value exactly, so the
        // snap flag is the only thing new about it.
        state.grid_snap = false;
        view.setState(state);

        const float waveform_quiet = grid_contrast(waveform_y);
        const float tone_row_quiet = grid_contrast(tone_row_y);
        CHECK(waveform_quiet > 0.0f);
        CHECK(tone_row_quiet > 0.0f);
        CHECK(waveform_quiet == Catch::Approx(waveform_snapped / 2.0f).margin(0.01));
        CHECK(tone_row_quiet == Catch::Approx(tone_row_snapped / 2.0f).margin(0.01));
    }
}

// Verifies zooming all the way out can fit a long timeline into the viewport.
TEST_CASE("EditorView wheel zoom out fits full timeline", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1280, 800);
    view.setState(makeLoadedEditorState(240.0));

    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    REQUIRE(track_content.getWidth() > viewport.getViewWidth());

    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f),
        juce::MouseWheelDetails{
            .deltaX = 0.0f,
            .deltaY = -100.0f,
            .isReversed = false,
            .isSmooth = false,
            .isInertial = false,
        });

    CHECK(track_content.getWidth() == viewport.getViewWidth());
}

// Verifies wheel zoom pivots on the visible cursor IN PLACE: its screen column is the same after
// the zoom as before, so zoom scales and never scrolls.
TEST_CASE("EditorView wheel zoom holds the visible cursor's column", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1280, 800);
    const auto state = makeLoadedEditorState(20.0);
    view.setState(state);

    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& track_viewport = findRequiredDescendant<TrackViewport>(view, "track_viewport");
    transport.current_position = common::core::TimePosition{10.0};
    viewport.setViewPosition(2800, 0);
    const float initial_cursor_position =
        canvasXForTime(track_viewport, track_content, transport.current_position.seconds);
    REQUIRE(initial_cursor_position >= static_cast<float>(viewport.getViewPositionX()));
    REQUIRE(
        initial_cursor_position <
        static_cast<float>(viewport.getViewPositionX() + viewport.getViewWidth()));
    const double initial_screen_x = static_cast<double>(initial_cursor_position) -
                                    static_cast<double>(viewport.getViewPositionX());

    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f),
        juce::MouseWheelDetails{
            .deltaX = 0.0f,
            .deltaY = 1.0f,
            .isReversed = false,
            .isSmooth = false,
            .isInertial = false,
        });

    const double zoomed_screen_x =
        static_cast<double>(
            canvasXForTime(track_viewport, track_content, transport.current_position.seconds)) -
        static_cast<double>(viewport.getViewPositionX());
    CHECK(zoomed_screen_x == Catch::Approx(initial_screen_x).margin(1.0));
}

// Verifies wheel zoom with the cursor OFF-SCREEN centres the view on it first, so zoom never
// scales around a point the charter cannot see.
TEST_CASE("EditorView wheel zoom centers an offscreen cursor", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1280, 800);
    const auto state = makeLoadedEditorState(20.0);
    view.setState(state);

    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& track_viewport = findRequiredDescendant<TrackViewport>(view, "track_viewport");
    transport.current_position = common::core::TimePosition{15.0};
    viewport.setViewPosition(0, 0);
    const float cursor_position =
        canvasXForTime(track_viewport, track_content, transport.current_position.seconds);
    REQUIRE(
        cursor_position >=
        static_cast<float>(viewport.getViewPositionX() + viewport.getViewWidth()));

    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f),
        juce::MouseWheelDetails{
            .deltaX = 0.0f,
            .deltaY = 1.0f,
            .isReversed = false,
            .isSmooth = false,
            .isInertial = false,
        });

    const double zoomed_screen_x =
        static_cast<double>(
            canvasXForTime(track_viewport, track_content, transport.current_position.seconds)) -
        static_cast<double>(viewport.getViewPositionX());
    CHECK(
        zoomed_screen_x ==
        Catch::Approx(static_cast<double>(viewport.getViewWidth()) / 2.0).margin(1.0));
}

// Delete is kept in sight AFTER it lands, as undo is, and only when the core asks (focus_count). A
// marker's Delete asks for nothing, so the view stays put while the charter walks back along a row,
// even past chips that stand off screen. A Delete that asks — notes under the caret — still brings
// what it left into view.
TEST_CASE("EditorView keeps a Delete in sight only when the core asks", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1280, 800);
    auto state = makeLoadedEditorState(20.0);
    // The selection's first member at the song's opening, the place the walk's last chip stands.
    state.selection_start_seconds = 1.0;
    state.selection_present = true;
    view.setState(state);

    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& track_viewport = findRequiredDescendant<TrackViewport>(view, "track_viewport");
    viewport.setViewPosition(2800, 0);
    REQUIRE(
        canvasXForTime(track_viewport, track_content, 1.0) <
        static_cast<float>(viewport.getViewPositionX()));

    // The command itself judges nothing, though what it acts on stands off screen.
    const juce::ApplicationCommandTarget::InvocationInfo info{toJuceCommandId(
        EditorCommandId::SelectionDelete)};
    CHECK(view.perform(info));
    CHECK(track_viewport.settledViewPositionX() == 2800);

    SECTION("a marker's Delete, which leaves nothing selected and asks for nothing")
    {
        state.selection_start_seconds.reset();
        state.selection_present = false;
        view.setState(state);
        CHECK(track_viewport.settledViewPositionX() == 2800);
    }

    SECTION("a Delete that asks, leaving the caret on the emptied slot")
    {
        ++state.focus_count;
        view.setState(state);
        CHECK(track_viewport.settledViewPositionX() == 0);
    }
}

// Verifies Ctrl composes NOTHING on a timeline seek: the grid-snap mode is the only thing that
// moves the placement quantum, so a Ctrl-click lands exactly where the same plain click does.
TEST_CASE("EditorView Ctrl-click seeks the same slot as a plain click", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};
    view.setBounds(0, 0, 1600, 1000);
    view.setState(
        core::EditorViewState{
            .open_enabled = true,
            .import_enabled = true,
            .save_enabled = true,
            .save_as_enabled = true,
            .export_enabled = true,
            .suggested_export_file = std::filesystem::path{"song.rock"},
            .close_enabled = true,
            .project_loaded = true,
            .save_requires_destination = false,
            .transport =
                core::TransportViewState{
                    .unavailable_reason = std::nullopt,
                    .play_pause_shows_pause_icon = false,
                },
            .visible_timeline =
                common::core::TimeRange{
                    .start = common::core::TimePosition{},
                    .end = common::core::TimePosition{4.0},
                },
            .arrangement = makeArrangementState(std::filesystem::path{"mix.wav"}),
            .signal_chain =
                core::SignalChainViewState{
                    .plugins = {},
                },
            .unsaved_changes_prompt = std::nullopt,
            .save_as_prompt = std::nullopt,
            .busy = std::nullopt,
        });

    auto& cursor_overlay = findRequiredDescendant<juce::Component>(view, "cursor_overlay");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    CHECK(cursor_overlay.isVisible());
    REQUIRE(cursor_overlay.getWidth() > 0);
    const float click_x = std::floor(static_cast<float>(cursor_overlay.getWidth()) * 0.25f) + 0.5f;
    // Seek clicks live in the highway band (the caret model): a click below it — over the tone
    // strip or lanes — never seeks.
    const auto below_band_y = static_cast<float>(cursor_overlay.getHeight() - 20);
    REQUIRE(below_band_y > static_cast<float>(arrangement_view.getBottom()));
    cursor_overlay.mouseDown(makeMouseDownEvent(
        cursor_overlay,
        click_x,
        below_band_y,
        juce::ModifierKeys{
            juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier
        }));
    CHECK(controller.timeline_seek_count == 0);

    const float click_y = 20.0f;
    REQUIRE(click_y < static_cast<float>(arrangement_view.getBottom()));
    cursor_overlay.mouseDown(makeMouseDownEvent(
        cursor_overlay,
        click_x,
        click_y,
        juce::ModifierKeys{
            juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier
        }));

    CHECK(controller.timeline_seek_count == 1);
    const auto ctrl_seek_position = controller.last_seek_position;
    REQUIRE(ctrl_seek_position.has_value());

    // The same pixel without Ctrl: the two answers must be identical, which is the whole claim.
    cursor_overlay.mouseDown(makeMouseDownEvent(cursor_overlay, click_x, click_y));
    CHECK(controller.timeline_seek_count == 2);
    const auto plain_seek_position = controller.last_seek_position;
    REQUIRE(plain_seek_position.has_value());
    if (ctrl_seek_position.has_value() && plain_seek_position.has_value())
    {
        CHECK(ctrl_seek_position->seconds == Catch::Approx(plain_seek_position->seconds));
    }
}

// Verifies unmodified timeline clicks snap to the nearest tempo-grid line.
TEST_CASE("EditorView timeline click snaps to nearest grid line", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1600, 1000);
    auto state = makeLoadedEditorState(4.0);
    setSessionTempoMap(state, makeOneMeasureTempoMap(4000));
    view.setState(state);

    auto& cursor_overlay = findRequiredDescendant<juce::Component>(view, "cursor_overlay");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& track_viewport = findRequiredDescendant<TrackViewport>(view, "track_viewport");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    CHECK(cursor_overlay.isVisible());
    REQUIRE(cursor_overlay.getWidth() > 1);

    {
        // The overlay spans the whole canvas, so its local column is the canvas column.
        const int expected_grid_x = canvasColumnForTime(track_viewport, track_content, 1.0);
        const auto click_x = static_cast<float>(expected_grid_x + 20);
        // Seek clicks live in the highway band (the caret model).
        const float click_y = 20.0f;
        REQUIRE(click_y < static_cast<float>(arrangement_view.getBottom()));
        cursor_overlay.mouseDown(makeMouseDownEvent(cursor_overlay, click_x, click_y));
    }

    CHECK(controller.timeline_seek_count == 1);
    const auto last_seek_position = controller.last_seek_position;
    REQUIRE(last_seek_position.has_value());
    // The snapped seek is the exact beat time, not a value quantized to the beat's pixel column.
    if (last_seek_position.has_value())
    {
        CHECK(last_seek_position->seconds == Catch::Approx(1.0));
    }
}

// Verifies ruler clicks use the same grid-snapped placement as timeline-content clicks.
TEST_CASE("EditorView ruler click snaps to nearest grid line", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1600, 1000);
    auto state = makeLoadedEditorState(4.0);
    setSessionTempoMap(state, makeOneMeasureTempoMap(4000));
    view.setState(state);

    auto& timeline_ruler = findRequiredDescendant<TimelineRuler>(view, "timeline_ruler");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& track_viewport = findRequiredDescendant<TrackViewport>(view, "track_viewport");
    REQUIRE(track_content.getWidth() > 1);

    {
        // Unscrolled, so the pinned ruler's local column is the canvas column below it.
        REQUIRE(
            findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll")
                .getViewPositionX() == 0);
        const int expected_grid_x = canvasColumnForTime(track_viewport, track_content, 1.0);
        const auto click_x = static_cast<float>(expected_grid_x + 20);
        timeline_ruler.mouseDown(makeMouseDownEvent(timeline_ruler, click_x, 10.0f));
    }

    CHECK(controller.timeline_seek_count == 1);
    const auto last_seek_position = controller.last_seek_position;
    REQUIRE(last_seek_position.has_value());
    // The snapped seek is the exact beat time, not a value quantized to the beat's pixel column.
    if (last_seek_position.has_value())
    {
        CHECK(last_seek_position->seconds == Catch::Approx(1.0));
    }
}

// The view forwards a tempo-chip drag to the core's move intent: the press selects the chip, and a
// drag half a second's width to the right asks for the anchor half a second later.
TEST_CASE("EditorView forwards a tempo-chip drag to the move intent", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1600, 1000);
    auto state = makeLoadedEditorState(4.0);
    setSessionTempoMap(
        state,
        common::core::TempoMap{
            std::vector{
                common::core::TimeSignatureChange{
                    .measure = 1, .meter = {.numerator = 4, .denominator = 4}
                },
            },
            std::vector{anchor("1:1", 0), anchor("1:3", 2000), anchor("2:1", 4000)},
        });
    state.marker_edits_enabled = true;
    view.setState(state);

    auto& timeline_ruler = findRequiredDescendant<TimelineRuler>(view, "timeline_ruler");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& track_viewport = findRequiredDescendant<TrackViewport>(view, "track_viewport");
    // Unscrolled, so the pinned ruler's local column is the canvas column below it.
    REQUIRE(
        findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll").getViewPositionX() ==
        0);
    const float press_x = canvasXForTime(track_viewport, track_content, 2.0) + 4.0f;
    const float drag_x = canvasXForTime(track_viewport, track_content, 2.5) + 4.0f;

    constexpr common::core::GridPosition third_beat{.measure = 1, .beat = 3};
    timeline_ruler.mouseDown(makeMouseDownEvent(timeline_ruler, press_x, 21.0f));
    CHECK(controller.last_selected_tempo_anchor == std::optional{third_beat});
    timeline_ruler.mouseDrag(
        testing::makeMouseDragEvent(timeline_ruler, drag_x, 21.0f, press_x, 21.0f));
    CHECK(controller.last_moved_tempo_anchor_position == std::optional{third_beat});
    CHECK(controller.last_moved_tempo_anchor_milliseconds == std::optional<std::int64_t>{2500});
    CHECK(controller.tempo_anchor_drag_release_count == 0);
    timeline_ruler.mouseUp(
        testing::makeMouseDragEvent(timeline_ruler, drag_x, 21.0f, press_x, 21.0f));
    CHECK(controller.tempo_anchor_drag_release_count == 1);
    CHECK(controller.timeline_seek_count == 0);
}

// A tempo-chip drag publishes the map its release would land as the grid map, and the core's
// answer for content as the content map: the grid and the dragged chip follow the grid map in both
// lock states, while content is re-timed to the content map, which is the preview with the Note
// Lock off and the session's map with it on. The session's beats fall every second; the preview
// pulls 1:3 from 2 s to 2.5 s, so its first span's beats fall every 1.25 s and the note on beat 2
// draws at 1.25 s when content follows the beat.
TEST_CASE(
    "EditorView draws the grid from the grid map and content from the content map",
    "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1280, 800);
    const auto map_with_third_beat_at = [](const std::int64_t milliseconds) {
        return common::core::TempoMap{
            std::vector{
                common::core::TimeSignatureChange{
                    .measure = 1, .meter = {.numerator = 4, .denominator = 4}
                },
            },
            std::vector{anchor("1:1", 0), anchor("1:3", milliseconds), anchor("2:1", 4000)},
        };
    };
    auto state = makeLoadedEditorState(4.0);
    setSessionTempoMap(state, map_with_third_beat_at(2000));
    state.grid_map = map_with_third_beat_at(2500);
    state.tab = makeSingleNoteChart(1.0);
    state.selected_tempo_anchor = common::core::GridPosition{.measure = 1, .beat = 3};
    state.marker_edits_enabled = true;

    double content_seconds = 0.0;
    SECTION("the Note Lock off: content moves with the beat")
    {
        state.content_map = state.grid_map;
        content_seconds = 1.25;
    }
    SECTION("the Note Lock on: content holds its time")
    {
        content_seconds = 1.0;
    }
    view.setState(state);

    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& track_viewport = findRequiredDescendant<TrackViewport>(view, "track_viewport");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    auto& timeline_ruler = findRequiredDescendant<TimelineRuler>(view, "timeline_ruler");
    auto& tab_view = findRequiredDescendant<TabView>(view, "tab_view");
    REQUIRE(
        findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll").getViewPositionX() ==
        0);

    const int lower_track_y = gridDotYAtOrAfter(arrangement_view.getBottom() + 20);
    REQUIRE(lower_track_y < track_content.getHeight());
    const juce::Image image = track_content.createComponentSnapshot(track_content.getLocalBounds());
    const auto has_line_at = [&](const double seconds) {
        const int x = canvasColumnForTime(track_viewport, track_content, seconds);
        return std::abs(
                   image.getPixelAt(x, lower_track_y).getBrightness() -
                   image.getPixelAt(x + 12, lower_track_y).getBrightness()) > 0.01f;
    };
    CHECK(has_line_at(1.25));
    CHECK_FALSE(has_line_at(1.0));

    const std::optional<juce::Rectangle<int>> chip = timeline_ruler.selectedChipBounds();
    REQUIRE(chip.has_value());
    if (chip.has_value())
    {
        const float preview_x = canvasXForTime(track_viewport, track_content, 2.5);
        const float session_x = canvasXForTime(track_viewport, track_content, 2.0);
        const auto chip_x = static_cast<float>(chip->getX());
        CHECK(std::abs(chip_x - preview_x) < std::abs(chip_x - session_x));
    }

    const std::optional<juce::Rectangle<float>> head = tab_view.noteHeadBounds(0);
    REQUIRE(head.has_value());
    if (head.has_value())
    {
        const float head_x = static_cast<float>(tab_view.getX()) + head->getCentreX();
        CHECK_THAT(
            head_x,
            Catch::Matchers::WithinAbs(
                canvasXForTime(track_viewport, track_content, content_seconds), 0.5));
    }
}

// Verifies the track grid, overlay snapping, and ruler snapping all use the state note value.
TEST_CASE("EditorView subdivision grid and snapping share spacing", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1600, 1000);
    auto state = makeLoadedEditorState(4.0);
    setSessionTempoMap(state, makeOneMeasureTempoMap(4000));
    state.grid_note_value = common::core::Fraction{1, 8};
    view.setState(state);

    auto& cursor_overlay = findRequiredDescendant<juce::Component>(view, "cursor_overlay");
    auto& timeline_ruler = findRequiredDescendant<TimelineRuler>(view, "timeline_ruler");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    REQUIRE(track_content.getWidth() > 1);

    // The eighth-note (half-beat in 4/4) subdivision at 0.5s draws a grid column in the lower
    // track area.
    auto& track_viewport = findRequiredDescendant<TrackViewport>(view, "track_viewport");
    {
        const int line_x = canvasColumnForTime(track_viewport, track_content, 0.5);
        const int background_x = std::min(track_content.getWidth() - 1, line_x + 12);
        const int lower_track_y = gridDotYAtOrAfter(arrangement_view.getBottom() + 20);
        const juce::Image image =
            track_content.createComponentSnapshot(track_content.getLocalBounds());
        CHECK(
            std::abs(
                image.getPixelAt(line_x, lower_track_y).getBrightness() -
                image.getPixelAt(background_x, lower_track_y).getBrightness()) > 0.01f);

        // Overlay clicks near the subdivision snap to its exact time (inside the highway
        // band, where seek clicks live under the caret model).
        const auto click_x = static_cast<float>(line_x + 10);
        const float click_y = 20.0f;
        REQUIRE(click_y < static_cast<float>(arrangement_view.getBottom()));
        cursor_overlay.mouseDown(makeMouseDownEvent(cursor_overlay, click_x, click_y));
        CHECK(controller.timeline_seek_count == 1);
        REQUIRE(controller.last_seek_position.has_value());
        if (controller.last_seek_position.has_value())
        {
            CHECK(controller.last_seek_position->seconds == Catch::Approx(0.5));
        }

        // Ruler clicks resolve through the same subdivision grid.
        timeline_ruler.mouseDown(makeMouseDownEvent(timeline_ruler, click_x, 10.0f));
        CHECK(controller.timeline_seek_count == 2);
        REQUIRE(controller.last_seek_position.has_value());
        if (controller.last_seek_position.has_value())
        {
            CHECK(controller.last_seek_position->seconds == Catch::Approx(0.5));
        }
    }
}

// Verifies the ruler follows the same rule as the content overlay: Ctrl composes nothing.
TEST_CASE("EditorView ruler Ctrl-click seeks the same slot as a plain click", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1600, 1000);
    const auto state = makeLoadedEditorState(4.0);
    view.setState(state);

    auto& timeline_ruler = findRequiredDescendant<TimelineRuler>(view, "timeline_ruler");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    REQUIRE(track_content.getWidth() > 0);

    const float click_x = std::floor(static_cast<float>(track_content.getWidth()) * 0.25f) + 0.5f;
    timeline_ruler.mouseDown(makeMouseDownEvent(
        timeline_ruler,
        click_x,
        10.0f,
        juce::ModifierKeys{
            juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier
        }));

    CHECK(controller.timeline_seek_count == 1);
    const auto ctrl_seek_position = controller.last_seek_position;
    REQUIRE(ctrl_seek_position.has_value());

    timeline_ruler.mouseDown(makeMouseDownEvent(timeline_ruler, click_x, 10.0f));
    CHECK(controller.timeline_seek_count == 2);
    const auto plain_seek_position = controller.last_seek_position;
    REQUIRE(plain_seek_position.has_value());
    if (ctrl_seek_position.has_value() && plain_seek_position.has_value())
    {
        CHECK(ctrl_seek_position->seconds == Catch::Approx(plain_seek_position->seconds));
    }
}

// Verifies non-primary (right-button) clicks never trigger a timeline seek.
TEST_CASE("EditorView right-click does not seek the timeline", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    view.setBounds(0, 0, 1600, 1000);
    auto state = makeLoadedEditorState(4.0);
    setSessionTempoMap(state, makeOneMeasureTempoMap(4000));
    view.setState(state);

    auto& cursor_overlay = findRequiredDescendant<juce::Component>(view, "cursor_overlay");
    auto& timeline_ruler = findRequiredDescendant<TimelineRuler>(view, "timeline_ruler");
    REQUIRE(cursor_overlay.getWidth() > 1);

    const float click_x = static_cast<float>(cursor_overlay.getWidth()) * 0.25f;
    const juce::ModifierKeys right_button{juce::ModifierKeys::rightButtonModifier};
    cursor_overlay.mouseDown(makeMouseDownEvent(cursor_overlay, click_x, 10.0f, right_button));
    timeline_ruler.mouseDown(makeMouseDownEvent(timeline_ruler, click_x, 10.0f, right_button));

    CHECK(controller.timeline_seek_count == 0);
    CHECK_FALSE(controller.last_seek_position.has_value());
}

// Verifies keyboard play/pause reaches the transport intent through the command mapping set, where
// Space is a registered command.
TEST_CASE("EditorView forwards space key to the controller", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    CHECK(view.getWantsKeyboardFocus());
    juce::KeyListener* const mappings = view.commandManager().getKeyMappings();
    CHECK(mappings->keyPressed(juce::KeyPress{juce::KeyPress::spaceKey}, &view));
    CHECK(controller.play_pause_press_count == 1);
}

// Digits route to the fret intent with or without a selection — the controller owns the
// retype-vs-insert-at-caret branch (the caret model). Dispatch rides the command mapping set:
// digits are registered commands like every other binding.
TEST_CASE("EditorView routes digits to the fret intent", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    core::EditorViewState state = makeLoadedEditorState(20.0);
    auto tab = std::make_shared<common::core::ChartViewState>();
    tab->open_strings = common::core::testing::standardTuning();
    state.tab = std::move(tab);
    view.setState(state);

    juce::KeyListener* const mappings = view.commandManager().getKeyMappings();
    CHECK(mappings->keyPressed(juce::KeyPress{'5', juce::ModifierKeys{}, 0}, &view));
    CHECK(controller.chart_fret_digit_count == 1);
    CHECK(controller.last_chart_fret_digit == 5);
}

// The entry keys' two planes: `Alt`+digit and `Alt+Insert` reach the ring-plane intents, their bare
// twins the note-plane ones, and neither plane's press is counted by the other.
TEST_CASE("EditorView routes the ring-plane entry keys to their own intents", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    core::EditorViewState state = makeLoadedEditorState(20.0);
    auto tab = std::make_shared<common::core::ChartViewState>();
    tab->open_strings = common::core::testing::standardTuning();
    state.tab = std::move(tab);
    view.setState(state);

    juce::KeyListener* const mappings = view.commandManager().getKeyMappings();
    const juce::ModifierKeys alt{juce::ModifierKeys::altModifier};
    CHECK(mappings->keyPressed(juce::KeyPress{'7', alt, 0}, &view));
    CHECK(controller.chart_ring_digit_count == 1);
    CHECK(controller.last_chart_ring_digit == 7);
    CHECK(controller.chart_fret_digit_count == 0);

    CHECK(mappings->keyPressed(juce::KeyPress{juce::KeyPress::insertKey}, &view));
    CHECK(controller.insert_at_caret_call_count == 1);
    CHECK(controller.ring_point_insert_call_count == 0);

    CHECK(mappings->keyPressed(juce::KeyPress{juce::KeyPress::insertKey, alt, 0}, &view));
    CHECK(controller.ring_point_insert_call_count == 1);
    CHECK(controller.insert_at_caret_call_count == 1);
}

// Up and Down walk the focus rows one row at a time, and Ctrl+Up/Down reach the adjacent group:
// both ride the one caret-step intent, told apart only by its reach flag.
TEST_CASE("EditorView routes Ctrl+Up and Ctrl+Down to the reaching row step", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    core::EditorViewState state = makeLoadedEditorState(20.0);
    auto tab = std::make_shared<common::core::ChartViewState>();
    tab->open_strings = common::core::testing::standardTuning();
    state.tab = std::move(tab);
    view.setState(state);

    juce::KeyListener* const mappings = view.commandManager().getKeyMappings();
    const int ctrl = juce::ModifierKeys::commandModifier;
    CHECK(mappings->keyPressed(juce::KeyPress{juce::KeyPress::downKey}, &view));
    CHECK(controller.last_chart_caret_step_direction == core::ChartStepDirection::Down);
    CHECK_FALSE(controller.last_chart_caret_step_reach);

    CHECK(mappings->keyPressed(
        juce::KeyPress{juce::KeyPress::upKey, juce::ModifierKeys{ctrl}, 0}, &view));
    CHECK(controller.last_chart_caret_step_direction == core::ChartStepDirection::Up);
    CHECK(controller.last_chart_caret_step_reach);

    CHECK(mappings->keyPressed(
        juce::KeyPress{juce::KeyPress::downKey, juce::ModifierKeys{ctrl}, 0}, &view));
    CHECK(controller.last_chart_caret_step_direction == core::ChartStepDirection::Down);
    CHECK(controller.last_chart_caret_step_reach);
    CHECK(controller.chart_caret_step_count == 3);
}

// Tab and Shift+Tab step to the adjacent object on the focused row, and the physical Ctrl key turns
// either into the notes-only step — the physical key rather than commandModifier, because Cmd+Tab
// is the macOS application switcher.
TEST_CASE("EditorView routes Tab to the row object step", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    core::EditorViewState state = makeLoadedEditorState(20.0);
    auto tab = std::make_shared<common::core::ChartViewState>();
    tab->open_strings = common::core::testing::standardTuning();
    state.tab = std::move(tab);
    view.setState(state);

    juce::KeyListener* const mappings = view.commandManager().getKeyMappings();
    const int ctrl = juce::ModifierKeys::ctrlModifier;
    const int shift = juce::ModifierKeys::shiftModifier;
    const auto press_tab = [mappings, &view](int modifiers) {
        return mappings->keyPressed(
            juce::KeyPress{juce::KeyPress::tabKey, juce::ModifierKeys{modifiers}, 0}, &view);
    };

    CHECK(press_tab(0));
    CHECK(controller.last_row_object_step_later);
    CHECK_FALSE(controller.last_row_object_step_notes_only);

    CHECK(press_tab(shift));
    CHECK_FALSE(controller.last_row_object_step_later);
    CHECK_FALSE(controller.last_row_object_step_notes_only);

    CHECK(press_tab(ctrl));
    CHECK(controller.last_row_object_step_later);
    CHECK(controller.last_row_object_step_notes_only);

    CHECK(press_tab(ctrl | shift));
    CHECK_FALSE(controller.last_row_object_step_later);
    CHECK(controller.last_row_object_step_notes_only);
    CHECK(controller.row_object_step_count == 4);
    CHECK(controller.chart_caret_step_count == 0);
}

// Each marker kind's Ctrl+Shift chord jumps focus onto that kind's row: the select half of the pair
// whose author half is Ctrl+letter, carried by one intent that names the row.
TEST_CASE("EditorView routes the Ctrl+Shift row jumps to the focus-row jump", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    core::EditorViewState state = makeLoadedEditorState(20.0);
    auto tab = std::make_shared<common::core::ChartViewState>();
    tab->open_strings = common::core::testing::standardTuning();
    state.tab = std::move(tab);
    view.setState(state);

    juce::KeyListener* const mappings = view.commandManager().getKeyMappings();
    constexpr int command_shift =
        juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;
    const auto press_jump = [mappings, &view](int key_code) {
        return mappings->keyPressed(
            juce::KeyPress{key_code, juce::ModifierKeys{command_shift}, 0}, &view);
    };

    CHECK(press_jump('m'));
    CHECK(controller.last_focus_row_jump == core::FocusRowJump::Section);

    CHECK(press_jump('b'));
    CHECK(controller.last_focus_row_jump == core::FocusRowJump::Tempo);

    CHECK(press_jump('/'));
    CHECK(controller.last_focus_row_jump == core::FocusRowJump::TimeSignature);

    CHECK(press_jump('t'));
    CHECK(controller.last_focus_row_jump == core::FocusRowJump::Tone);

    CHECK(press_jump('a'));
    CHECK(controller.last_focus_row_jump == core::FocusRowJump::AddAutomationLane);

    CHECK(controller.focus_row_jump_count == 5);
    CHECK(controller.chart_caret_step_count == 0);
    CHECK(controller.row_object_step_count == 0);

    // With no chart the chord is still consumed, so JUCE never plays the alert sound a disabled
    // command's matched chord produces: the core is the gate and answers the intent with nothing.
    core::EditorViewState no_chart = makeLoadedEditorState(20.0);
    no_chart.tab = nullptr;
    view.setState(no_chart);
    CHECK(press_jump('m'));
    CHECK(controller.focus_row_jump_count == 6);
}

// `Ctrl+R` is the selection's rename, and plain `R` still reaches the tremolo toggle: one letter,
// two verbs, told apart by the exact modifier matching the mapping set installs. The chord
// self-gates on the core's published verb, so with no rename target it reaches the controller with
// nothing at all — the prompt the verb opens is modal, so what it names is pinned in the core's own
// suite instead.
TEST_CASE("EditorView routes Ctrl+R to the selection rename", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    core::EditorViewState state = makeLoadedEditorState(20.0);
    auto tab = std::make_shared<common::core::ChartViewState>();
    tab->open_strings = common::core::testing::standardTuning();
    state.tab = std::move(tab);
    view.setState(state);

    juce::KeyPressMappingSet* const mappings = view.commandManager().getKeyMappings();
    REQUIRE(mappings != nullptr);
    const juce::KeyPress ctrl_r{'r', juce::ModifierKeys{juce::ModifierKeys::commandModifier}, 0};
    CHECK(
        mappings->findCommandForKeyPress(ctrl_r) ==
        toJuceCommandId(EditorCommandId::RenameSelection));

    CHECK(mappings->keyPressed(juce::KeyPress{'r', juce::ModifierKeys{}, 0}, &view));
    CHECK(
        controller.chart_technique_toggles ==
        std::vector<core::ChartTechnique>{core::ChartTechnique::Tremolo});

    CHECK(mappings->keyPressed(ctrl_r, &view));
    CHECK(controller.last_renamed_tone_document_ref.empty());
    CHECK(controller.last_renamed_song_section_name.empty());
    CHECK(
        controller.chart_technique_toggles ==
        std::vector<core::ChartTechnique>{core::ChartTechnique::Tremolo});
}

// `Shift+L` reaches the junction toggle, and plain `L` still reaches the connection verb it
// extends: one letter, two verbs, told apart by the modifier the interaction model reserves for
// exactly that. Dispatch rides the mapping set like every other registered command.
TEST_CASE("EditorView routes Shift+L to the junction toggle", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    core::EditorViewState state = makeLoadedEditorState(20.0);
    auto tab = std::make_shared<common::core::ChartViewState>();
    tab->open_strings = common::core::testing::standardTuning();
    state.tab = std::move(tab);
    view.setState(state);

    juce::KeyListener* const mappings = view.commandManager().getKeyMappings();
    CHECK(mappings->keyPressed(
        juce::KeyPress{'l', juce::ModifierKeys{juce::ModifierKeys::shiftModifier}, 0}, &view));
    CHECK(controller.chart_junction_toggle_count == 1);
    CHECK(controller.chart_technique_toggles.empty());

    CHECK(mappings->keyPressed(juce::KeyPress{'l', juce::ModifierKeys{}, 0}, &view));
    CHECK(
        controller.chart_technique_toggles ==
        std::vector<core::ChartTechnique>{core::ChartTechnique::Legato});
    CHECK(controller.chart_junction_toggle_count == 1);
}

// The attack letters reach their toggle rows, `Shift+T` the left-hand tap's: one letter carrying
// two rows, told apart by the exact modifier matching the mapping set installs.
TEST_CASE("EditorView routes the attack letters to their verbs", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    core::EditorViewState state = makeLoadedEditorState(20.0);
    auto tab = std::make_shared<common::core::ChartViewState>();
    tab->open_strings = common::core::testing::standardTuning();
    state.tab = std::move(tab);
    view.setState(state);

    juce::KeyListener* const mappings = view.commandManager().getKeyMappings();
    CHECK(mappings->keyPressed(juce::KeyPress{'t', juce::ModifierKeys{}, 0}, &view));
    CHECK(mappings->keyPressed(juce::KeyPress{'s', juce::ModifierKeys{}, 0}, &view));
    CHECK(mappings->keyPressed(juce::KeyPress{'p', juce::ModifierKeys{}, 0}, &view));
    CHECK(mappings->keyPressed(
        juce::KeyPress{'t', juce::ModifierKeys{juce::ModifierKeys::shiftModifier}, 0}, &view));
    CHECK(
        controller.chart_technique_toggles == std::vector<core::ChartTechnique>{
                                                  core::ChartTechnique::Tap,
                                                  core::ChartTechnique::Slap,
                                                  core::ChartTechnique::Pop,
                                                  core::ChartTechnique::LeftTap
                                              });
}

// The harmonic letter is its OWN verb, not a row of the technique toggle: its set states a value
// rather than a flag, so `H` sends the harmonic intent and nothing else. Whether that press asks
// the charter for a node or writes one at once is the controller's decision, made against the chart
// the press lands on — the view never forks on it, and never sends a technique here. The picker it
// may be handed back afterwards is a one-shot request, and the core's own suites pin when it is
// asked for and what it carries.
TEST_CASE("EditorView routes the harmonic letter to the harmonic verb", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    core::EditorViewState state = makeLoadedEditorState(20.0);
    auto tab = std::make_shared<common::core::ChartViewState>();
    tab->open_strings = common::core::testing::standardTuning();
    state.tab = std::move(tab);
    view.setState(state);

    juce::KeyListener* const mappings = view.commandManager().getKeyMappings();
    CHECK(mappings->keyPressed(juce::KeyPress{'h', juce::ModifierKeys{}, 0}, &view));
    CHECK(controller.chart_harmonic_requests == 1);
    CHECK(controller.chart_technique_toggles.empty());
}

// Selection verbs follow the selection, not the pointer: with a chart selection active,
// Alt+wheel (sustain) and Alt+Shift+wheel (fret shift) act on it over the timeline content
// (where zoom would otherwise consume the wheel) and anywhere else in the editor window.
TEST_CASE("EditorView routes selection wheels regardless of pointer position", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory), juce::File{}};

    core::EditorViewState state = makeLoadedEditorState(20.0);
    auto tab = std::make_shared<common::core::ChartViewState>();
    tab->open_strings = common::core::testing::standardTuning();
    tab->notes = {
        common::core::NoteViewState{
            .start_seconds = 1.0,
            .ring_end_seconds = 1.5,
            .ink_end_seconds = 1.0,
            .string = 1,
            .fret = 3,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
    };
    state.tab = std::move(tab);
    state.chart_edit.selected_notes = {0};
    view.setState(state);

    // Over the timeline content, Alt+wheel adjusts sustain instead of zooming.
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    const juce::MouseWheelDetails wheel_up{
        .deltaX = 0.0f, .deltaY = 1.0f, .isReversed = false, .isSmooth = false, .isInertial = false
    };
    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f, juce::ModifierKeys::altModifier), wheel_up);
    CHECK(controller.chart_sustain_adjust_count == 1);
    CHECK(controller.last_chart_sustain_direction == 1);

    // Alt+Shift+wheel over the content shifts frets.
    track_content.mouseWheelMove(
        makeMouseDownEvent(
            track_content,
            20.0f,
            20.0f,
            juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier),
        wheel_up);
    CHECK(controller.chart_fret_shift_count == 1);
    CHECK(controller.last_chart_fret_shift_direction == 1);

    // Anywhere else in the window, the wheel bubbles to the editor root and still routes.
    view.mouseWheelMove(
        makeMouseDownEvent(view, 30.0f, 30.0f, juce::ModifierKeys::altModifier), wheel_up);
    CHECK(controller.chart_sustain_adjust_count == 2);

    // Without a selection, an Alt wheel is not a selection verb (and never zooms).
    state.chart_edit.selected_notes = {};
    view.setState(state);
    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f, juce::ModifierKeys::altModifier), wheel_up);
    CHECK(controller.chart_sustain_adjust_count == 2);
    CHECK(controller.chart_fret_shift_count == 1);
}

// Verifies cursor geometry uses a pushed visible range plus a separately read position.
TEST_CASE("EditorView cursor geometry maps position through visible range", "[ui][editor-view]")
{
    const auto midpoint_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{5.0},
        common::core::TimeRange{
            .start = common::core::TimePosition{}, .end = common::core::TimePosition{10.0}
        },
        201);
    REQUIRE(midpoint_cursor.has_value());
    CHECK(optionalValueForApprox(midpoint_cursor) == Catch::Approx(100.0f));

    const auto offset_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{12.0},
        common::core::TimeRange{
            .start = common::core::TimePosition{10.0}, .end = common::core::TimePosition{14.0}
        },
        101);
    REQUIRE(offset_cursor.has_value());
    CHECK(optionalValueForApprox(offset_cursor) == Catch::Approx(50.0f));

    const auto fractional_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{1.25},
        common::core::TimeRange{
            .start = common::core::TimePosition{}, .end = common::core::TimePosition{4.0}
        },
        101);
    REQUIRE(fractional_cursor.has_value());
    CHECK(optionalValueForApprox(fractional_cursor) == Catch::Approx(31.25f));

    const auto before_start_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{-1.0},
        common::core::TimeRange{
            .start = common::core::TimePosition{}, .end = common::core::TimePosition{4.0}
        },
        101);
    REQUIRE(before_start_cursor.has_value());
    CHECK(optionalValueForApprox(before_start_cursor) == Catch::Approx(0.0f));

    const auto after_end_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{9.0},
        common::core::TimeRange{
            .start = common::core::TimePosition{}, .end = common::core::TimePosition{4.0}
        },
        101);
    REQUIRE(after_end_cursor.has_value());
    CHECK(optionalValueForApprox(after_end_cursor) == Catch::Approx(100.0f));

    CHECK_FALSE(cursorXForTimelinePosition(
                    common::core::TimePosition{1.0},
                    common::core::TimeRange{
                        .start = common::core::TimePosition{}, .end = common::core::TimePosition{}
                    },
                    101)
                    .has_value());
}

} // namespace rock_hero::editor::ui
