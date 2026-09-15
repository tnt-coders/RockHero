#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <compare>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/plugin/plugin_chain_snapshot.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/chart/chart_pointer.h>
#include <rock_hero/editor/core/signal_chain/plugin_view_state.h>
#include <rock_hero/editor/core/testing/chart_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::GridPosition;
using common::core::SongSection;

[[nodiscard]] GridPosition downbeat(int measure)
{
    return GridPosition{.measure = measure, .beat = 1};
}

// A 4/4 map at 120 BPM whose terminal downbeat is measure 5 beat 1, so measures 1-4 can carry a
// section and measure 5 is the closing barline every out-of-song refusal is measured against. The
// audio runs longer than the map on purpose, so the transport can rest at or past the terminal
// downbeat without the seek clamping short of it — which is the only way to aim a verb there.
[[nodiscard]] common::core::Song makeSectionSong(std::vector<SongSection> sections)
{
    common::core::Song song = makeSong(
        std::filesystem::path{"song.wav"},
        loadedTimelineRange(16.0),
        std::string{g_tone_document_ref});
    song.tempo_map = common::core::TempoMap::defaultMap(common::core::TimeDuration{8.0});
    song.sections = std::move(sections);
    // A chart, so a caret can arm: the marker every section verb lands on IS the armed caret.
    song.arrangements.front().chart = makeTestChart();
    return song;
}

// One plugin the fake rig reports as part of the audible tone's chain, so the signal-chain panel a
// section verb must leave alone has rows to compare rather than being empty either way.
[[nodiscard]] common::audio::PluginChainEntry makeChainEntry(
    std::string instance_id, std::size_t chain_index)
{
    return common::audio::PluginChainEntry{
        .instance_id = std::move(instance_id),
        .plugin_id = "plugin-" + std::to_string(chain_index),
        .name = "Plugin " + std::to_string(chain_index),
        .manufacturer = "Tests",
        .format_name = "VST3",
        .category = {},
        .chain_index = chain_index,
        .block_index = chain_index,
        .display_type_override = {},
    };
}

// Owns the fakes and a controller with the supplied song loaded, ready to drive section verbs.
struct LoadedSectionEditor
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    EditorController controller;
    FakeEditorView view;

    // The plain case: a song and an empty audible chain, which is what every section verb needs.
    explicit LoadedSectionEditor(common::core::Song song)
        : LoadedSectionEditor{std::move(song), {}}
    {}

    // The same, with the audible tone's plugin chain the fake rig reports for the loaded song, so a
    // test can watch what a re-published snapshot would do to the panel.
    LoadedSectionEditor(
        common::core::Song song, std::vector<common::audio::PluginChainEntry> audible_chain)
        : controller{
              audioPorts(transport, audio, plugin_host, live_rig),
              defaultControllerServices(),
              noopExitFunction(),
              EditorController::ProjectOperations{
                  .open_function = project_services.openFunction(),
              },
          }
    {
        // The prepared duration is what the loaded arrangement's timeline ends up being, and the
        // timeline is what a seek clamps to; without it the fake's 4 s default would stop the
        // transport two measures short of the terminal downbeat these tests aim at.
        audio.next_prepared_audio_duration = common::core::TimeDuration{16.0};
        audio.next_set_active_arrangement_result = true;
        live_rig.next_load_result.plugins = std::move(audible_chain);
        project_services.next_song = std::move(song);
        controller.attachView(view);
        controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    }

    [[nodiscard]] const std::vector<SongSection>& sections() const
    {
        return controller.session().song().sections;
    }

    // Seeks the transport into a measure so the marker rule resolves there. 120 BPM 4/4 puts
    // measure N's downbeat at (N - 1) * 2 seconds.
    void seekToMeasure(int measure)
    {
        controller.onTimelineSeekRequested(
            common::core::TimePosition{static_cast<double>(measure - 1) * 2.0});
    }

    // What the section chord would do right now, as the surface reads it: the verb the core
    // publishes, which is the whole of the chord's decision.
    [[nodiscard]] SectionChordTarget publishedSectionChordTarget() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr ? state->section_chord_target : SectionChordTarget{};
    }

    // Arms the caret at the parked cursor: the first arrow press on a passive marker arms without
    // stepping, so this is the keyboard's own way of placing a marker verb's position.
    void armCaretAtCursor()
    {
        controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    }

    // Turns snapping off, which only the warning's own answer can do.
    void turnSnapOff()
    {
        controller.onGridSnapToggleRequested();
        controller.onGridSnapWarningDecision(GridSnapWarningDecision::TurnSnappingOff);
    }

    // The published section views, which is where the selection outline is read from.
    [[nodiscard]] std::vector<SongSectionViewState> publishedSections() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr ? state->sections : std::vector<SongSectionViewState>{};
    }

    // True while the published state says a selection exists on some surface.
    [[nodiscard]] bool selectionPresent() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr && state->selection_present;
    }

    // The published signal-chain rows, where the authored visual block placement is read from.
    [[nodiscard]] std::vector<PluginViewState> publishedPlugins() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr ? state->signal_chain.plugins : std::vector<PluginViewState>{};
    }

    // The published output fader value, which a preview moves ahead of any committed value.
    [[nodiscard]] double publishedOutputGainDb() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr ? state->signal_chain.output_gain.db : 0.0;
    }

    // True while the published state offers an undo, which is how a refusal proves it pushed
    // nothing rather than pushing an entry that happens to restore the same list.
    [[nodiscard]] bool undoAvailable() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr && state->undo_enabled;
    }

    // How many entries the undo history holds, so a verb that must record NOTHING can be told from
    // one that records an entry restoring the same list.
    [[nodiscard]] std::size_t undoEntryCount() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr ? state->undo_history.labels.size() : 0;
    }
};

} // namespace

