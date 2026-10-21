#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <compare>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/plugin/plugin_chain_snapshot.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
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
using common::core::testing::downbeat;

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

// A controller with the supplied song loaded, ready to drive section verbs.
struct LoadedSectionEditor : SongEditorHarness
{
    // The plain case: a song and an empty audible chain, which is what every section verb needs.
    explicit LoadedSectionEditor(common::core::Song song)
        : LoadedSectionEditor{std::move(song), {}}
    {}

    // The same, with the audible tone's plugin chain the fake rig reports for the loaded song, so a
    // test can watch what a re-published snapshot would do to the panel.
    LoadedSectionEditor(
        common::core::Song song, std::vector<common::audio::PluginChainEntry> audible_chain)
    {
        live_rig.next_load_result.plugins = std::move(audible_chain);
        open(std::move(song));
    }

    // Seeks the transport into a measure so the marker rule resolves there. 120 BPM 4/4 puts
    // measure N's downbeat at (N - 1) * 2 seconds.
    void seekToMeasure(int measure)
    {
        seek(static_cast<double>(measure - 1) * 2.0);
    }

    // What the section chord would do right now, as the surface reads it: the verb the core
    // publishes, which is the whole of the chord's decision.
    [[nodiscard]] SectionChordTarget publishedSectionChordTarget() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr ? state->section_chord_target : SectionChordTarget{};
    }

    // The published section views, which is where the selection outline is read from.
    [[nodiscard]] std::vector<SongSectionViewState> publishedSections() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr ? state->sections : std::vector<SongSectionViewState>{};
    }

    // The section labels the published 3D highway state carries, in order, as the board draws them.
    [[nodiscard]] std::vector<std::string> publishedHighwaySectionNames() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        if (state == nullptr || state->highway == nullptr)
        {
            return {};
        }
        std::vector<std::string> names;
        names.reserve(state->highway->sections.size());
        for (const common::core::HighwaySectionViewState& section : state->highway->sections)
        {
            names.push_back(section.name);
        }
        return names;
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
    [[nodiscard]] double publishedOutputLevelDb() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr ? state->signal_chain.output.level.db : 0.0;
    }

    // True while the published state offers an undo, which is how a refusal proves it pushed
    // nothing rather than pushing an entry that happens to restore the same list.
    [[nodiscard]] bool undoAvailable() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr && state->undo_enabled;
    }
};

} // namespace

// Adding a section round-trips exactly through undo and redo, because every section edit carries
// the whole list before and after, so undo is an assignment rather than a replayed inverse.
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
// reads the cursor's slot and answers insert-here or rename-this. The cursor is the PAUSED cursor
// where no caret is armed and the caret where one is, so the chord is reachable without arming
// first and the two readings are one rule rather than two.
TEST_CASE("The published section chord names the verb at the cursor's slot", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(3), .name = "Chorus"}})};

    editor.seekToMeasure(2);
    const SectionChordTarget parked = editor.publishedSectionChordTarget();
    const auto* const parked_insert = std::get_if<InsertSectionTarget>(&parked);
    REQUIRE(parked_insert != nullptr);
    CHECK(parked_insert->position == downbeat(2));
    // Nothing stands there, which is why the verb is an insert.
    CHECK(std::ranges::none_of(editor.publishedSections(), [](const SongSectionViewState& section) {
        return section.position == GridPosition{.measure = 2, .beat = 1};
    }));

    // Arming a caret at that same cursor answers the same verb.
    editor.armCaretAtCursor();
    const SectionChordTarget armed = editor.publishedSectionChordTarget();
    const auto* const armed_insert = std::get_if<InsertSectionTarget>(&armed);
    REQUIRE(armed_insert != nullptr);
    CHECK(armed_insert->position == downbeat(2));

    // A cursor exactly on a section's start renames it, on its own name — the name the core
    // publishes with the verb, so the prompt needs no lookup of its own.
    editor.seekToMeasure(3);
    const SectionChordTarget over_section = editor.publishedSectionChordTarget();
    const auto* const rename = std::get_if<RenameSectionTarget>(&over_section);
    REQUIRE(rename != nullptr);
    CHECK(rename->position == downbeat(3));
    CHECK(rename->name == "Chorus");

    // Inside the section but off its start the verb is an insert there: a section is an instant,
    // not a measure, so standing in its passage addresses nothing.
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{5.0});
    const SectionChordTarget inside = editor.publishedSectionChordTarget();
    const auto* const inside_insert = std::get_if<InsertSectionTarget>(&inside);
    REQUIRE(inside_insert != nullptr);
    CHECK(inside_insert->position == GridPosition{.measure = 3, .beat = 3});
}

