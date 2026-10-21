#include "main_window/editor_view.h"
#include "shared/editor_theme.h"
#include "tab/tab_view.h"
#include "timeline/timeline_ruler.h"
#include "timeline/track_viewport.h"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <ranges>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/testing/tuning_fixtures.h>
#include <rock_hero/common/ui/testing/highway_theme_fixtures.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>
#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

// A four-second song with two spans and a none hold: a chord span over [1s, 2s), an arpeggio span
// over [3s, 3.5s), and a none hold over [2.25s, 2.75s) between them. `held` decides whether the
// chord span is held (drawn solid) or derived (drawn as a ghost).
[[nodiscard]] std::shared_ptr<const common::core::ChartViewState> makeSpanRowChart(const bool held)
{
    common::core::ChartViewState chart;
    chart.open_strings = common::core::testing::standardTuning();
    chart.shapes = {
        common::core::ShapeViewState{
            .start_seconds = 1.0,
            .drawn_end_seconds = 2.0,
            .close_seconds = 2.0,
            .arpeggio = false,
            .strings = {},
            .held = held,
        },
        common::core::ShapeViewState{
            .start_seconds = 3.0,
            .drawn_end_seconds = 3.5,
            .close_seconds = 3.5,
            .arpeggio = true,
            .strings = {},
            .held = true,
        },
    };
    chart.shape_close_prefix_max = common::core::makeSustainPrefixMax(
        chart.shapes | std::views::transform(&common::core::ShapeViewState::close_seconds));
    chart.none_holds = {
        common::core::NoneHoldViewState{.start_seconds = 2.25, .end_seconds = 2.75},
    };
    return std::make_shared<const common::core::ChartViewState>(std::move(chart));
}

using testing::nearestColor;
using testing::squaredColorDistance;

// Whether a pixel of `flashed` moved toward the theme's invalid ink from the same pixel of
// `plain`: the refusal glow tints it at any level above zero, so a slow runner cannot flake it.
[[nodiscard]] bool glowsAt(const juce::Image& plain, const juce::Image& flashed, int x, int y)
{
    const juce::Colour invalid = editorTheme().invalid;
    return squaredColorDistance(flashed.getPixelAt(x, y), invalid) <
           squaredColorDistance(plain.getPixelAt(x, y), invalid);
}

// One editor shown over the span-row chart, its views found by id: the shell, its ruler and its
// lane, with the selection and the chord span's kind the case asks for.
struct SpanRowEditor
{
    juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{
        controller,
        viewAudioPorts(transport, thumbnail_factory),
        juce::File{},
        fixtureHighwayThemeRegistry()
    };
    core::EditorViewState state{makeLoadedEditorState(4.0)};

    // Shows the span-row chart, its chord span held or derived as asked, with marker edits open.
    explicit SpanRowEditor(const bool held)
    {
        view.setBounds(0, 0, 1280, 800);
        state.tab = makeSpanRowChart(held);
        state.marker_edits_enabled = true;
        view.setState(state);
    }

    // Republishes the state with this chart selection.
    void select(const core::ChartEditViewState& edit)
    {
        state.chart_edit = edit;
        view.setState(state);
    }

    [[nodiscard]] TrackViewport& shell()
    {
        return findRequiredDescendant<TrackViewport>(view, "track_viewport");
    }

    [[nodiscard]] TimelineRuler& ruler()
    {
        return findRequiredDescendant<TimelineRuler>(view, "timeline_ruler");
    }

    [[nodiscard]] TabView& lane()
    {
        return findRequiredDescendant<TabView>(view, "tab_view");
    }

    [[nodiscard]] juce::Viewport& scroll()
    {
        return findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    }

    [[nodiscard]] juce::Image render()
    {
        return shell().createComponentSnapshot(shell().getLocalBounds());
    }

    // A box of the lane in the shell's coordinates, rounded to the pixels it covers.
    [[nodiscard]] juce::Rectangle<int> inShell(const juce::Rectangle<float> lane_box)
    {
        return shell().getLocalArea(&lane(), lane_box).toNearestIntEdges();
    }
};

} // namespace

