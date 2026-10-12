// Headless smoke coverage for HighwayRenderer itself, driven against bgfx's Noop backend.
//
// WHAT THIS SUITE PROVES
//
// The renderer comes up against a REAL bgfx context: it links the shipped Direct3D 11 shader
// binaries (bgfx parses and validates a shader blob's magic, version, and uniform records at the
// API layer whatever backend is active, so a corrupt or stale binary still fails here), uploads
// and MEASURES the shipped textures, then accepts real projected view states and encodes several
// hundred frames across them. It covers the lifecycle and the encode path end to end: content
// before/inside/after the drawn span, a resize mid-sweep, a sweep with every reading aid off, a
// content swap mid-sweep, a paused redraw at dt = 0, an empty view state, the overlay pass, a
// large-batch capacity probe, and the teardown order the renderer's contract requires (renderer
// dies before bgfx shutdown).
//
// The signal is bgfx's own instrumentation, not ours. The debug preset compiles bgfx with
// BX_CONFIG_DEBUG=1, so its internal BX_ASSERTs are live: an invalid handle, a view id past the
// configured maximum, a vertex layout that disagrees with the transient buffer it was allocated
// against, a uniform set with the wrong type, or a submit with an unlinked program aborts the
// process. Reaching the end of this case means none of that happened over the frames it drew.
//
// WHAT THIS SUITE CANNOT PROVE
//
// Nothing about the picture. The Noop backend's submit() zeroes Stats::numPrims and never writes
// Stats::numDraw (renderer_noop.cpp), so draw counts, batch counts, submission order, state
// changes, and every pixel are invisible from here — there is no output to read at all. A green
// run says "no crash and no assert", never "the frame is correct", and never "the batch was
// submitted": the renderer DROPS a batch it cannot submit (see submitBatch's 65535-vertex guard),
// and a drop is indistinguishable from a draw at this level. Visual correctness stays with the
// geometry and decision unit suites beside this one plus review by eye.
//
// WHY ONE TEST CASE
//
// bgfx is a process singleton that cannot be re-initialized after shutdown, so this binary owns
// exactly one device (noop_render_device.h) and every scenario below runs against it in sequence
// rather than in sibling cases that would each want their own. Catch2 SECTIONs would re-enter the
// case body per leaf and re-create the renderer each time, which buys nothing here.
//
// WHY IT SKIPS OFF WINDOWS
//
// The compiled shaders come from shaderc's HLSL backend, which is Windows-only
// (rock_hero_stage_highway_shaders), so the staged tree is empty elsewhere and this case reports
// a skip instead of claiming coverage it did not run.