// The whole point of the one-edit design: add, rename, move and delete all round-trip exactly,
// because each carries both whole lists and undo is an assignment rather than a replayed inverse.
TEST_CASE("EditorController adds a section at the marker's measure", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong({})};

    editor.controller.onSongSectionInsertRequested(downbeat(3), "Chorus");
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().position == downbeat(3));
    CHECK(editor.sections().front().name == "Chorus");

    editor.controller.onUndoRequested();
    CHECK(editor.sections().empty());

    editor.controller.onRedoRequested();
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().name == "Chorus");
}

// The chord's whole decision is published as the VERB, so no surface reconstructs it: the core
// reads the cursor, snaps it to its measure's downbeat, and answers insert-here or rename-this. The
// cursor is the PAUSED cursor where no caret is armed and the caret where one is, so the chord is
// reachable without arming first and the two readings are one rule rather than two.
TEST_CASE("The published section chord names the verb at the cursor's measure", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(3), .name = "Chorus"}})};

    editor.seekToMeasure(2);
    const SectionChordTarget parked = editor.publishedSectionChordTarget();
    const auto* const parked_insert = std::get_if<InsertSectionTarget>(&parked);
    REQUIRE(parked_insert != nullptr);
    CHECK(parked_insert->downbeat == downbeat(2));
    // Nothing stands there, which is why the verb is an insert.
    CHECK(std::ranges::none_of(editor.publishedSections(), [](const SongSectionViewState& section) {
        return section.position == GridPosition{.measure = 2, .beat = 1};
    }));

    // Arming a caret at that same cursor answers the same verb.
    editor.armCaretAtCursor();
    const SectionChordTarget armed = editor.publishedSectionChordTarget();
    const auto* const armed_insert = std::get_if<InsertSectionTarget>(&armed);
    REQUIRE(armed_insert != nullptr);
    CHECK(armed_insert->downbeat == downbeat(2));

    // A cursor in the measure a section already opens renames it, on its own name — the name the
    // core publishes with the verb, so the prompt needs no lookup of its own.
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{5.0});
    const SectionChordTarget over_section = editor.publishedSectionChordTarget();
    const auto* const rename = std::get_if<RenameSectionTarget>(&over_section);
    REQUIRE(rename != nullptr);
    CHECK(rename->position == downbeat(3));
    CHECK(rename->name == "Chorus");
}

// The chord reads the cursor's TICK, not the slot an arrow press would take there: a cursor late in
// a measure names the measure it is IN, where a section can actually start, even when the nearest
// grid line lies in the next one. Snap moves the slot and never this, so the two states agree.
TEST_CASE("The section chord names the measure the cursor is in", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong({})};
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});

    // 3.8 s is measure 2, three and a half beats in; the nearest quarter line is measure 3's
    // downbeat, so reading the slot would author a measure late.
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{3.8});
    const SectionChordTarget snapped = editor.publishedSectionChordTarget();
    const auto* const snapped_insert = std::get_if<InsertSectionTarget>(&snapped);
    REQUIRE(snapped_insert != nullptr);
    CHECK(snapped_insert->downbeat == downbeat(2));

    editor.turnSnapOff();
    const SectionChordTarget unsnapped = editor.publishedSectionChordTarget();
    const auto* const unsnapped_insert = std::get_if<InsertSectionTarget>(&unsnapped);
    REQUIRE(unsnapped_insert != nullptr);
    CHECK(unsnapped_insert->downbeat == downbeat(2));
}