// The selected span's chip and top rail read as ONE shape: one ring runs around the chip's top and
// sides and on along the rail's top past the chip, never between the chip and the rail, and a
// second ring closes around the bottom rail — each wholly outside its boxes, so the rails keep
// their ink. Neither the ruler nor the lane draws it, so it is whole across their seam.
TEST_CASE(
    "TrackViewport rings a selected span's chip and rails as one shape", "[ui][track-viewport]")
{
    SpanRowEditor editor{true};
    editor.select(core::ChartEditViewState{.selected_span = 0});
    const std::optional<juce::Rectangle<int>> chip = editor.ruler().selectedChipBounds();
    const std::optional<DrawnSpanRow> row = editor.lane().selectedSpanRow();
    REQUIRE(chip.has_value());
    REQUIRE(row.has_value());
    if (!chip.has_value() || !row.has_value())
    {
        return;
    }
    const juce::Rectangle<int> top = editor.inShell(row->top_rail);
    const juce::Rectangle<int> bottom = editor.inShell(row->bottom_rail);
    const juce::Image image = editor.render();
    const juce::Colour accent = editorTheme().accent;
    const juce::Colour chord_rail = image.getPixelAt(top.getCentreX(), top.getY() + 1);
    const juce::Colour chord_chip = image.getPixelAt(chip->getX() + 2, chip->getCentreY());
    const auto classify = [&](const juce::Colour pixel) {
        return nearestColor(pixel, {chord_rail, chord_chip, accent});
    };

    // The fixture's geometry: the chip stands on the ruler's base, right on the rail's top, from
    // the rail's left edge, and narrower than the rail.
    CHECK(chip->getBottom() == top.getY());
    CHECK(chip->getX() == top.getX());
    REQUIRE(chip->getRight() + 4 < top.getRight());

    // Around the chip: its top and its left side.
    CHECK(image.getPixelAt(chip->getCentreX(), chip->getY() - 1) == accent);
    CHECK(image.getPixelAt(chip->getX() - 1, chip->getCentreY()) == accent);
    // The ruler's base row beside the chip carries the ring along the rail's top; under the chip
    // it is still the chip, fused with the rail.
    CHECK(image.getPixelAt(chip->getRight() + 3, top.getY() - 1) == accent);
    CHECK(classify(image.getPixelAt(chip->getX() + 2, top.getY() - 1)) == chord_chip);
    // The rails keep their ink inside the ring; the ring lies under the top rail and around the
    // bottom rail's outer side.
    for (const int rail_row : {top.getY(), top.getY() + 1, top.getBottom() - 1})
    {
        CHECK(classify(image.getPixelAt(top.getCentreX(), rail_row)) == chord_rail);
    }
    CHECK(image.getPixelAt(top.getCentreX(), top.getBottom()) == accent);
    CHECK(image.getPixelAt(bottom.getCentreX(), bottom.getY() - 1) == accent);
    CHECK(image.getPixelAt(bottom.getCentreX(), bottom.getBottom()) == accent);
    CHECK(classify(image.getPixelAt(bottom.getCentreX(), bottom.getY() + 1)) == chord_rail);

    // The ruler's own render carries no ring: the outline is the shell's alone.
    const juce::Image ruler =
        editor.ruler().createComponentSnapshot(editor.ruler().getLocalBounds());
    CHECK(ruler.getPixelAt(chip->getCentreX(), chip->getY() - 1) != accent);
}

// A none hold stands no chip, so each of its rails wears its own closed ring, the top one whole
// over the ruler's base row rather than clipped at the lane's edge.
TEST_CASE("TrackViewport rings a selected none hold's two rails", "[ui][track-viewport]")
{
    SpanRowEditor editor{true};
    editor.select(core::ChartEditViewState{.selected_none_hold = 0});
    const std::optional<DrawnSpanRow> row = editor.lane().selectedSpanRow();
    REQUIRE(row.has_value());
    if (!row.has_value())
    {
        return;
    }
    CHECK_FALSE(row->span.has_value());
    const juce::Image image = editor.render();
    const juce::Colour accent = editorTheme().accent;
    for (const juce::Rectangle<float>& lane_rail : {row->top_rail, row->bottom_rail})
    {
        const juce::Rectangle<int> rail = editor.inShell(lane_rail);
        CHECK(image.getPixelAt(rail.getCentreX(), rail.getY() - 1) == accent);
        CHECK(image.getPixelAt(rail.getCentreX(), rail.getBottom()) == accent);
        CHECK(image.getPixelAt(rail.getX() - 1, rail.getCentreY()) == accent);
        CHECK(image.getPixelAt(rail.getRight(), rail.getCentreY()) == accent);
    }
}

// A refused span glows as one shape too: around its chip on the ruler and around its rails in the
// lane, and its neighbour does not.
TEST_CASE("TrackViewport glows a refused span's chip and rails", "[ui][track-viewport]")
{
    SpanRowEditor editor{true};
    const std::optional<DrawnSpanRow> row = editor.lane().drawnSpanRow(0);
    const std::optional<DrawnSpanRow> neighbour = editor.lane().drawnSpanRow(1);
    const std::optional<juce::Rectangle<float>> chip = editor.ruler().spanChipBox(0);
    REQUIRE(row.has_value());
    REQUIRE(neighbour.has_value());
    REQUIRE(chip.has_value());
    if (!row.has_value() || !neighbour.has_value() || !chip.has_value())
    {
        return;
    }
    const juce::Image plain = editor.render();
    const juce::Image plain_lane =
        editor.lane().createComponentSnapshot(editor.lane().getLocalBounds());
    editor.view.flashRefusal(
        core::RefusalFlash{
            .notes = {},
            .spans = {0},
            .none_holds = {},
            .fret_hand_positions = {},
            .keyframes = {},
            .ruler_chips = {},
            .tone_regions = {},
            .locks = {},
            .sentence = {},
        });
    const juce::Image flashed = editor.render();

    const juce::Rectangle<int> chip_box = chip->getSmallestIntegerContainer();
    const juce::Rectangle<int> top = editor.inShell(row->top_rail);
    const juce::Rectangle<int> bottom = editor.inShell(row->bottom_rail);
    const juce::Rectangle<int> other = editor.inShell(neighbour->top_rail);
    CHECK(glowsAt(plain, flashed, chip_box.getCentreX(), chip_box.getY() - 2));
    CHECK(glowsAt(plain, flashed, top.getCentreX(), top.getBottom() + 1));
    CHECK(glowsAt(plain, flashed, bottom.getCentreX(), bottom.getY() - 2));
    CHECK_FALSE(glowsAt(plain, flashed, other.getCentreX(), other.getBottom() + 1));
    // The lane lights none of it: the span row's glow is the shell's alone.
    const juce::Image flashed_lane =
        editor.lane().createComponentSnapshot(editor.lane().getLocalBounds());
    const juce::Rectangle<int> lane_top = row->top_rail.getSmallestIntegerContainer();
    CHECK(
        flashed_lane.getPixelAt(lane_top.getCentreX(), lane_top.getBottom() + 1) ==
        plain_lane.getPixelAt(lane_top.getCentreX(), lane_top.getBottom() + 1));
}