// The chord reads the slot an arrow press would arm on, exactly as the tone and tempo chords do:
// the nearest line of the placement quantum with snap on, the tick with it off.
TEST_CASE("The section chord names the cursor's placement slot", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong({})};
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});

    // 3.8 s is measure 2, 3.6 beats in; the nearest quarter line is measure 3's downbeat.
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{3.8});
    const SectionChordTarget snapped = editor.publishedSectionChordTarget();
    const auto* const snapped_insert = std::get_if<InsertSectionTarget>(&snapped);
    REQUIRE(snapped_insert != nullptr);
    CHECK(snapped_insert->position == downbeat(3));

    // With snap off the slot is the tick: three fifths of a beat past beat 4 of measure 2.
    turnGridSnapOff(editor.controller);
    const SectionChordTarget unsnapped = editor.publishedSectionChordTarget();
    const auto* const unsnapped_insert = std::get_if<InsertSectionTarget>(&unsnapped);
    REQUIRE(unsnapped_insert != nullptr);
    CHECK(
        unsnapped_insert->position ==
        GridPosition{.measure = 2, .beat = 4, .offset = common::core::Fraction{3, 5}});
}

// Ctrl+M at a mid-measure slot inserts THERE, and a second press at the same cursor renames what
// the first made: the create-then-name double press works off any slot, not only a downbeat.
TEST_CASE("The section chord inserts and then renames at a mid-measure slot", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong({})};
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});
    constexpr GridPosition pickup{.measure = 2, .beat = 3};

    // 3.0 s is beat 3 of measure 2 exactly.
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{3.0});
    const SectionChordTarget first = editor.publishedSectionChordTarget();
    const auto* const insert = std::get_if<InsertSectionTarget>(&first);
    REQUIRE(insert != nullptr);
    CHECK(insert->position == pickup);

    editor.controller.onSongSectionInsertRequested(insert->position, "Pickup");
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().position == pickup);

    const SectionChordTarget second = editor.publishedSectionChordTarget();
    const auto* const rename = std::get_if<RenameSectionTarget>(&second);
    REQUIRE(rename != nullptr);
    CHECK(rename->position == pickup);
    CHECK(rename->name == "Pickup");
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

// The chord AUTHORS at the cursor and never reads the selection: a chip outlined elsewhere leaves
// the verb naming the cursor's own measure, so Ctrl+M cannot rename a section the charter is not
// standing in.
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
    CHECK(insert->position == downbeat(4));
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

// A restate SELECTS its target, so the section the chord addressed is left under Enter, Delete and
// Alt+arrows. A rename to the same name records nothing — and still selects, because what the
// press addressed is what the charter is working on either way.
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

// A section is a named instant anywhere on the grid, so the insert stores the slot it was handed
// and snaps nothing: a section half a measure into measure 2 starts half a measure into measure 2.
TEST_CASE("Section insert keeps the slot it was handed", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong({})};

    constexpr GridPosition mid_measure{.measure = 2, .beat = 3};
    editor.controller.onSongSectionInsertRequested(mid_measure, "Verse");
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().position == mid_measure);
}

// Section insert refuses rather than overwriting: a blank name and a slot another section already
// holds each leave the list alone and record no undo entry.
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

// Section insert refuses the song's closing barline rather than clamping it back into the song.
TEST_CASE("Section insert refuses a measure past the song end", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong({})};

    // Measure 5 beat 1 is the terminal anchor: a section there would name a passage of no length.
    editor.controller.onSongSectionInsertRequested(downbeat(5), "Outro");
    CHECK(editor.sections().empty());
}

// A rename round-trips exactly through undo and redo.
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

// The 3D highway's labels and camera zones are projected from the sections, so a section edit must
// rebuild that projection on its own — and so must its undo and redo — with no chart or tempo edit
// in between to advance the revision the projection cache is keyed on.
TEST_CASE("A section rename and its undo reach the published highway labels", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"}})};
    CHECK(editor.publishedHighwaySectionNames() == std::vector<std::string>{"VERSE"});

    editor.controller.onSongSectionRenameRequested(downbeat(2), "Chorus");
    CHECK(editor.publishedHighwaySectionNames() == std::vector<std::string>{"CHORUS"});

    editor.controller.onUndoRequested();
    CHECK(editor.publishedHighwaySectionNames() == std::vector<std::string>{"VERSE"});

    editor.controller.onRedoRequested();
    CHECK(editor.publishedHighwaySectionNames() == std::vector<std::string>{"CHORUS"});
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
    CHECK(
        flashedRulerChips(editor.view) ==
        std::vector{RulerChipKey{.row = RulerChipRow::Section, .start = downbeat(2)}});

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
    editor.controller.onToneOutputPreviewChanged(
        common::audio::ToneOutput{.level = common::audio::Gain{-6.0}, .pan = common::audio::Pan{}});
    editor.live_rig.next_load_result.output =
        common::audio::ToneOutput{.level = common::audio::Gain{-6.0}, .pan = common::audio::Pan{}};

    editor.controller.onSongSectionSelected(downbeat(2));
    editor.controller.onSongSectionRenameRequested(downbeat(2), "Chorus");
    REQUIRE(editor.sections().front().name == "Chorus");

    CHECK(editor.publishedPlugins() == chain_before);
    CHECK(std::is_eq(editor.publishedOutputLevelDb() <=> -6.0));

    // Releasing the fader records the whole drag as one entry from the value it started at, which
    // is only possible while the preview survived the rename. Undo therefore answers the fader, not
    // the rename.
    editor.controller.onToneOutputChanged(
        common::audio::ToneOutput{.level = common::audio::Gain{-6.0}, .pan = common::audio::Pan{}});
    editor.controller.onUndoRequested();
    CHECK(std::is_eq(editor.publishedOutputLevelDb() <=> 0.0));
    CHECK(editor.sections().front().name == "Chorus");
}

