#include "chart/legato_normalize.h"

#include <array>
#include <cstddef>
#include <limits>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <vector>

namespace rock_hero::editor::core
{

void normalizeChartLegato(
    std::vector<common::core::ChartNote>& notes,
    const std::vector<common::core::ChartShape>& shapes, const common::core::TempoMap& tempo_map)
{
    // Judge against the SAVED form, never the in-memory one. The repair aims at what the gate
    // will validate, and the gate validates saved notes — so reading in-memory values here would
    // let the two disagree about the same note. They did: a pick slide's latent mute made an
    // all-muted onset group read as choked in memory and as held once saved, which flipped a
    // span's hold extension and silently downgraded a following pull-off that validation accepts.
    // Judging the saved form retires that whole class rather than one field of it, and it stays
    // correct as savedChartNote grows: the two streams are index-parallel because savedChartNote
    // never touches position, string, or attack.
    std::vector<common::core::ChartNote> judged;
    judged.reserve(notes.size());
    for (const common::core::ChartNote& note : notes)
    {
        judged.push_back(common::core::savedChartNote(note));
    }
    // Held lengths for the hold test, span-extended where the spans imply a held shape. Computed
    // once up front: repairs only ever change an attack, never a position or a sustain, so the
    // table cannot go stale inside the walk.
    const std::vector<common::core::Fraction> effective_sustains =
        common::core::chartEffectiveSustains(judged, shapes, tempo_map);
    // The last note seen per string: the stream is sorted, so this IS each note's same-string
    // predecessor when it is reached. Repairs apply before a note is recorded, so a later note
    // judges against the repaired values.
    constexpr std::size_t no_predecessor = std::numeric_limits<std::size_t>::max();
    std::array<std::size_t, static_cast<std::size_t>(common::core::g_max_chart_strings) + 1>
        last_per_string{};
    last_per_string.fill(no_predecessor);
    for (std::size_t note_index = 0; note_index < notes.size(); ++note_index)
    {
        common::core::ChartNote& note = notes[note_index];
        const bool string_in_range =
            note.string >= 1 && note.string <= common::core::g_max_chart_strings;
        const std::size_t source_index =
            string_in_range ? last_per_string.at(static_cast<std::size_t>(note.string))
                            : no_predecessor;
        // The predecessor is read from the judged (saved) stream; the note being repaired is not,
        // because a Pull or a Hammer is never a pick slide, so savedChartNote leaves the fields
        // these rules read on it untouched.
        const common::core::ChartNote* const source =
            source_index == no_predecessor ? nullptr : &judged[source_index];
        // Every repair writes through here so the two streams cannot drift: `judged` is what
        // later notes are judged against, and it must carry the attack this walk just decided.
        const auto repair = [&note, &judged, note_index](const common::core::NoteAttack attack) {
            note.attack = attack;
            judged[note_index].attack = attack;
        };
        // What the predecessor actually justifies. `derivedLegatoAttack` is the one authority for
        // that question, so nothing below re-derives it: the only decisions left here are WHEN the
        // stream's own attack is impossible, and which repairs may reach for a derived one.
        const common::core::Fraction source_sustain = source_index == no_predecessor
                                                          ? common::core::Fraction{}
                                                          : effective_sustains[source_index];
        if (note.attack == common::core::NoteAttack::Pull)
        {
            // A pull-off the stream no longer supports becomes whatever it does support: the
            // hammer-on a lower released fret justifies, else a plain pick — because hammering
            // after a release is the left-hand tap, Ctrl+H's domain, never derived.
            const common::core::NoteAttack derived =
                common::core::derivedLegatoAttack(note, source, source_sustain, tempo_map);
            if (derived != common::core::NoteAttack::Pull)
            {
                repair(derived);
            }
        }
        else if (
            (note.attack == common::core::NoteAttack::Hammer ||
             note.attack == common::core::NoteAttack::Tap) &&
            note.fret == 0 && !note.harmonic_node.has_value()
        )
        {
            // Nothing to strike (E4), which binds Hammer and Tap alike — the open string has no
            // fret to land on and no node to strike. A HAMMER re-reads as whatever the predecessor
            // justifies, which for a strikeless note can only be the pull-off a still-held higher
            // press supports, or a plain pick. A tap with nowhere to strike is simply not a tap,
            // and turning it into a pull would invent legato out of a picking-hand articulation.
            // Covering Tap here rather than in a second pass matters because a junk `Tapped` flag
            // on an open string is real Guitar Pro data, and it used to fail E4 at validation and
            // take the WHOLE song's import down.
            repair(
                note.attack == common::core::NoteAttack::Hammer
                    ? common::core::derivedLegatoAttack(note, source, source_sustain, tempo_map)
                    : common::core::NoteAttack::Pick);
        }
        if (string_in_range)
        {
            last_per_string.at(static_cast<std::size_t>(note.string)) = note_index;
        }
    }
}

} // namespace rock_hero::editor::core