// The outline encloses what of the mark the view shows: a lane scrolled up under the pinned ruler
// leaves a thinner top rail under the chip, still ringed as one shape with it, and a lane scrolled
// past the rail leaves the chip alone, ringed closed across its base.
TEST_CASE("TrackViewport rings what of a scrolled span shows", "[ui][track-viewport]")
{
    SpanRowEditor editor{true};
    editor.select(core::ChartEditViewState{.selected_span = 0});
    const std::optional<juce::Rectangle<int>> chip = editor.ruler().selectedChipBounds();
    const std::optional<DrawnSpanRow> row = editor.lane().selectedSpanRow();
    REQUIRE(chip.has_value());
    REQUIRE(row.has_value());
    if (!chip.has_value() || !row.has_value())
    {
        return;
    }
    const juce::Rectangle<int> top = editor.inShell(row->top_rail);
    const juce::Colour accent = editorTheme().accent;
    const int beside_chip = chip->getRight() + 3;
    juce::Viewport& scroll = editor.scroll();
    REQUIRE(scroll.getViewHeight() < scroll.getViewedComponent()->getHeight());

    // Scrolled two rows up: one rail row shows at the view's top, ringed above (the ruler's base
    // row) and below.
    scroll.setViewPosition(0, 2);
    const juce::Image thin = editor.render();
    CHECK(thin.getPixelAt(beside_chip, top.getY() - 1) == accent);
    CHECK(thin.getPixelAt(beside_chip, top.getY() + 1) == accent);
    CHECK(thin.getPixelAt(beside_chip, top.getY()) != accent);

    // Scrolled past the rail: no rail shows, so the ruler's base row beside the chip is bare and
    // the chip's own ring closes across its base, on the view's top row.
    scroll.setViewPosition(0, 10);
    const juce::Image chip_alone = editor.render();
    CHECK(chip_alone.getPixelAt(beside_chip, top.getY() - 1) != accent);
    CHECK(chip_alone.getPixelAt(chip->getCentreX(), chip->getBottom()) == accent);
    CHECK(chip_alone.getPixelAt(chip->getCentreX(), chip->getY() - 1) == accent);
}

// A derived span's chip ghosts exactly as its rails do: the chip is cleared to the rails' own
// ground before its ghost composites, so its fill reads as the ghosted rail's ink and no ruler
// tick shows through it.
TEST_CASE(
    "TrackViewport shows a derived span's chip in its ghosted rail's ink", "[ui][track-viewport]")
{
    SpanRowEditor editor{false};
    const std::optional<DrawnSpanRow> row = editor.lane().drawnSpanRow(0);
    const std::optional<juce::Rectangle<float>> chip = editor.ruler().spanChipBox(0);
    REQUIRE(row.has_value());
    REQUIRE(chip.has_value());
    if (!row.has_value() || !chip.has_value())
    {
        return;
    }
    const juce::Image image = editor.render();
    const juce::Rectangle<int> top = editor.inShell(row->top_rail);
    const juce::Rectangle<int> chip_box = chip->getSmallestIntegerContainer();
    const juce::Colour ghost_rail = image.getPixelAt(top.getCentreX(), top.getY() + 1);
    // The chip's base row runs over the beat tick at its own start (1 s is a beat), which a chip
    // ghosted over the ruler's own ground would let show through; cleared to the rails' ground, the
    // whole row is the ghosted rail's ink and nothing else.
    const juce::Colour ruler_ground = editorTheme().timeline_ruler_background;
    const juce::Colour tick = editorTheme().grid_measure;
    for (int x = chip_box.getX() + 1; x < chip_box.getRight() - 1; ++x)
    {
        const juce::Colour pixel = image.getPixelAt(x, chip_box.getBottom() - 1);
        CHECK(nearestColor(pixel, {ghost_rail, ruler_ground, tick}) == ghost_rail);
        CHECK(squaredColorDistance(pixel, ghost_rail) <= 3);
    }
}

} // namespace rock_hero::editor::ui