#include "noop_render_device.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <compare>
#include <cstddef>
#include <cstring>
#include <expected>
#include <juce_core/juce_core.h>
#include <memory>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/highway/highway_camera.h>
#include <rock_hero/common/core/highway/highway_projection.h>
#include <rock_hero/common/core/highway/highway_resources.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/ui/highway/highway_renderer.h>
#include <rock_hero/common/ui/highway_theme/highway_render_look.h>
#include <rock_hero/common/ui/highway_theme/highway_theme.h>
#include <rock_hero/common/ui/render/render_device.h>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>
#include <rock_hero/common/ui/testing/highway_theme_fixtures.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::common::ui
{

namespace
{

using common::core::ChartNote;
using common::core::Fraction;
using common::core::GridPosition;
using common::core::NoteAttack;
using common::core::NoteEmphasis;

// Reads a whole resource file; empty when it is missing or unreadable, which is what the shader
// loader below reads as "this platform staged nothing".
[[nodiscard]] std::vector<std::byte> readAllBytes(const juce::File& file)
{
    juce::MemoryBlock block;
    if (!file.existsAsFile() || !file.loadFileAsData(block) || block.getSize() == 0)
    {
        return {};
    }
    std::vector<std::byte> bytes(block.getSize());
    std::memcpy(bytes.data(), block.getData(), block.getSize());
    return bytes;
}

// Loads the compiled shader pairs staged beside this suite, walking the shared program table
// exactly as both products' loaders do. A stage that never got staged stays empty rather than
// failing here, because "nothing was staged" is the ordinary non-Windows state, not an error.
[[nodiscard]] HighwayShaderSet loadStagedShaderSet()
{
    const juce::File staged{ROCK_HERO_SHADERS_DIR};
    HighwayShaderSet set;
    for (const common::core::HighwayShaderProgram program : common::core::g_highway_shader_programs)
    {
        const std::string name{common::core::highwayShaderProgramName(program)};
        set.at(common::core::indexOf(program)) = HighwayShaderPair{
            .vertex = readAllBytes(staged.getChildFile("vs_" + name + ".bin")),
            .fragment = readAllBytes(staged.getChildFile("fs_" + name + ".bin")),
        };
    }
    return set;
}

// True when every program's two stages loaded. The renderer needs the whole set, and on Windows
// staging is all-or-nothing — a shaderc failure fails the build — so an incomplete set means
// shaderc never ran at all.
[[nodiscard]] bool isCompleteShaderSet(const HighwayShaderSet& set)
{
    return std::ranges::none_of(set, [](const HighwayShaderPair& pair) {
        return pair.vertex.empty() || pair.fragment.empty();
    });
}

// Loads the default theme's art from the source tree, the same bytes the products deploy.
[[nodiscard]] HighwayTextureSet loadShippedTextureSet()
{
    const juce::File textures = common::core::juceFileFromPath(testing::defaultThemeDirectory());
    HighwayTextureSet set;
    for (const common::core::HighwayTexture texture : common::core::g_highway_textures)
    {
        const std::string file_name{common::core::highwayTextureFileName(texture)};
        set.at(common::core::indexOf(texture)) =
            readAllBytes(textures.getChildFile(juce::String{file_name}));
    }
    return set;
}

// Loads the fixed mark sheet from the source tree, the same bytes the products deploy.
[[nodiscard]] std::vector<std::byte> loadShippedMarkSheet()
{
    return readAllBytes(common::core::juceFileFromPath(testing::highwayMarkSheetPath()));
}

// A 4/4 120 BPM map covering eight measures, matching the projection suite's fixture map.
[[nodiscard]] common::core::TempoMap makeSmokeTempoMap()
{
    return common::core::TempoMap::defaultMap(common::core::TimeDuration{16.0});
}

// Two song-level markers: the second forces a camera framing-zone boundary mid-sweep, so the
// camera steps at least once while the board is being drawn.
[[nodiscard]] std::vector<common::core::SongSection> makeSmokeSections()
{
    return {
        common::core::SongSection{
            .position = GridPosition{.measure = 1, .beat = 1}, .name = "intro"
        },
        common::core::SongSection{
            .position = GridPosition{.measure = 5, .beat = 1}, .name = "chorus"
        },
    };
}

// One note per draw family the board owns, ordered by position: a scrape with its unpitched
// terminal, a strummed chord (which derives a shape span and a chord box), a palm mute, a
// tremolo tail, a vibrato, an accented head, a ghosted tail carrying both a bend point and a
// pitched glide, a dead palm-muted stop, an open string on a harmonic node over the capo, a
// legato pair resolving to a hammer-on and a pull-off, a tapped pair (the tapping-hand light and
// its box), a long accented open tail, and a strum restruck in part under a ringing string (an
// arpeggio span: its posture mark and its rails). Two fret-hand placements a wide jump apart give
// the hand window both a settled span and a transition sweep.
[[nodiscard]] common::core::Arrangement makeFamiliesArrangement()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.tuning.capo = 2;
    chart.notes = {
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1},
            .string = 6,
            .fret = 17,
            .sustain = Fraction{1, 4},
            .attack = NoteAttack::PickSlide,
            .bend = {},
            // The turnaround, then the required terminal: a scrape's slide-out is the keyframe
            // stating a fret exactly at the ring's end.
            .keyframes =
                {
                    common::core::Keyframe{.offset = Fraction{1, 8}, .fret = 5},
                    common::core::Keyframe{
                        .offset = Fraction{1, 4}, .fret = 12, .bend = {}, .vibrato = {}
                    },
                },
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 1,
            .fret = 4,
            .sustain = Fraction{1, 4},
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 2,
            .fret = 6,
            .sustain = Fraction{1, 32},
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 3,
            .fret = 6,
            .sustain = Fraction{1, 32},
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 2},
            .string = 4,
            .fret = 7,
            .sustain = Fraction{1, 8},
            .palm_mute = true,
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 3},
            .string = 5,
            .fret = 9,
            .sustain = Fraction{1, 8},
            .tremolo = true,
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 4},
            .string = 5,
            .fret = 9,
            .sustain = Fraction{1, 4},
            .vibrato = common::core::VibratoState::Narrow,
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 1},
            .string = 4,
            .fret = 12,
            .sustain = Fraction{1, 32},
            .emphasis = NoteEmphasis::Accent,
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 2},
            .string = 3,
            .fret = 7,
            .sustain = Fraction{1, 2},
            .emphasis = NoteEmphasis::Ghost,
            .keyframes =
                {
                    common::core::Keyframe{.offset = Fraction{1, 4}, .bend = 2.0},
                    common::core::Keyframe{.offset = Fraction{1, 2}, .fret = 9},
                },
        },
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 3},
            .string = 6,
            .fret = 5,
            .sustain = Fraction{1, 32},
            .palm_mute = true,
            .dead = true,
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 4},
            .string = 5,
            .fret = 0,
            .sustain = Fraction{1, 8},
            .harmonic_node = 7.02,
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 1},
            .string = 4,
            .fret = 5,
            .sustain = Fraction{1, 4},
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 2},
            .string = 4,
            .fret = 7,
            .sustain = Fraction{1, 4},
            .attack = NoteAttack::Legato,
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 3},
            .string = 4,
            .fret = 5,
            .sustain = Fraction{1, 4},
            .attack = NoteAttack::Legato,
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 5, .beat = 1},
            .string = 2,
            .fret = 12,
            .sustain = Fraction{1, 4},
            .attack = NoteAttack::Tap,
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 5, .beat = 1},
            .string = 3,
            .fret = 14,
            .sustain = Fraction{1, 4},
            .attack = NoteAttack::Tap,
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 6, .beat = 1},
            .string = 1,
            .fret = 0,
            .sustain = Fraction{1},
            .emphasis = NoteEmphasis::Accent,
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 7, .beat = 1},
            .string = 1,
            .fret = 12,
            .sustain = Fraction{1, 4},
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 7, .beat = 1},
            .string = 2,
            .fret = 13,
            .sustain = Fraction{1, 4},
            .bend = {},
            .keyframes = {},
        },
        // Rings through the restrike below, which therefore sounds the shape only in part.
        ChartNote{
            .position = GridPosition{.measure = 7, .beat = 1},
            .string = 3,
            .fret = 14,
            .sustain = Fraction{1, 2},
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 7, .beat = 2},
            .string = 1,
            .fret = 12,
            .sustain = Fraction{1, 4},
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 7, .beat = 2},
            .string = 2,
            .fret = 13,
            .sustain = Fraction{1, 4},
            .bend = {},
            .keyframes = {},
        },
    };
    chart.fret_hand_positions = {
        common::core::FretHandPosition{
            .position = GridPosition{.measure = 2, .beat = 1}, .fret = 4
        },
        common::core::FretHandPosition{
            .position = GridPosition{.measure = 5, .beat = 1}, .fret = 12
        },
    };

    common::core::Arrangement arrangement;
    arrangement.chart = std::move(chart);
    return arrangement;
}