// Alt+arrows move the selected section one line of the placement quantum, the step every marker
// start takes, and bring the cursor to the slot it moved to, as every selection move of a marker
// does.
TEST_CASE("EditorController moves the selected section a placement line", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"}})};
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});
    editor.controller.onSongSectionSelected(downbeat(2));

    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().position == GridPosition{.measure = 2, .beat = 2});
    CHECK_THAT(editor.transport.current_position.seconds, Catch::Matchers::WithinAbs(2.5, 1e-9));

    // The position is the section's identity, so the selection moved with it and a second press
    // keeps going instead of losing its operand.
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(editor.sections().front().position == downbeat(2));
    CHECK_THAT(editor.transport.current_position.seconds, Catch::Matchers::WithinAbs(2.0, 1e-9));

    // Both presses are one held run, which walked back to where it began and so recorded nothing.
    CHECK(editor.undoEntryCount() == 0);
}

// A section move refuses rather than swapping or merging: a step onto a slot another section holds,
// onto the terminal anchor, or earlier than the song's start leaves it in place, and each refusal
// glows the section's chip, as a refused tempo, meter or hand-position chip does.
TEST_CASE("Section move refuses an occupied slot and the song's ends", "[core][sections]")
{
    const auto section_chip = [](const GridPosition start) {
        return RulerChipKey{.row = RulerChipRow::Section, .start = start};
    };
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"},
         SongSection{.position = GridPosition{.measure = 2, .beat = 2}, .name = "Chorus"}})};
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});
    editor.controller.onSongSectionSelected(downbeat(2));

    // Beat 2 already holds a section: refused, not swapped and not merged, and the cursor stays.
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    REQUIRE(editor.sections().size() == 2);
    CHECK(editor.sections()[0].position == downbeat(2));
    CHECK(editor.sections()[1].position == GridPosition{.measure = 2, .beat = 2});
    CHECK_THAT(editor.transport.current_position.seconds, Catch::Matchers::WithinAbs(0.0, 1e-9));
    CHECK(flashedRulerChips(editor.view) == std::vector{section_chip(downbeat(2))});

    // Beat 4 of measure 4 is the last slot that can carry a section, ten quarter lines on from 2:2;
    // 5:1 is the closing barline, and the step onto it is refused.
    constexpr GridPosition last_slot{.measure = 4, .beat = 4};
    editor.controller.onSongSectionSelected(GridPosition{.measure = 2, .beat = 2});
    editor.pressAlt(ChartStepDirection::Right, 10);
    CHECK(editor.sections()[1].position == last_slot);
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.sections()[1].position == last_slot);

    // 1:1 is the first slot, four quarter lines back from 2:1: a step earlier from there has
    // nowhere to go, so it is refused and records nothing on top of the run that got there.
    editor.controller.onSongSectionSelected(downbeat(2));
    editor.pressAlt(ChartStepDirection::Left, 4);
    CHECK(editor.sections()[0].position == downbeat(1));
    const std::size_t entries = editor.undoEntryCount();
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(editor.sections()[0].position == downbeat(1));
    CHECK(editor.undoEntryCount() == entries);
    CHECK(flashedRulerChips(editor.view).back() == section_chip(downbeat(1)));
}

// Delete removes the selected section and round-trips exactly through undo and redo.
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

// Deleting a section selects the one before it on the row, as a chip click would, so a run of
// deletes walks back along the row.
TEST_CASE("Deleting a section selects the section before it", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"},
         SongSection{.position = downbeat(3), .name = "Chorus"}})};
    editor.controller.onSongSectionSelected(downbeat(3));

    editor.controller.onSelectionDeleteRequested();
    const std::vector<SongSectionViewState> published = editor.publishedSections();
    REQUIRE(published.size() == 1);
    CHECK(published[0].selected);
    CHECK(editor.selectionPresent());
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