// The verb is nothing exactly where the commit would refuse it, so the press is inert rather than
// prompting for a name it cannot use: the terminal downbeat carries no section, a playing transport
// closes the whole marker plane, and with no song there is nothing to hold a marker.
TEST_CASE("The section chord is nothing where no section can start", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong({})};

    // Measure 5 beat 1 is the closing barline: a section there would name a passage of no length.
    editor.seekToMeasure(5);
    const SectionChordTarget terminal = editor.publishedSectionChordTarget();
    CHECK(std::holds_alternative<std::monostate>(terminal));

    // A chord must never land a beat late off a moving transport, so it answers nothing at all.
    editor.transport.current_state.playing = true;
    editor.seekToMeasure(2);
    const SectionChordTarget playing = editor.publishedSectionChordTarget();
    CHECK(std::holds_alternative<std::monostate>(playing));

    editor.transport.current_state.playing = false;
    editor.controller.onCloseRequested();
    const SectionChordTarget closed = editor.publishedSectionChordTarget();
    CHECK(std::holds_alternative<std::monostate>(closed));
}

// The chord AUTHORS at the cursor and never reads the selection (Phase 3 retired the grammar's
// select-at-the-cursor rule): a chip outlined elsewhere leaves the verb naming the cursor's own
// measure, so Ctrl+M cannot rename a section the charter is not standing in.
TEST_CASE("The section chord ignores the selected chip", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(3), .name = "Chorus"}})};

    // Park the cursor in a free measure first: selecting a chip seeks nothing, while a cursor move
    // would release the chip.
    editor.seekToMeasure(4);
    editor.controller.onSongSectionSelected(downbeat(3));
    const std::vector<SongSectionViewState> published = editor.publishedSections();
    REQUIRE(published.size() == 1);
    REQUIRE(published.front().selected);

    const SectionChordTarget target = editor.publishedSectionChordTarget();
    const auto* const insert = std::get_if<InsertSectionTarget>(&target);
    REQUIRE(insert != nullptr);
    CHECK(insert->downbeat == downbeat(4));
}

// An insert leaves what it made selected, as a typed note does, so Enter, Delete and Alt+arrows act
// on the new section; the caret it was typed from demotes in place. The cursor never left its
// measure, so the chord's own verb becomes the RENAME of what the first press made — which is what
// makes the double press a create-then-name.
TEST_CASE(
    "Section insert selects the new section and leaves the chord renaming it", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong({})};
    editor.seekToMeasure(3);
    editor.armCaretAtCursor();
    const SectionChordTarget before = editor.publishedSectionChordTarget();
    REQUIRE(std::holds_alternative<InsertSectionTarget>(before));

    editor.controller.onSongSectionInsertRequested(downbeat(3), "Chorus");
    REQUIRE(editor.sections().size() == 1);
    const std::vector<SongSectionViewState> published = editor.publishedSections();
    REQUIRE(published.size() == 1);
    CHECK(published.front().selected);

    const SectionChordTarget after = editor.publishedSectionChordTarget();
    const auto* const rename = std::get_if<RenameSectionTarget>(&after);
    REQUIRE(rename != nullptr);
    CHECK(rename->position == downbeat(3));
    CHECK(rename->name == "Chorus");

    // Re-arming the caret changes nothing: the caret is the cursor, at the same slot.
    editor.armCaretAtCursor();
    const SectionChordTarget rearmed = editor.publishedSectionChordTarget();
    CHECK(std::holds_alternative<RenameSectionTarget>(rearmed));
}

// A restate SELECTS its target (grammar rule 4), so the section the chord addressed is left under
// Enter, Delete and Alt+arrows. A rename to the same name records nothing — and still selects,
// because what the press addressed is what the charter is now working on either way.
TEST_CASE("A section rename selects the section it addressed", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"}})};
    REQUIRE_FALSE(editor.selectionPresent());

    editor.controller.onSongSectionRenameRequested(downbeat(2), "Chorus");
    const std::vector<SongSectionViewState> renamed = editor.publishedSections();
    REQUIRE(renamed.size() == 1);
    CHECK(renamed.front().name == "Chorus");
    CHECK(renamed.front().selected);
    const std::size_t entries = editor.undoEntryCount();
    REQUIRE(entries == 1);

    editor.controller.onSongSectionSelected(std::nullopt);
    REQUIRE_FALSE(editor.selectionPresent());

    editor.controller.onSongSectionRenameRequested(downbeat(2), "Chorus");
    const std::vector<SongSectionViewState> restated = editor.publishedSections();
    REQUIRE(restated.size() == 1);
    CHECK(restated.front().selected);
    CHECK(editor.undoEntryCount() == entries);
}