// The swap content: a different chart on a different tuning, so replacing the state rebuilds the
// retained board face rather than redrawing the same lanes. Drawn under mirrored, inverted, and
// lane-padded display options so the per-frame display mapping runs its non-default branches.
[[nodiscard]] common::core::Arrangement makeSwapArrangement()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3"};
    chart.notes = {
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 1,
            .fret = 3,
            .sustain = Fraction{1, 2},
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 1},
            .string = 4,
            .fret = 0,
            .sustain = Fraction{1, 4},
            .bend = {},
            .keyframes = {},
        },
    };
    chart.fret_hand_positions = {
        common::core::FretHandPosition{
            .position = GridPosition{.measure = 2, .beat = 1}, .fret = 1
        },
    };

    common::core::Arrangement arrangement;
    arrangement.chart = std::move(chart);
    return arrangement;
}

// The capacity probe's content: four measures of accented, tremolo'd open tails struck on all six
// strings twice a quarter. This is the shape that can overflow a 16-bit index base — one onset
// group of long teethed open tails whose accent glow passes 65535 vertices — so it pushes the
// largest batches the encoder builds. What it asserts is survival: past the cap the renderer DROPS
// the batch and logs once, which is the guard doing its job and is invisible from here.
[[nodiscard]] common::core::Arrangement makeDenseAccentArrangement()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes.reserve(192);
    for (int measure = 1; measure <= 4; ++measure)
    {
        for (int beat = 1; beat <= 4; ++beat)
        {
            for (const Fraction offset : {Fraction{0}, Fraction{1, 2}})
            {
                for (int string = 1; string <= 6; ++string)
                {
                    chart.notes.push_back(
                        ChartNote{
                            .position =
                                GridPosition{.measure = measure, .beat = beat, .offset = offset},
                            .string = string,
                            .fret = 0,
                            .sustain = Fraction{1, 8},
                            .tremolo = true,
                            .emphasis = NoteEmphasis::Accent,
                            .bend = {},
                            .keyframes = {},
                        });
                }
            }
        }
    }

    common::core::Arrangement arrangement;
    arrangement.chart = std::move(chart);
    return arrangement;
}