// A section starts on a downbeat and nowhere else, so a marker resting mid-measure snaps back to
// that measure's downbeat instead of authoring a position the format would accept but the board
// could not promote a bar for.
TEST_CASE("Section insert snaps the marker to its measure downbeat", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong({})};

    // Half a measure into measure 2: the verb snaps the position it was handed back to measure 2's
    // downbeat.
    editor.controller.onSongSectionInsertRequested(GridPosition{.measure = 2, .beat = 3}, "Verse");
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().position == downbeat(2));
}

// Every section verb refuses rather than clamping or overwriting: an empty name, a downbeat
// another section already holds, and a measure past the closing barline each leave the list alone.
TEST_CASE("Section insert refuses an empty name and a duplicate", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"}})};

    editor.controller.onSongSectionInsertRequested(downbeat(2), "   ");
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().name == "Verse");

    editor.controller.onSongSectionInsertRequested(downbeat(2), "Chorus");
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().name == "Verse");

    // Nothing was committed, so there is no entry to undo.
    CHECK_FALSE(editor.undoAvailable());
}

TEST_CASE("Section insert refuses a measure past the song end", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong({})};

    // Measure 5 beat 1 is the terminal anchor: a section there would name a passage of no length.
    editor.controller.onSongSectionInsertRequested(downbeat(5), "Outro");
    CHECK(editor.sections().empty());
}

TEST_CASE("EditorController renames a section and round-trips it", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"}})};

    editor.controller.onSongSectionRenameRequested(downbeat(2), "Chorus");
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().name == "Chorus");

    editor.controller.onUndoRequested();
    CHECK(editor.sections().front().name == "Verse");

    editor.controller.onRedoRequested();
    CHECK(editor.sections().front().name == "Chorus");
}

// The package reader forbids an empty name, so the verb must refuse one rather than author a
// section that would fail to save.
TEST_CASE("Section rename refuses an empty name", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"}})};

    editor.controller.onSongSectionRenameRequested(downbeat(2), "  ");
    CHECK(editor.sections().front().name == "Verse");
    CHECK_FALSE(editor.undoAvailable());

    // A position no section starts at names nothing to rename.
    editor.controller.onSongSectionRenameRequested(downbeat(3), "Chorus");
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().name == "Verse");
}

// The audible tone is derived from the selection, the cursor and the tone model, so a verb that
// moves none of the three must not republish it. A section rename resolves the same tone, the same
// chain and the same gain, so its sync is a no-op — which is what leaves the panel's chain bound as
// it was and an in-flight fader drag alive. A republish would take both away: it replaces the chain
// snapshot (re-applying the snapshot's own block placement over any authored one, which
// SignalChainWorkflow's own suite pins) and it drops the preview's "before" value, the only thing
// the commit's undo entry can be measured from.
TEST_CASE("Section rename leaves the audible tone's published state alone", "[core][sections]")
{
    LoadedSectionEditor editor{
        makeSectionSong({SongSection{.position = downbeat(2), .name = "Verse"}}),
        {makeChainEntry("amp", 0), makeChainEntry("delay", 1)}
    };
    const std::vector<PluginViewState> chain_before = editor.publishedPlugins();
    REQUIRE(chain_before.size() == 2);

    // Drag the output fader without releasing. FakeLiveRig answers setAudibleTone with its canned
    // load result rather than the gain it was just set to (docs/tracking/backlog.md), so the canned
    // result is made to agree with the fader — which is what the real rig reports back.
    editor.controller.onOutputGainPreviewChanged(-6.0);
    editor.live_rig.next_load_result.output_gain = common::audio::Gain{-6.0};

    editor.controller.onSongSectionSelected(downbeat(2));
    editor.controller.onSongSectionRenameRequested(downbeat(2), "Chorus");
    REQUIRE(editor.sections().front().name == "Chorus");

    CHECK(editor.publishedPlugins() == chain_before);
    CHECK(std::is_eq(editor.publishedOutputGainDb() <=> -6.0));

    // Releasing the fader records the whole drag as one entry from the value it started at, which
    // is only possible while the preview survived the rename. Undo therefore answers the fader, not
    // the rename.
    editor.controller.onOutputGainChanged(-6.0);
    editor.controller.onUndoRequested();
    CHECK(std::is_eq(editor.publishedOutputGainDb() <=> 0.0));
    CHECK(editor.sections().front().name == "Chorus");
}

// Alt+arrows move the selected section one MEASURE, because a measure is the section's step, and
// bring the cursor to the downbeat it moved to, as every selection move of a marker does.
TEST_CASE("EditorController moves the selected section a measure", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"}})};
    editor.controller.onSongSectionSelected(downbeat(2));

    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().position == downbeat(3));
    CHECK_THAT(editor.transport.current_position.seconds, Catch::Matchers::WithinAbs(4.0, 1e-9));

    // The position is the section's identity, so the selection moved with it and a second press
    // keeps going instead of losing its operand.
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(editor.sections().front().position == downbeat(2));
    CHECK_THAT(editor.transport.current_position.seconds, Catch::Matchers::WithinAbs(2.0, 1e-9));

    editor.controller.onUndoRequested();
    CHECK(editor.sections().front().position == downbeat(3));
    editor.controller.onUndoRequested();
    CHECK(editor.sections().front().position == downbeat(2));
}

TEST_CASE("Section move refuses an occupied downbeat and the song end", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"},
         SongSection{.position = downbeat(3), .name = "Chorus"}})};
    editor.controller.onSongSectionSelected(downbeat(2));

    // Measure 3 already holds a section: refused, not swapped and not merged, and the cursor stays.
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    REQUIRE(editor.sections().size() == 2);
    CHECK(editor.sections()[0].position == downbeat(2));
    CHECK(editor.sections()[1].position == downbeat(3));
    CHECK_THAT(editor.transport.current_position.seconds, Catch::Matchers::WithinAbs(0.0, 1e-9));

    // Measure 4 is the last that can carry a section; measure 5 is the closing barline.
    editor.controller.onSongSectionSelected(downbeat(3));
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.sections()[1].position == downbeat(4));
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.sections()[1].position == downbeat(4));

    // Measure 1 is the first: a step earlier from there leaves the grid.
    editor.controller.onSongSectionSelected(downbeat(2));
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(editor.sections()[0].position == downbeat(1));
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(editor.sections()[0].position == downbeat(1));
}

TEST_CASE("EditorController deletes the selected section", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"},
         SongSection{.position = downbeat(3), .name = "Chorus"}})};
    editor.controller.onSongSectionSelected(downbeat(2));

    editor.controller.onSelectionDeleteRequested();
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().name == "Chorus");

    editor.controller.onUndoRequested();
    REQUIRE(editor.sections().size() == 2);
    CHECK(editor.sections()[0].name == "Verse");
    CHECK(editor.sections()[1].name == "Chorus");

    editor.controller.onRedoRequested();
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().name == "Chorus");
}

// The selection is an alternative of the one editor-wide selection, so it publishes through the
// same view state every other kind does, and it is what Delete finds.
TEST_CASE("A section selection round-trips through the view state", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"},
         SongSection{.position = downbeat(3), .name = "Chorus"}})};

    editor.controller.onSongSectionSelected(downbeat(3));
    std::vector<SongSectionViewState> published = editor.publishedSections();
    REQUIRE(published.size() == 2);
    CHECK_FALSE(published[0].selected);
    CHECK(published[1].selected);
    CHECK(editor.selectionPresent());

    // Deselecting releases the alternative and the outline with it.
    editor.controller.onSongSectionSelected(std::nullopt);
    published = editor.publishedSections();
    REQUIRE(published.size() == 2);
    CHECK_FALSE(published[0].selected);
    CHECK_FALSE(published[1].selected);
    CHECK_FALSE(editor.selectionPresent());
}

// A section chip is a timeline marker like a tone region, so it shares that lifecycle: moving the
// cursor releases it. Selecting a chip seeks nothing, which is what lets the pair coexist.
TEST_CASE("A section selection clears when the cursor moves", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"}})};

    editor.controller.onSongSectionSelected(downbeat(2));
    REQUIRE(editor.selectionPresent());

    editor.seekToMeasure(4);
    CHECK_FALSE(editor.selectionPresent());
}

} // namespace rock_hero::editor::core