// Draws `frames` consecutive frames, advancing song time by `step_seconds` each and passing the
// same value as the frame delta, then submitting each through the device. A zero step is the
// paused redraw both products perform while the transport sits still. The diagnostic overlay
// rides every frame because that is how the products composite it: draw, then drawOverlayRects,
// then submit.
void drawFrames(
    HighwayRenderer& renderer, RenderDevice& device, const double first_now_seconds,
    const double step_seconds, const int frames)
{
    static constexpr std::array<HighwayOverlayRect, 2> g_overlay{
        HighwayOverlayRect{
            .left = 8.0F, .top = 8.0F, .right = 208.0F, .bottom = 72.0F, .abgr = 0x80202020
        },
        HighwayOverlayRect{
            .left = 8.0F, .top = 80.0F, .right = 120.0F, .bottom = 96.0F, .abgr = 0xff40c0ff
        },
    };
    for (int frame = 0; frame < frames; ++frame)
    {
        const double now_seconds = first_now_seconds + (step_seconds * static_cast<double>(frame));
        renderer.draw(now_seconds, step_seconds, device.width(), device.height());
        renderer.drawOverlayRects(g_overlay, device.width(), device.height());
        device.submitFrame();
    }
}

} // namespace

// Drives HighwayRenderer itself. Scope, and the limits of the Noop backend it runs on, are
// documented at the top of this file — read that before reading a green run as evidence about
// what the board looks like.
TEST_CASE("Highway renderer survives a headless Noop frame sweep", "[ui][highway][surface]")
{
    const HighwayShaderSet shaders = loadStagedShaderSet();
    if (!isCompleteShaderSet(shaders))
    {
        SKIP("compiled highway shaders are staged on Windows only (shaderc's HLSL backend)");
    }

    RenderDevice* const device = sharedNoopDevice();
    REQUIRE(device != nullptr);
    device->resize(960, 540);

    // The renderer lives in an inner scope so its GPU handles are destroyed while bgfx is still
    // up: its contract is create only against a live device, destroy before bgfx shutdown.
    {
        std::expected<HighwayRenderer, HighwayRendererError> renderer = HighwayRenderer::create(
            shaders,
            loadShippedTextureSet(),
            loadShippedMarkSheet(),
            testing::fixtureHighwayTheme(),
            testing::fixtureStringColors(),
            HighwayPlainLook{});
        REQUIRE(renderer.has_value());

        // Encoding frames before any content is the state both products are in between device
        // bring-up and the first chart load.
        drawFrames(*renderer, *device, 0.0, 1.0 / 60.0, 8);

        const common::core::TempoMap tempo_map = makeSmokeTempoMap();
        const common::core::Arrangement families_arrangement = makeFamiliesArrangement();
        const common::core::HighwayViewState families = makeHighwayViewState(
            makeChartViewState(families_arrangement, tempo_map), tempo_map, makeSmokeSections());

        // Fixture guards, not renderer assertions: without them a projection change could empty
        // a draw family and leave the sweep below silently exercising a blank board while still
        // reporting green.
        CHECK_FALSE(families.chart.notes.empty());
        CHECK_FALSE(families.chart.fret_hand_positions.empty());
        // The hand window, its light and the camera move by the fretting hand's track, not the
        // placements the board labels, so both must be present — and each hand's light has to be
        // lit somewhere, or the sweep would never draw the floor light at all.
        CHECK_FALSE(families.fret_hand.track.empty());
        CHECK_FALSE(families.fret_hand.lit.empty());
        CHECK_FALSE(families.pick_hand.lit.empty());
        CHECK_FALSE(families.chord_groups.empty());
        CHECK_FALSE(families.tap_onsets.empty());
        // A tapped CHORD, not only a tap: the picking hand's box, its rails and its box-side pop
        // draw only for two or more taps struck together.
        CHECK(std::ranges::any_of(families.tap_onsets, common::core::tappedChord));
        // An arpeggio whose posture mark draws, or the mark and its reading-aid switch go unswept.
        CHECK(
            std::ranges::any_of(
                families.chart.shapes, [](const common::core::ShapeViewState& shape) {
                    return shape.arpeggio && shape.bracket_seconds.has_value();
                }));
        CHECK_FALSE(families.beats.empty());
        CHECK_FALSE(families.sections.empty());
        CHECK(
            std::ranges::any_of(families.chart.notes, [](const common::core::NoteViewState& note) {
                return note.fret == 0;
            }));
        CHECK(
            std::ranges::any_of(families.chart.notes, [](const common::core::NoteViewState& note) {
                return note.ink_end_seconds > note.start_seconds;
            }));
        CHECK(
            std::ranges::any_of(families.chart.notes, [](const common::core::NoteViewState& note) {
                return note.emphasis == NoteEmphasis::Accent;
            }));

        // Three hundred frames across the whole content in two halves, starting two seconds
        // before the first onset and ending past the last tail: every note crosses the drawn
        // window from the horizon to the strike line and dissolves there.
        const double last_beat_seconds = families.beats.back().seconds;
        const double sweep_step_seconds = (last_beat_seconds + 4.0) / 300.0;
        renderer->setViewState(std::make_shared<const common::core::HighwayViewState>(families));
        drawFrames(*renderer, *device, -2.0, sweep_step_seconds, 150);

        // The resize lands between the halves, with content on the board: the view rects and the
        // projection follow the new backbuffer from the next encoded frame, and the second half
        // carries the sweep the rest of the way past the last tail.
        device->resize(1280, 720);
        drawFrames(*renderer, *device, last_beat_seconds / 2.0, sweep_step_seconds, 150);

        // The paused redraw: song time frozen, frame delta zero, the camera smoother asked to
        // integrate nothing.
        drawFrames(*renderer, *device, last_beat_seconds / 2.0, 0.0, 30);

        // The render look is a theme part: a pixel look refits the scene target to whole blocks
        // and adds the block canvas, a resize under it refits both, the CRT drops the canvas for
        // the window-size scene, the dither and the halftone each present through their own
        // program, and the plain look brings the window-size target back.
        renderer->setTheme(
            testing::fixtureHighwayTheme(),
            testing::fixtureStringColors(),
            HighwayPixelLook{.scale = 3, .scanlines = 0.5});
        drawFrames(*renderer, *device, last_beat_seconds / 2.0, sweep_step_seconds, 20);
        device->resize(1001, 563);
        drawFrames(*renderer, *device, last_beat_seconds / 2.0, sweep_step_seconds, 20);
        renderer->setTheme(
            testing::fixtureHighwayTheme(),
            testing::fixtureStringColors(),
            HighwayCrtLook{.line_height = 3, .scanlines = 0.4});
        drawFrames(*renderer, *device, last_beat_seconds / 2.0, sweep_step_seconds, 20);
        renderer->setTheme(
            testing::fixtureHighwayTheme(),
            testing::fixtureStringColors(),
            HighwayDitherLook{.scale = 2});
        drawFrames(*renderer, *device, last_beat_seconds / 2.0, sweep_step_seconds, 20);
        renderer->setTheme(
            testing::fixtureHighwayTheme(),
            testing::fixtureStringColors(),
            HighwayHalftoneLook{.cell = 8});
        drawFrames(*renderer, *device, last_beat_seconds / 2.0, sweep_step_seconds, 20);
        renderer->setTheme(
            testing::fixtureHighwayTheme(), testing::fixtureStringColors(), HighwayPlainLook{});
        drawFrames(*renderer, *device, last_beat_seconds / 2.0, sweep_step_seconds, 20);
        device->resize(1280, 720);

        // The camera is the host's: a re-published state (an edit of the chart on screen) keeps it
        // gliding from where it is, and only resetCamera (a load) snaps it to its target. Framed
        // at rest on the first zone, then one frame of glide toward the last zone's target, which
        // the hand's wide jump puts elsewhere.
        const std::vector<double>& zone_starts = families.camera_zone_starts;
        REQUIRE(zone_starts.size() >= 2);
        const auto same_pose = [](const common::core::HighwayCameraPose& lhs,
                                  const common::core::HighwayCameraPose& rhs) {
            return std::is_eq(lhs.x <=> rhs.x) && std::is_eq(lhs.z <=> rhs.z);
        };
        renderer->resetCamera();
        drawFrames(*renderer, *device, zone_starts.front(), 0.0, 1);
        const common::core::HighwayCameraPose framed = renderer->cameraPose();
        drawFrames(*renderer, *device, zone_starts.back(), 1.0 / 60.0, 1);
        const common::core::HighwayCameraPose gliding = renderer->cameraPose();
        CHECK_FALSE(same_pose(gliding, framed));
        renderer->setViewState(std::make_shared<const common::core::HighwayViewState>(families));
        drawFrames(*renderer, *device, zone_starts.back(), 0.0, 1);
        CHECK(same_pose(renderer->cameraPose(), gliding));
        renderer->resetCamera();
        drawFrames(*renderer, *device, zone_starts.back(), 0.0, 1);
        CHECK_FALSE(same_pose(renderer->cameraPose(), gliding));

        // The reading aids only decide whether an element draws, so turning them off mid-glide
        // leaves the camera where it is; the sweep after it draws the whole content with every
        // aid off — no note targets, no rolling flip, no arpeggio mark.
        drawFrames(*renderer, *device, zone_starts.front(), 1.0 / 60.0, 1);
        const common::core::HighwayCameraPose before_aids_off = renderer->cameraPose();
        renderer->setDisplayOptions(
            common::core::HighwayDisplayOptions{
                .view =
                    common::core::HighwayViewPreferences{
                        .mirrored = false,
                        .invert_string_order = true,
                        .show_note_targets = false,
                        .show_note_rotation = false,
                        .show_arpeggio_brackets = false,
                    },
                .minimum_string_count = 0,
            });
        drawFrames(*renderer, *device, zone_starts.front(), 0.0, 1);
        CHECK(same_pose(renderer->cameraPose(), before_aids_off));
        drawFrames(*renderer, *device, -2.0, 2.0 * sweep_step_seconds, 150);

        // A content swap mid-sweep, under the non-default display mapping.
        const common::core::Arrangement swap_arrangement = makeSwapArrangement();
        renderer->setDisplayOptions(
            common::core::HighwayDisplayOptions{
                .view =
                    common::core::HighwayViewPreferences{
                        .mirrored = true,
                        .invert_string_order = false,
                        .show_note_targets = true,
                        .show_note_rotation = true,
                        .show_arpeggio_brackets = true,
                    },
                .minimum_string_count = 7,
            });
        renderer->setViewState(
            std::make_shared<const common::core::HighwayViewState>(makeHighwayViewState(
                makeChartViewState(swap_arrangement, tempo_map), tempo_map, makeSmokeSections())));
        drawFrames(*renderer, *device, -1.0, (last_beat_seconds + 2.0) / 120.0, 60);

        // A look that differs only in color, swapped in mid-sweep: setTheme rebuilds only the
        // string lines, and the frames after it encode under the new colors.
        HighwayTheme recolored = testing::fixtureHighwayTheme();
        recolored.backdrop = 0xFF202040U;
        recolored.chord = 0xFFFF00FFU;
        StringColorPalette recolored_strings = testing::fixtureStringColors();
        recolored_strings.colors.front() = 0xFF40FF40U;
        renderer->setTheme(recolored, recolored_strings, HighwayPlainLook{});
        drawFrames(*renderer, *device, -1.0, (last_beat_seconds + 2.0) / 120.0, 30);

        // An arrangement with no chart at all: empty scene, no beats, no camera zones — the
        // state the editor preview holds when its song has no arrangement loaded. Back under the
        // default display mapping.
        renderer->setDisplayOptions(common::core::HighwayDisplayOptions{});
        const common::core::Arrangement empty_arrangement{};
        const common::core::HighwayViewState empty =
            makeHighwayViewState(makeChartViewState(empty_arrangement, tempo_map), tempo_map, {});
        CHECK(empty.chart.notes.empty());
        CHECK(empty.beats.empty());
        renderer->setViewState(std::make_shared<const common::core::HighwayViewState>(empty));
        drawFrames(*renderer, *device, 0.0, 1.0 / 60.0, 20);

        // Capacity probe: the largest batches the encoder builds, swept across the four dense
        // measures so the drawn window is saturated with accented tremolo tails throughout.
        const common::core::Arrangement dense_arrangement = makeDenseAccentArrangement();
        const common::core::HighwayViewState dense =
            makeHighwayViewState(makeChartViewState(dense_arrangement, tempo_map), tempo_map, {});
        CHECK(dense.chart.notes.size() > std::size_t{100});
        // It is also the no-placements frame: with no track the fretting hand's light stands at
        // the reference nut window, lit by the open strings' evidence.
        CHECK(dense.fret_hand.track.empty());
        CHECK_FALSE(dense.fret_hand.lit.empty());
        renderer->setViewState(std::make_shared<const common::core::HighwayViewState>(dense));
        drawFrames(*renderer, *device, 0.0, 4.0 / 40.0, 40);
    }

    // Reaching here means the renderer released every GPU handle it owned against a live bgfx,
    // which is the teardown order its contract requires; the device outlives this case and shuts
    // bgfx down at process exit. One more frame proves the device still encodes after the
    // renderer that was feeding it is gone.
    device->submitFrame();
}

} // namespace rock_hero::common::ui
