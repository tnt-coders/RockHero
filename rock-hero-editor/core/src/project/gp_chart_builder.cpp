#include "project/gp_chart_builder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <compare>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::BendPoint;
using common::core::Chart;
using common::core::ChartNote;
using common::core::ChartShape;
using common::core::ChordTemplate;
using common::core::Fraction;
using common::core::GridPosition;
using common::core::NoteAttack;
using common::core::NoteHarmonic;
using common::core::NoteMute;
using common::core::SlideWaypoint;

// One note event on the global rational beat axis, before tie merging.
struct NoteEvent
{
    Fraction global_beat{}; // onset on the global beat axis
    int measure{1};
    int beat{1};               // one-based beat within the measure
    Fraction offset{};         // sub-beat offset within [0, 1)
    Fraction duration_beats{}; // duration in the onset measure's beat unit
    GpNote source;
    bool tremolo{false};

    // The beat the source NOTATED this event at: the principal's onset for every event of a
    // beat, even when a grace lead or an on-beat shift makes it sound earlier or later. The
    // sustain normalization binds tails only to separately notated onsets, so fabricated onset
    // positions can neither bind a chord-mate's tail nor fake a deliberate hold.
    Fraction notated_beat{};
};

// Per-measure grid facts derived from the master bars once.
struct MeasureGrid
{
    std::vector<int> beats_per_measure; // numerator per measure, index 0 = measure 1
    std::vector<int> denominator;       // denominator per measure
    std::vector<int> first_global_beat; // global beat index of each measure's downbeat
};

[[nodiscard]] MeasureGrid makeMeasureGrid(const GpScore& score)
{
    MeasureGrid grid;
    int global_beat = 0;
    for (const GpMasterBar& bar : score.master_bars)
    {
        grid.beats_per_measure.push_back(bar.numerator);
        grid.denominator.push_back(bar.denominator);
        grid.first_global_beat.push_back(global_beat);
        global_beat += bar.numerator;
    }
    return grid;
}

// The minimum-sustain-distance margin at a position's measure — the ONE statement of the
// margin-per-note rule every trim, span close, glide window, and lead derives from. Note
// positions always index the grid: collectEvents clamps bar indexes into it and
// gridPositionForGlobalBeat looks measures up from it, so no defensive clamp is needed here.
[[nodiscard]] Fraction sustainMarginAt(const MeasureGrid& grid, const GridPosition& position)
{
    const auto measure_index = static_cast<std::size_t>(position.measure - 1);
    return common::core::minimumSustainDistanceBeats(grid.denominator[measure_index]);
}

// Shrinks a per-slot ornament lead so `count` slots fit strictly inside the available gap: when
// the full leads spill over, each slot takes the gap halved and split across the slots
// (gap / 2N). A non-positive gap stays non-positive, which every caller reads as "no room —
// drop the gesture". Fraction's operators keep the intermediate products int64-safe.
[[nodiscard]] constexpr Fraction fitLeadToGap(
    const Fraction lead, const Fraction gap, const int count)
{
    if (Fraction{count} * lead >= gap)
    {
        return gap * Fraction{1, 2 * count};
    }
    return lead;
}

// Sub-beat step used to keep a degenerate glide or slide-out payload strictly after the chain's
// last waypoint (the model's ascending-payload invariant).
constexpr Fraction g_minimum_slide_window{1, 8};

// Bumps a payload window landing on or before the note's last waypoint to one minimum step past
// it, so the payload stays ascending.
[[nodiscard]] Fraction keptStrictlyAfterLastWaypoint(const ChartNote& note, const Fraction window)
{
    if (!note.slides.empty() && window <= note.slides.back().offset)
    {
        return note.slides.back().offset + g_minimum_slide_window;
    }
    return window;
}

// Pitch-class names for MIDI note numbers.
constexpr std::array<const char*, 12> g_midi_note_names{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

// Names the open-string pitch of a MIDI note number ("E2" for 40).
[[nodiscard]] std::string midiNoteName(int midi)
{
    const int octave = midi / 12 - 1;
    return std::string{g_midi_note_names.at(static_cast<std::size_t>(((midi % 12) + 12) % 12))} +
           std::to_string(octave);
}

// Seconds per beat of one measure at a quarter-note BPM: a /8 measure's beat is half a quarter.
[[nodiscard]] double secondsPerBeat(double quarter_bpm, int denominator)
{
    return (60.0 / quarter_bpm) * (4.0 / static_cast<double>(denominator));
}

// Snaps anchor seconds onto the package format's millisecond grid. Guitar Pro's frame offsets
// divided by 44100 almost never land on a whole millisecond, but the package stores anchor
// seconds at three decimals, so an unrounded map imports fine yet cannot be saved. Rounding uses
// the same integer-millisecond quantum the writer uses, and any anchor that would collide with or
// regress past its predecessor is nudged one millisecond later to keep the map strictly ordered.
void snapAnchorsToMillisecondGrid(std::vector<common::core::BeatAnchor>& anchors)
{
    double previous_seconds = -1.0;
    for (common::core::BeatAnchor& anchor : anchors)
    {
        double snapped = static_cast<double>(std::llround(anchor.seconds * 1000.0)) / 1000.0;
        if (snapped <= previous_seconds)
        {
            snapped = previous_seconds + 0.001;
        }
        anchor.seconds = snapped;
        previous_seconds = snapped;
    }
}

// Builds the warp-anchor tempo map from the score's sync points, extending a downbeat terminal
// anchor past the final bar so every note position lies inside the map. Unusable sync points are
// dropped with a conversion note, so the build itself cannot fail.
[[nodiscard]] common::core::TempoMap buildTempoMap(
    const GpScore& score, const MeasureGrid& grid, std::vector<std::string>& notes)
{
    std::vector<common::core::TimeSignatureChange> signatures;
    for (std::size_t measure = 0; measure < grid.beats_per_measure.size(); ++measure)
    {
        if (measure == 0 ||
            grid.beats_per_measure[measure] != grid.beats_per_measure[measure - 1] ||
            grid.denominator[measure] != grid.denominator[measure - 1])
        {
            signatures.push_back(
                common::core::TimeSignatureChange{
                    .measure = static_cast<int>(measure) + 1,
                    .numerator = grid.beats_per_measure[measure],
                    .denominator = grid.denominator[measure],
                });
        }
    }

    std::vector<common::core::BeatAnchor> anchors;
    double last_tempo = score.base_tempo_quarter_bpm;
    for (const GpSyncPoint& sync : score.sync_points)
    {
        const int measure_count = static_cast<int>(grid.beats_per_measure.size());
        if (sync.bar < 0 || sync.bar >= measure_count)
        {
            continue;
        }
        const int beats_in_bar = grid.beats_per_measure[static_cast<std::size_t>(sync.bar)];
        const double beat_position = sync.bar_fraction * beats_in_bar;
        const auto whole_beat = static_cast<int>(std::lround(beat_position));
        if (std::abs(beat_position - whole_beat) > 1e-3)
        {
            notes.emplace_back("dropped an off-beat audio sync point");
            continue;
        }

        int measure = sync.bar + 1;
        int beat = whole_beat + 1;
        if (beat > beats_in_bar)
        {
            // A rollover from the last bar's end lands on the terminal downbeat, which is a
            // legal anchor position: it pins the song's end to the audio exactly.
            measure += 1;
            beat = 1;
        }

        // Anchors must advance strictly in both grid position and audio time; sync points that
        // regress on either axis would corrupt the map, so they are dropped.
        if (!anchors.empty() &&
            (anchors.back().seconds >= sync.seconds || anchors.back().measure > measure ||
             (anchors.back().measure == measure && anchors.back().beat >= beat)))
        {
            continue;
        }
        anchors.push_back(
            common::core::BeatAnchor{.measure = measure, .beat = beat, .seconds = sync.seconds});
        if (sync.modified_tempo > 0.0)
        {
            last_tempo = sync.modified_tempo;
        }
    }

    if (anchors.empty())
    {
        anchors.push_back(common::core::BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0});
        notes.emplace_back("score has no audio sync points; timing uses the base tempo");
    }

    if (anchors.front().measure != 1 || anchors.front().beat != 1)
    {
        // Back-extrapolate the missing lead-in at the first known tempo, clamped at zero so
        // the map never starts before the audio. Whole measures and the first anchor's partial
        // measure subtract separately because the anchor may sit on the terminal downbeat,
        // one past the last real measure.
        const common::core::BeatAnchor& first = anchors.front();
        double seconds = first.seconds;
        const double first_tempo = score.sync_points.empty()
                                       ? score.base_tempo_quarter_bpm
                                       : std::max(1.0, score.sync_points.front().modified_tempo);
        for (int measure = 1; measure < first.measure; ++measure)
        {
            const auto measure_index = static_cast<std::size_t>(measure - 1);
            seconds -= grid.beats_per_measure[measure_index] *
                       secondsPerBeat(first_tempo, grid.denominator[measure_index]);
        }
        if (first.beat > 1)
        {
            const auto first_index = static_cast<std::size_t>(first.measure - 1);
            seconds -=
                (first.beat - 1) * secondsPerBeat(first_tempo, grid.denominator[first_index]);
        }
        if (seconds < 0.0)
        {
            seconds = 0.0;
            notes.emplace_back("score starts before the audio; the lead-in was clamped");
        }
        anchors.insert(
            anchors.begin(), common::core::BeatAnchor{.measure = 1, .beat = 1, .seconds = seconds});
        if (anchors.size() > 1 && anchors[1].seconds <= anchors[0].seconds)
        {
            anchors.erase(anchors.begin() + 1);
        }
    }

    // Terminal anchor on the downbeat after the final bar, extrapolated at the last tempo. A
    // final sync point can land exactly there (a rollover from the last bar's end); it already
    // pins the song's end to the audio, so no extrapolated anchor is added on top of it.
    const int total_measures = static_cast<int>(grid.beats_per_measure.size());
    const int last_sync_measure = anchors.back().measure;
    const int last_sync_beat = anchors.back().beat;
    const double last_sync_seconds = anchors.back().seconds;
    if (last_sync_measure != total_measures + 1)
    {
        double terminal_seconds = last_sync_seconds;
        for (int measure = last_sync_measure; measure <= total_measures; ++measure)
        {
            const auto measure_index = static_cast<std::size_t>(measure - 1);
            const int beats = measure == last_sync_measure
                                  ? grid.beats_per_measure[measure_index] - (last_sync_beat - 1)
                                  : grid.beats_per_measure[measure_index];
            terminal_seconds +=
                beats * secondsPerBeat(std::max(1.0, last_tempo), grid.denominator[measure_index]);
        }
        anchors.push_back(
            common::core::BeatAnchor{
                .measure = total_measures + 1,
                .beat = 1,
                .seconds = std::max(terminal_seconds, last_sync_seconds + 0.001),
            });
    }

    // Warn when audio sync points leave most of the song to constant-tempo extrapolation: those
    // bars start aligned but drift from any recording that is not metronomically steady, which is
    // a source-data limitation the import cannot recover (the sync points simply are not there).
    if (!score.sync_points.empty() && (total_measures - last_sync_measure) * 4 > total_measures)
    {
        notes.emplace_back(
            "audio sync points cover only up to measure " + std::to_string(last_sync_measure) +
            " of " + std::to_string(total_measures) +
            "; later timing is extrapolated at the last tempo and may drift from the recording");
    }

    snapAnchorsToMillisecondGrid(anchors);
    return common::core::TempoMap{std::move(signatures), std::move(anchors)};
}

// Converts a Guitar Pro percent (one decimal at most) into an exact rational of one.
[[nodiscard]] Fraction percentFraction(double percent)
{
    return Fraction{static_cast<int>(std::lround(percent * 10.0)), 1000};
}

// Maps one GP bend onto the chart's [offset, semitones] pairs across the note sustain.
[[nodiscard]] std::vector<BendPoint> buildBendPoints(
    const GpBend& bend, Fraction sustain, std::vector<std::string>& notes)
{
    if (sustain.numerator <= 0)
    {
        notes.emplace_back("dropped a bend on a note without sustain");
        return {};
    }

    struct RawPoint
    {
        double offset_percent;
        double value;
    };
    // The middle value holds between the two middle offsets; when they coincide, the equal-offset
    // merge below collapses the plateau back to a single point.
    const std::array<RawPoint, 4> raw{
        RawPoint{.offset_percent = bend.origin_offset, .value = bend.origin_value},
        RawPoint{.offset_percent = bend.middle_offset1, .value = bend.middle_value},
        RawPoint{.offset_percent = bend.middle_offset2, .value = bend.middle_value},
        RawPoint{.offset_percent = bend.destination_offset, .value = bend.destination_value},
    };

    std::vector<BendPoint> points;
    for (const RawPoint& point : raw)
    {
        const Fraction offset =
            percentFraction(std::clamp(point.offset_percent, 0.0, 100.0)) * sustain;
        // GP bend values are percent of a whole step; the chart stores semitones.
        const double semitones = point.value / 50.0;
        if (!points.empty() && points.back().offset == offset)
        {
            points.back().semitones = semitones;
            continue;
        }
        if (!points.empty() && offset < points.back().offset)
        {
            continue;
        }
        points.push_back(BendPoint{.offset = offset, .semitones = semitones});
    }

    // A flat zero curve carries no information.
    const bool all_zero = std::ranges::all_of(
        points, [](const BendPoint& point) { return std::is_eq(point.semitones <=> 0.0); });
    return all_zero ? std::vector<BendPoint>{} : points;
}

// Classifies a track's part by a heuristic: four strings or a bass-named track become Bass, the
// first non-bass track becomes Lead, and the rest Rhythm. This is a stopgap — Guitar Pro tracks
// carry no Rock Hero part, so a robust import should let the user map each track to a part on
// import rather than guessing. Tracked in docs/plans/todo/gp-track-part-mapping.md.
[[nodiscard]] common::core::Part partForTrack(const GpTrack& track, bool first_track)
{
    std::string lower_name = track.name;
    std::ranges::transform(lower_name, lower_name.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (track.tuning_midi.size() <= 4 || lower_name.find("bass") != std::string::npos)
    {
        return common::core::Part::Bass;
    }

    return first_track ? common::core::Part::Lead : common::core::Part::Rhythm;
}

// Names a part for the human-readable conversion note about the import's part guesses.
[[nodiscard]] const char* partName(common::core::Part part)
{
    switch (part)
    {
        case common::core::Part::Lead:
            return "Lead";
        case common::core::Part::Rhythm:
            return "Rhythm";
        case common::core::Part::Bass:
            return "Bass";
    }
    return "Lead";
}

// Splits a global-beat-axis position back into measure/beat/offset grid fields. Grace leads can
// cross a bar line backward (a grace before a downbeat sounds in the previous bar), so the
// measure is looked up from the position rather than taken from the principal's bar.
[[nodiscard]] GridPosition gridPositionForGlobalBeat(const MeasureGrid& grid, const Fraction global)
{
    const auto after = std::ranges::upper_bound(
        grid.first_global_beat, global, std::ranges::less{}, [](const int beats) {
            return Fraction{beats};
        });
    const std::size_t measure_index =
        after == grid.first_global_beat.begin()
            ? 0
            : static_cast<std::size_t>(std::distance(grid.first_global_beat.begin(), after)) - 1;
    const Fraction in_measure = (global - Fraction{grid.first_global_beat[measure_index]});
    const int whole_beats = in_measure.numerator / in_measure.denominator;
    return GridPosition{
        .measure = static_cast<int>(measure_index) + 1,
        .beat = whole_beats + 1,
        .offset = (in_measure - Fraction{whole_beats}),
    };
}

// Collects the timed note events of one track across bars and voices. Grace beats take no time
// from the bar; each run attaches to the next sounding beat in its voice (the principal). A
// before-beat grace sounds a thirty-second-note lead ahead of the principal; an on-beat grace
// sounds on the principal's position and delays the principal notes on its strings by the same
// lead (Guitar Pro's two grace placements). A lead shrinks to half the available gap when the
// neighboring onset sits closer than the full leads, and graces with no room at all are dropped.
[[nodiscard]] std::vector<NoteEvent> collectEvents(
    const GpTrack& track, const MeasureGrid& grid, std::vector<std::string>& notes)
{
    std::vector<NoteEvent> events;
    int overfull = 0;
    int dropped_graces = 0;

    const auto emit_note = [&events, &grid](
                               const GpNote& source,
                               const bool tremolo,
                               const Fraction global,
                               const Fraction duration,
                               const Fraction notated) {
        const GridPosition position = gridPositionForGlobalBeat(grid, global);
        NoteEvent event;
        event.measure = position.measure;
        event.beat = position.beat;
        event.offset = position.offset;
        event.duration_beats = duration;
        event.global_beat = global;
        event.source = source;
        event.tremolo = tremolo;
        event.notated_beat = notated;
        events.push_back(std::move(event));
    };

    // Grace runs and conflict neighbors persist across bar lines within a voice, keyed by the
    // voice's index in its bar.
    std::map<std::size_t, std::vector<const GpBeat*>> pending_graces_per_voice;
    std::map<std::size_t, Fraction> last_onset_per_voice;
    for (std::size_t bar_index = 0; bar_index < track.bars.size(); ++bar_index)
    {
        const auto measure_index = std::min(bar_index, grid.beats_per_measure.size() - 1);
        const int beats_in_bar = grid.beats_per_measure[measure_index];
        const int denominator = grid.denominator[measure_index];
        // A thirty-second note in this measure's meter, on the signature-beat axis.
        const Fraction full_lead{denominator, 32};

        for (std::size_t voice_index = 0; voice_index < track.bars[bar_index].voices.size();
             ++voice_index)
        {
            std::vector<const GpBeat*>& pending = pending_graces_per_voice[voice_index];
            Fraction position_beats{};
            for (const GpBeat& beat : track.bars[bar_index].voices[voice_index])
            {
                if (beat.grace != GpGracePlacement::None)
                {
                    if (!beat.notes.empty())
                    {
                        pending.push_back(&beat);
                    }
                    continue;
                }

                const Fraction duration_beats = (beat.duration_whole * Fraction{denominator});
                const Fraction onset = position_beats;
                position_beats = position_beats + duration_beats;
                if (beat.notes.empty())
                {
                    // A rest cannot host a grace run: graces ornament a sounding principal.
                    dropped_graces += static_cast<int>(pending.size());
                    pending.clear();
                    continue;
                }
                if (onset >= Fraction{beats_in_bar})
                {
                    ++overfull;
                    dropped_graces += static_cast<int>(pending.size());
                    pending.clear();
                    continue;
                }

                const Fraction principal_global =
                    (Fraction{grid.first_global_beat[measure_index]} + onset);
                Fraction principal_shift{};
                std::vector<int> shifted_strings;
                if (!pending.empty())
                {
                    const auto is_on_beat = [](const GpBeat* grace) {
                        return grace->grace == GpGracePlacement::OnBeat;
                    };
                    const int on_count =
                        static_cast<int>(std::ranges::count_if(pending, is_on_beat));
                    const int before_count = static_cast<int>(pending.size()) - on_count;

                    // Before-beat run: leads stack backward from the principal. The gap floor is
                    // the voice's previous sounding onset, or the song start when none exists.
                    if (before_count > 0)
                    {
                        const auto last = last_onset_per_voice.find(voice_index);
                        const Fraction floor =
                            last != last_onset_per_voice.end() ? last->second : Fraction{0};
                        const Fraction gap = principal_global - floor;
                        const Fraction lead = fitLeadToGap(full_lead, gap, before_count);
                        if (lead.numerator <= 0)
                        {
                            dropped_graces += before_count;
                        }
                        else
                        {
                            int remaining = before_count;
                            for (const GpBeat* grace : pending)
                            {
                                if (is_on_beat(grace))
                                {
                                    continue;
                                }
                                const Fraction back = Fraction{remaining} * lead;
                                const Fraction global = principal_global - back;
                                for (const GpNote& grace_note : grace->notes)
                                {
                                    emit_note(
                                        grace_note, grace->tremolo, global, lead, principal_global);
                                }
                                --remaining;
                            }
                        }
                    }

                    // On-beat run: graces sound on the principal's position and the principal
                    // notes on their strings land one lead later per grace, ends unchanged.
                    if (on_count > 0)
                    {
                        const Fraction lead = fitLeadToGap(full_lead, duration_beats, on_count);
                        if (lead.numerator <= 0)
                        {
                            dropped_graces += on_count;
                        }
                        else
                        {
                            int slot = 0;
                            for (const GpBeat* grace : pending)
                            {
                                if (!is_on_beat(grace))
                                {
                                    continue;
                                }
                                const Fraction forward = Fraction{slot} * lead;
                                const Fraction global = principal_global + forward;
                                for (const GpNote& grace_note : grace->notes)
                                {
                                    emit_note(
                                        grace_note, grace->tremolo, global, lead, principal_global);
                                    shifted_strings.push_back(grace_note.string);
                                }
                                ++slot;
                            }
                            principal_shift = Fraction{on_count} * lead;
                        }
                    }
                    pending.clear();
                }

                for (const GpNote& source : beat.notes)
                {
                    const bool shifted = std::ranges::contains(shifted_strings, source.string);
                    emit_note(
                        source,
                        beat.tremolo,
                        shifted ? (principal_global + principal_shift) : principal_global,
                        shifted ? (duration_beats - principal_shift) : duration_beats,
                        principal_global);
                }
                last_onset_per_voice[voice_index] = principal_global + principal_shift;
            }
        }
    }
    for (const auto& entry : pending_graces_per_voice)
    {
        // A grace run at the end of a track has no principal to attach to.
        dropped_graces += static_cast<int>(entry.second.size());
    }
    if (overfull > 0)
    {
        notes.push_back(std::to_string(overfull) + " beats overflowed their bar and were dropped");
    }
    if (dropped_graces > 0)
    {
        notes.push_back(
            std::to_string(dropped_graces) + " grace-note beats had no room and were dropped");
    }

    std::ranges::stable_sort(events, [](const NoteEvent& lhs, const NoteEvent& rhs) {
        if (lhs.global_beat != rhs.global_beat)
        {
            return lhs.global_beat < rhs.global_beat;
        }
        return lhs.source.string < rhs.source.string;
    });
    return events;
}

// Built notes plus their onset on the global beat axis (needed for tie and slide spans, and by
// the sustain normalization that runs after both).
struct BuiltNote
{
    ChartNote note;
    Fraction global_beat{};
    Fraction end_global_beat{};
    int gp_string{0};
    int slide_flags{0};

    // The beat the source notated this onset at (see NoteEvent::notated_beat). Deliberately NOT
    // updated when resolveSlideIns moves a head onto its lead: the moved head sounds early but
    // stays notated where it was written, so it binds neighboring tails at its sounding beat
    // while never faking a deliberate hold there.
    Fraction notated_beat{};

    // Onset of the tied continuation the slide flags were inherited from, when they were: the
    // glide leaves from the junction, not the merged note's onset (policy rule 15).
    std::optional<Fraction> slide_from_beat;
};

// Reports whether the note carries a technique that lives on its sustain tail. These notes keep
// their tails through the sub-beat drop rule: removing the tail would remove the technique.
[[nodiscard]] bool hasSustainTechnique(const ChartNote& note)
{
    return !note.bend.empty() || !note.slides.empty() || note.slide_out.has_value() ||
           note.vibrato || note.tremolo;
}

// Normalizes imported sustains for chart readability (import policy, user rules 2026-07-21;
// hold semantics refined 2026-07-22). The maintained plain-English spec is "GP chart
// normalization policy" in docs/developer/the-project-lifecycle.md — tweak behavior there
// first, then re-align this code.
//
// 1. A tail is trimmed to end at least the minimum-sustain-distance margin — the shared
//    constant in grid_arithmetic.h, the same margin the editor's duration verb clamps to —
//    before the next BINDING onset on ANY string. Binding follows the notated timeline:
//    events sharing a notated beat (chord members, and a strum's own grace-shifted notes)
//    never bind each other, even when grace leads stagger their sounding onsets. One hold is
//    exempt: a tail ringing strictly past the next binding onset's NOTATED beat — merged from
//    a tie or notated across voices — is a deliberate hold that neither this trim nor the
//    drop rule touches (that ring is exactly what the projections' arpeggio arrival rule
//    reads). An importer-fabricated early onset (a grace lead, a moved slide-in head) binds
//    the tail at its sounding beat but cannot witness a hold: the source never notated an
//    onset there, so a ring past it proves nothing deliberate. Repeated chords trim like
//    everything else: their held reading lives in the merged shape span (rule 11), which is
//    derived from the notated pre-trim ends and already runs through every restrike.
// 2. Trimming never clips a bend or slide payload: the tail floors at the last payload offset,
//    so a slide still reaches its target note (exact adjacency stays legal per 40-Q2-B).
// 3. A note with no sustain-carried technique NOTATED shorter than one beat loses its tail
//    entirely after trimming: Guitar Pro gives every note its full notated duration, and a
//    sub-beat effect-free ring reads as noise in a chart rather than as a deliberate sustain.
//    The comparison reads the notated length, not the trimmed one (user rule 2026-07-28): a
//    note held a full beat or longer in the source keeps its tail even though the margin
//    leaves it slightly shorter than the beat.
//
// The rules are import normalization only — the editor never rewrites spacing the user authored.
void normalizeImportedSustains(
    std::vector<BuiltNote>& built, const MeasureGrid& grid, std::vector<std::string>& notes)
{
    int trimmed = 0;
    int dropped = 0;

    std::size_t group_begin = 0;
    while (group_begin < built.size())
    {
        std::size_t group_end = group_begin + 1;
        while (group_end < built.size() &&
               built[group_end].global_beat == built[group_begin].global_beat)
        {
            ++group_end;
        }

        for (std::size_t index = group_begin; index < group_end; ++index)
        {
            ChartNote& note = built[index].note;
            // The drop rule reads the NOTATED length, captured before the trim (user rule
            // 2026-07-28): a note held a full beat or longer in the source keeps its trimmed
            // tail even though the margin leaves it slightly shorter than the beat.
            const Fraction notated_sustain = note.sustain;
            // The next binding onset (rule 1): the first later event whose NOTATED beat differs
            // from this note's — notationally simultaneous events (chord members, a strum's own
            // grace-shifted notes) never bind. The tail keeps the margin before the binding
            // onset's SOUNDING beat, but the hold exemption reads its NOTATED beat: a grace
            // lead or moved slide-in head sounds inside an earlier ring without the source ever
            // notating an onset there, so it must not turn that ring into a "deliberate hold".
            // Later events can notate earlier than they sound (on-beat-shifted strum members),
            // so the scan runs until no later event can still notate ahead of the minimum seen.
            bool has_binding = false;
            Fraction sounding_gap{};
            std::optional<Fraction> notated_ahead;
            for (std::size_t scan = group_end; scan < built.size(); ++scan)
            {
                if (notated_ahead.has_value() && built[scan].global_beat >= *notated_ahead)
                {
                    break;
                }
                if (built[scan].notated_beat == built[index].notated_beat)
                {
                    continue;
                }
                if (!has_binding)
                {
                    has_binding = true;
                    sounding_gap = built[scan].global_beat - built[index].global_beat;
                }
                if (!notated_ahead.has_value() || built[scan].notated_beat < *notated_ahead)
                {
                    notated_ahead = built[scan].notated_beat;
                }
            }

            // A ring held strictly past the next binding onset's notated beat is a deliberate
            // hold (rule 1).
            if (notated_ahead.has_value() &&
                (*notated_ahead - built[index].global_beat) < note.sustain)
            {
                continue;
            }
            if (note.sustain.numerator > 0 && has_binding)
            {
                const Fraction limit = sounding_gap - sustainMarginAt(grid, note.position);
                if (limit < note.sustain)
                {
                    Fraction target = limit.numerator < 0 ? Fraction{} : limit;
                    if (!note.bend.empty() && target < note.bend.back().offset)
                    {
                        target = note.bend.back().offset;
                    }
                    if (!note.slides.empty() && target < note.slides.back().offset)
                    {
                        target = note.slides.back().offset;
                    }
                    // The unpitched slide-out is NOT a protected payload (user rule 2026-07-28):
                    // its end is gesture geometry derived from the notated duration, not a
                    // musical event, so it trims back with the tail to respect the margin. The
                    // trimmed end must stay strictly positive and strictly after the last
                    // waypoint (the model's ascending-payload invariant); a crowding that would
                    // crush it keeps its end instead — the protected-adjacency fallback the
                    // other rules use.
                    if (note.slide_out.has_value() && target < note.slide_out->offset)
                    {
                        const bool crushed =
                            target.numerator <= 0 ||
                            (!note.slides.empty() && target <= note.slides.back().offset);
                        if (crushed)
                        {
                            target = note.slide_out->offset;
                        }
                        else
                        {
                            note.slide_out->offset = target;
                        }
                    }
                    if (target < note.sustain)
                    {
                        note.sustain = target;
                        ++trimmed;
                    }
                }
            }
            if (note.sustain.numerator > 0 && !hasSustainTechnique(note) &&
                notated_sustain < Fraction{1})
            {
                note.sustain = Fraction{};
                ++dropped;
            }
        }
        group_begin = group_end;
    }
    if (trimmed > 0)
    {
        notes.push_back(
            std::to_string(trimmed) + " sustains were trimmed to the minimum sustain distance");
    }
    if (dropped > 0)
    {
        notes.push_back(
            std::to_string(dropped) + " sub-beat sustains without techniques were dropped");
    }
}

// Derives chord templates and hand-posture spans from the note stream (import policy, user
// request 2026-07-21). Guitar Pro scores in practice carry no handshape data (corpus chord
// collections are empty), so any onset striking two or more strings becomes a chord posture,
// deduplicated into the template table, and consecutive onsets holding the same posture merge
// into one shape span covering the strums' notated (pre-trim) durations — the grouping the tab
// renders as a chord box over repeated strums. Tap-only onsets are transparent to the whole
// derivation (user rule 2026-07-28): taps are the tapping hand, so they neither form postures
// nor close held spans, letting a ringing chord's span cover the taps above it. ANY articulation difference is a new chord (user
// rule 2026-07-21): span continuity compares each string's whole note with only its position and
// duration neutralized, so attack (hammer/pull/tap/slap/pop), muting, harmonics, vibrato,
// tremolo, accent, bends, and slides — and any technique added to ChartNote later — all split
// the span, while strum durations never do. The template table stays deduplicated by frets
// alone (the hand posture is identical; techniques render on the notes). A note still ringing
// through a chord's onset (tie-held from before, not re-struck) joins the posture on its string
// (policy rule 12, user rule 2026-07-22); the projections' shared arrival rule then renders the
// partly-struck span as an arpeggio, while fully-strummed spans stay chord boxes — no other
// arpeggio grouping is derived (broken-chord grouping needs the corpus-informed pass). A span
// closed by a following event trims to the minimum-sustain-distance margin before it — the same
// margin every other element keeps (policy rule 12a, user rule 2026-07-23). Derived templates
// are unnamed and unfingered. The maintained plain-English spec is "GP chart normalization
// policy" in docs/developer/the-project-lifecycle.md.
void deriveChordShapes(const std::vector<BuiltNote>& built, const MeasureGrid& grid, Chart& chart)
{
    const std::size_t string_count = chart.tuning.strings.size();
    std::map<std::vector<std::optional<int>>, std::size_t> template_indices;

    // One struck string's contribution to a span's articulation identity: the whole note with
    // its position and duration neutralized, so ChartNote equality decides "same chord" and new
    // technique fields can never silently drop out of the comparison.
    using StringArticulation = std::optional<ChartNote>;

    struct OpenSpan
    {
        std::size_t chord{0};
        std::vector<StringArticulation> articulation;
        GridPosition position;
        Fraction start_beat{};
        Fraction end_beat{};
        Fraction last_strum_beat{};
    };
    std::optional<OpenSpan> open;
    // The closing onset reduced by the minimum-sustain-distance margin (at the closing onset's
    // measure) — where a span it closes must end.
    const auto margin_limit = [&grid](const BuiltNote& closing) {
        return closing.global_beat - sustainMarginAt(grid, closing.note.position);
    };
    // Closes the held span. A span closed by a following event trims to the margin before it
    // (policy rule 12a — spans keep the same minimum sustain distance as every other element,
    // user rule 2026-07-23), floored at the last strum so the box always reaches its final
    // restrike. A span that would lose all length (a single strum crowded closer than the
    // margin) falls back to exact adjacency, mirroring the sustain rules' protected-adjacency
    // precedent — chart validation rejects zero-length spans.
    const auto close_span = [&chart, &open](
                                const std::optional<Fraction> closing_limit,
                                const std::optional<Fraction>
                                    closing_beat) {
        if (open.has_value())
        {
            Fraction end = open->end_beat;
            if (closing_limit.has_value() && *closing_limit < end)
            {
                end = std::max(*closing_limit, open->last_strum_beat);
            }
            if (!(open->start_beat < end) && closing_beat.has_value())
            {
                // Exact adjacency: the crowded span ends at the earlier of its notated ring and
                // the closing onset — both sit strictly after the span start, so the span keeps
                // positive length even when the closer lands exactly on the notated end (a
                // dense run of short strums).
                end = std::min(open->end_beat, *closing_beat);
            }
            chart.shapes.push_back(
                ChartShape{
                    .position = open->position,
                    .sustain = end - open->start_beat,
                    .chord = open->chord,
                });
            open.reset();
        }
    };

    // The last note sounded per string, for the ring-through rule (policy rule 12): a note
    // whose notated tail crosses a chord's onset on an un-struck string is still sounding, so
    // its held fret joins the derived posture — and the projections' arrival rule renders the
    // partly-struck span as an arpeggio.
    std::vector<const BuiltNote*> ringing(string_count, nullptr);

    std::size_t index = 0;
    while (index < built.size())
    {
        std::size_t onset_end = index;
        Fraction notated_end{};
        std::vector<StringArticulation> articulation(string_count);
        std::size_t struck = 0;
        while (onset_end < built.size() && built[onset_end].global_beat == built[index].global_beat)
        {
            const ChartNote& note = built[onset_end].note;
            // Tap-attack notes are invisible to span derivation (user rule 2026-07-28): the
            // taps belong to the tapping hand, not the fretting posture, so they join no
            // posture and extend no ring, and a mixed onset — a fretting-hand note struck
            // under simultaneous right-hand taps — is judged by its non-tap members alone.
            if (note.attack != NoteAttack::Tap)
            {
                if (const auto string_index = static_cast<std::size_t>(note.string - 1);
                    string_index < string_count)
                {
                    ChartNote key = note;
                    key.position = GridPosition{};
                    key.sustain = Fraction{};
                    articulation[string_index] = std::move(key);
                    ++struck;
                }
                if (notated_end < built[onset_end].end_global_beat)
                {
                    notated_end = built[onset_end].end_global_beat;
                }
            }
            ++onset_end;
        }
        if (struck == 0)
        {
            // Tap-only onsets are transparent: they neither form a chord posture nor end a held
            // one. A chord whose notated ring extends under the taps keeps its span, which the
            // projections' arrival rule then renders as a held arpeggio — the corpus's
            // held-shape-under-tapping case. A short-ringing chord is unaffected: its span
            // still ends at its own notated duration, before the taps.
        }
        else if (struck >= 2)
        {
            // Ring-through strings join the posture (they never count as struck): the held
            // note's articulation folds in so span merging still compares whole notes.
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                const BuiltNote* const ring = ringing[string_index];
                if (!articulation[string_index].has_value() && ring != nullptr &&
                    built[index].global_beat < ring->end_global_beat)
                {
                    ChartNote key = ring->note;
                    key.position = GridPosition{};
                    key.sustain = Fraction{};
                    articulation[string_index] = std::move(key);
                }
            }
            std::vector<std::optional<int>> posture(string_count);
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                const StringArticulation& slot = articulation[string_index];
                if (slot.has_value())
                {
                    posture[string_index] = slot->fret;
                }
            }
            const auto [entry, inserted] =
                template_indices.try_emplace(posture, chart.templates.size());
            if (inserted)
            {
                chart.templates.push_back(
                    ChordTemplate{
                        .name = {},
                        .frets = std::move(posture),
                        .fingers = std::vector<std::optional<int>>(string_count),
                    });
            }
            if (open.has_value() && open->articulation == articulation)
            {
                if (open->end_beat < notated_end)
                {
                    open->end_beat = notated_end;
                }
                open->last_strum_beat = built[index].global_beat;
            }
            else
            {
                close_span(margin_limit(built[index]), built[index].global_beat);
                open = OpenSpan{
                    .chord = entry->second,
                    .articulation = std::move(articulation),
                    .position = built[index].note.position,
                    .start_beat = built[index].global_beat,
                    .end_beat = notated_end,
                    .last_strum_beat = built[index].global_beat,
                };
            }
        }
        else
        {
            // Any intervening non-chord onset ends the held posture.
            close_span(margin_limit(built[index]), built[index].global_beat);
        }
        // This onset's non-tap notes become the ring candidates for later onsets (updated after
        // use: a note starting at an onset is struck there, not ringing through it). Taps stay
        // invisible here too — a ringing tap never folds into a later posture.
        for (std::size_t member = index; member < onset_end; ++member)
        {
            if (const auto string_index = static_cast<std::size_t>(built[member].note.string - 1);
                string_index < string_count && built[member].note.attack != NoteAttack::Tap)
            {
                ringing[string_index] = &built[member];
            }
        }
        index = onset_end;
    }
    close_span(std::nullopt, std::nullopt);
}

// A silence long enough to read as a phrase break: the hand re-anchors across it. 0.8s is the
// corpus sweet spot (source 4100-arrangement study, 2026-07-28) — it holds the authored move rate
// (~13.2 anchors per 100 notes) while lifting exact anchor-fret agreement from 59% to 72%.
constexpr double g_fhp_phrase_rest_seconds = 0.8;

// Generates the fret-hand position track, corpus-derived from the source study
// (docs/plans/todo/fhp-corpus-derived-generation.md, 4100 authored arrangements). The hand covers
// a [fret, fret+width-1] window (width four unless one onset spans wider), open strings never
// constrain it, and it tracks the LEFT hand. Two rules the earlier greedy walk could not capture:
//   1. A TAPPED note is not a coverage event. Two-hand taps sit a median seven frets above the
//      fretting hand, so the anchor stays on the fretted / left-hand notes and any held chord
//      shape while the tap floats above the window; the highway camera frames the tap separately.
//   2. The hand RE-ANCHORS at musical boundaries — section starts (phrase_boundary_beats) and
//      rests >= g_fhp_phrase_rest_seconds — biased to the phrase's floor fret, not only when a
//      note leaves the window (only ~35% of authored moves are forced). Within a segment it moves
//      minimally when forced and drags with pitched slides.
// Scored against the corpus this reaches 72.5% exact anchor-fret agreement at the authored move
// rate. The maintained plain-English spec is "GP chart normalization policy" in
// docs/developer/the-project-lifecycle.md — tweak behavior there first, then re-align this code.
[[nodiscard]] std::vector<common::core::FretHandPosition> generateFretHandPositions(
    const std::vector<BuiltNote>& built, const common::core::TempoMap& tempo_map,
    const std::vector<Fraction>& phrase_boundary_beats)
{
    // One instant the fret hand must cover: the fretted extent of an onset group, or a pitched
    // slide waypoint mid-sustain. A nonzero shift marks a slide waypoint carrying its fret delta
    // from the glide's source, which drags the anchor by that delta (rule 9) instead of being
    // fit like a struck onset.
    struct CoverageEvent
    {
        Fraction global_beat{};
        GridPosition position;
        int min_fret{0};
        int max_fret{0};
        int shift{0};
    };
    std::vector<CoverageEvent> events;
    std::size_t index = 0;
    while (index < built.size())
    {
        CoverageEvent onset{
            .global_beat = built[index].global_beat,
            .position = built[index].note.position,
        };
        std::size_t onset_end = index;
        while (onset_end < built.size() && built[onset_end].global_beat == built[index].global_beat)
        {
            const ChartNote& note = built[onset_end].note;
            // Tap exclusion: a tapped note floats above the window and never anchors the hand.
            if (note.attack != NoteAttack::Tap)
            {
                if (note.fret > 0)
                {
                    onset.min_fret =
                        onset.min_fret == 0 ? note.fret : std::min(onset.min_fret, note.fret);
                    onset.max_fret = std::max(onset.max_fret, note.fret);
                }
                int slide_source = note.fret;
                for (const SlideWaypoint& waypoint : note.slides)
                {
                    if (waypoint.fret <= 0)
                    {
                        continue;
                    }
                    events.push_back(
                        CoverageEvent{
                            .global_beat = built[onset_end].global_beat + waypoint.offset,
                            .position = common::core::advanceGridPosition(
                                tempo_map, note.position, waypoint.offset),
                            .min_fret = waypoint.fret,
                            .max_fret = waypoint.fret,
                            .shift = slide_source > 0 ? waypoint.fret - slide_source : 0,
                        });
                    slide_source = waypoint.fret;
                }
            }
            ++onset_end;
        }
        if (onset.min_fret > 0)
        {
            events.push_back(onset);
        }
        index = onset_end;
    }

    // Waypoint events land mid-sustain, out of onset order, so the stream re-sorts before
    // same-instant events merge into one coverage demand (a waypoint coinciding with an onset
    // is one instant the hand covers once).
    std::ranges::stable_sort(events, [](const CoverageEvent& lhs, const CoverageEvent& rhs) {
        return lhs.global_beat < rhs.global_beat;
    });
    std::vector<CoverageEvent> merged;
    for (const CoverageEvent& event : events)
    {
        if (!merged.empty() && merged.back().global_beat == event.global_beat)
        {
            merged.back().min_fret = std::min(merged.back().min_fret, event.min_fret);
            merged.back().max_fret = std::max(merged.back().max_fret, event.max_fret);
            // Coinciding shifts keep the first waypoint's delta (degenerate input; stable sort
            // preserves emission order, so "first" is well defined).
            if (merged.back().shift == 0)
            {
                merged.back().shift = event.shift;
            }
        }
        else
        {
            merged.push_back(event);
        }
    }

    std::vector<common::core::FretHandPosition> positions;
    int anchor = 0;
    int width = 4;
    bool have_anchor = false;
    double previous_seconds = 0.0;
    Fraction previous_beat{};
    std::size_t phrase_index = 0;
    for (const CoverageEvent& event : merged)
    {
        const double seconds = tempo_map.secondsAtGlobalBeatPosition(event.global_beat.toDouble());
        // A new segment begins at the first event, across a long rest, or at a section start that
        // falls strictly after the previous event.
        bool boundary = !have_anchor || seconds - previous_seconds >= g_fhp_phrase_rest_seconds;
        while (phrase_index < phrase_boundary_beats.size() &&
               phrase_boundary_beats[phrase_index] <= previous_beat)
        {
            ++phrase_index;
        }
        if (have_anchor && phrase_index < phrase_boundary_beats.size() &&
            phrase_boundary_beats[phrase_index] <= event.global_beat)
        {
            boundary = true;
        }
        previous_seconds = seconds;
        previous_beat = event.global_beat;

        const bool reanchor = boundary || !have_anchor;
        const bool covered = have_anchor && !reanchor && event.min_fret >= anchor &&
                             event.max_fret <= anchor + width - 1;
        if (event.shift == 0 && covered)
        {
            continue;
        }
        const int next_width = std::max(4, event.max_fret - event.min_fret + 1);
        const int lowest_anchor = std::max(1, event.max_fret - next_width + 1);
        // At a boundary the hand re-places biased to the phrase's floor (the lowest fretted note);
        // otherwise it drags from the current anchor by the slide delta and clamps into range.
        const int next_anchor =
            reanchor ? std::clamp(event.min_fret, lowest_anchor, event.min_fret)
                     : std::clamp(anchor + event.shift, lowest_anchor, event.min_fret);
        if (have_anchor && next_anchor == anchor && next_width == width)
        {
            continue; // re-anchoring landed on the same window; nothing visible changed
        }
        positions.push_back(
            common::core::FretHandPosition{
                .position = event.position,
                .fret = next_anchor,
                .width = next_width,
            });
        anchor = next_anchor;
        width = next_width;
        have_anchor = true;
    }
    return positions;
}

// Resolves bare slide-in flags (16 from below, 32 from above) into ordinary slides — no new
// notation (user rule 2026-07-27): the note's head moves onto the lead at the derived start
// fret, an ordinary pitched waypoint glides to the notated fret at the notated position, and
// the landing keeps no head of its own, exactly like a legato junction. Guitar Pro gives the
// gesture no start fret, so the fret-hand positions supply it: the slide is the hand traveling
// into the placement that arrives at the note, departing from the same finger slot in the
// preceding placement — start = landing fret + (preceding anchor − landing anchor). A grace
// note sliding into its principal is the explicit-fret notation and never reaches here: it
// resolves through the ordinary slide chain. When the hand does not move at the note, or the
// placement delta contradicts the flag's direction, the flag wins with a two-fret start in its
// direction. Runs after fret-hand generation — by construction the derived start fret lies
// inside the preceding placement, so moved heads never perturb the window walk, and the
// placement at the notated position sits exactly on the glide's landing waypoint — and before
// the sustain policy, so the transformed note is a slide when the trim rules run: a slide-in
// into a held landing keeps its hold like any notated slide (user rule 2026-07-28). The lead
// is the shared minimum-sustain-distance margin, halved when the previous onset on the string
// sits closer.
void resolveSlideIns(
    std::vector<BuiltNote>& built, const std::vector<common::core::FretHandPosition>& placements,
    const MeasureGrid& grid, std::vector<std::string>& notes)
{
    int unplaceable = 0;
    bool moved = false;
    for (std::size_t index = 0; index < built.size(); ++index)
    {
        BuiltNote& entry = built[index];
        if ((entry.slide_flags & (16 | 32)) == 0)
        {
            continue;
        }
        ChartNote& note = entry.note;
        const bool from_below = (entry.slide_flags & 16) != 0;
        if (note.fret < 1)
        {
            // An open string cannot be slid into.
            ++unplaceable;
            continue;
        }

        int start = from_below ? note.fret - 2 : note.fret + 2;
        const auto after = std::ranges::upper_bound(
            placements,
            note.position,
            std::ranges::less{},
            &common::core::FretHandPosition::position);
        if (after != placements.begin())
        {
            const auto landing = after - 1;
            if (landing != placements.begin() && landing->position == note.position)
            {
                const int delta = (landing - 1)->fret - landing->fret;
                if (delta != 0 && (delta < 0) == from_below)
                {
                    start = note.fret + delta;
                }
            }
        }
        start = std::clamp(start, 1, common::core::g_max_fret);
        if (start == note.fret)
        {
            // Sliding into fret 1 from below (or the last fret from above): no start exists.
            ++unplaceable;
            continue;
        }

        // The song start bounds the lead when no earlier onset on the string does.
        Fraction gap = entry.global_beat;
        for (std::size_t previous = index; previous-- > 0;)
        {
            if (built[previous].gp_string == entry.gp_string)
            {
                gap = entry.global_beat - built[previous].global_beat;
                break;
            }
        }
        const Fraction lead = fitLeadToGap(sustainMarginAt(grid, note.position), gap, 1);
        if (lead.numerator <= 0)
        {
            ++unplaceable;
            continue;
        }

        // The head moves back onto the lead; every payload offset is onset-relative and
        // shifts with it, and the notated fret becomes the first glide waypoint. The sustain
        // end stays where the notated note ended.
        for (BendPoint& point : note.bend)
        {
            point.offset = point.offset + lead;
        }
        for (SlideWaypoint& waypoint : note.slides)
        {
            waypoint.offset = waypoint.offset + lead;
        }
        if (note.slide_out.has_value())
        {
            note.slide_out->offset = note.slide_out->offset + lead;
        }
        note.slides.insert(note.slides.begin(), SlideWaypoint{.offset = lead, .fret = note.fret});
        note.fret = start;
        note.sustain = note.sustain + lead;
        entry.global_beat = entry.global_beat - lead;
        note.position = gridPositionForGlobalBeat(grid, entry.global_beat);
        moved = true;
    }
    if (moved)
    {
        // A moved head can pass a neighboring onset on another string; the note stream stays
        // sorted by (position, string) for the chart invariant.
        std::ranges::stable_sort(built, [](const BuiltNote& lhs, const BuiltNote& rhs) {
            if (lhs.global_beat != rhs.global_beat)
            {
                return lhs.global_beat < rhs.global_beat;
            }
            return lhs.note.string < rhs.note.string;
        });
    }
    if (unplaceable > 0)
    {
        notes.push_back(
            std::to_string(unplaceable) +
            " slide-ins had no representable start and were left plain");
    }
}

// Builds one track's chart: tie merging, technique mapping, bends, slide resolution, sustain
// normalization, and fret-hand position generation. The tempo map places mid-sustain
// slide-waypoint positions on the musical grid.
[[nodiscard]] Chart buildChart(
    const GpTrack& track, const MeasureGrid& grid, const common::core::TempoMap& tempo_map,
    const std::vector<Fraction>& phrase_boundary_beats, std::vector<std::string>& notes)
{
    Chart chart;
    for (const int midi : track.tuning_midi)
    {
        chart.tuning.strings.push_back(midiNoteName(midi));
    }
    chart.tuning.capo = track.capo;

    const std::vector<NoteEvent> events = collectEvents(track, grid, notes);

    std::vector<BuiltNote> built;
    std::map<int, std::size_t> open_note_per_string;
    std::map<int, int> previous_fret_per_string;
    int dropped_duplicates = 0;

    for (const NoteEvent& event : events)
    {
        const GpNote& source = event.source;
        const Fraction event_end = event.global_beat + event.duration_beats;

        if (source.tie_destination)
        {
            // Continuations extend the open note on the string instead of creating an onset.
            const auto open = open_note_per_string.find(source.string);
            if (open != open_note_per_string.end())
            {
                BuiltNote& origin = built[open->second];
                if (event_end > origin.end_global_beat)
                {
                    origin.end_global_beat = event_end;
                    origin.note.sustain = origin.end_global_beat - origin.global_beat;
                }
                origin.note.vibrato = origin.note.vibrato || source.vibrato;
                origin.note.tremolo = origin.note.tremolo || event.tremolo;
                if (source.bend.has_value())
                {
                    const Fraction base = event.global_beat - origin.global_beat;
                    for (BendPoint point :
                         buildBendPoints(*source.bend, event.duration_beats, notes))
                    {
                        point.offset = point.offset + base;
                        if (origin.note.bend.empty() ||
                            point.offset > origin.note.bend.back().offset)
                        {
                            origin.note.bend.push_back(point);
                        }
                    }
                }
                // A slide notated on the tied continuation belongs to the merged note (policy
                // rule 15): the flags fold in rather than vanishing with the merged onset, and
                // the continuation's own onset marks where the glide leaves from.
                origin.slide_flags |= source.slide_flags;
                if (source.slide_flags != 0)
                {
                    origin.slide_from_beat = event.global_beat;
                }
                if (!source.tie_origin)
                {
                    open_note_per_string.erase(open);
                }
                previous_fret_per_string[source.string] = source.fret;
                continue;
            }
        }

        BuiltNote entry;
        entry.global_beat = event.global_beat;
        entry.end_global_beat = event_end;
        entry.gp_string = source.string;
        entry.slide_flags = source.slide_flags;
        entry.notated_beat = event.notated_beat;

        ChartNote& note = entry.note;
        note.position =
            GridPosition{.measure = event.measure, .beat = event.beat, .offset = event.offset};
        note.string = source.string + 1;
        note.fret = source.fret;
        note.sustain = event.duration_beats;
        note.vibrato = source.vibrato;
        note.tremolo = event.tremolo;
        note.accent = source.accent;

        if (source.left_hand_tapped)
        {
            // A left-hand tap is the fretting hand hammering the note from nowhere (no pick
            // stroke), which the hammer-on states accurately — no separate notation (user rule
            // 2026-07-28). Always a hammer, never a pull: nothing is released to sound it. The
            // Hammer attack also gives the right downstream behavior automatically — the note
            // anchors the fret hand, closes chord spans, and never floats above the window,
            // all of which are Tap-attack special cases. Checked before the generic tap: a
            // note carrying both marks is a left-hand tap, the more specific articulation
            // (user rule 2026-07-28).
            note.attack = NoteAttack::Hammer;
        }
        else if (source.tapped)
        {
            note.attack = NoteAttack::Tap;
        }
        else if (source.hopo_destination)
        {
            const auto previous = previous_fret_per_string.find(source.string);
            note.attack =
                previous != previous_fret_per_string.end() && source.fret < previous->second
                    ? NoteAttack::Pull
                    : NoteAttack::Hammer;
        }

        if (source.full_mute)
        {
            note.mute = NoteMute::Full;
        }
        else if (source.palm_mute)
        {
            note.mute = NoteMute::Palm;
        }

        if (!source.harmonic_type.empty())
        {
            note.harmonic =
                source.harmonic_type == "Pinch" ? NoteHarmonic::Pinch : NoteHarmonic::Natural;
            if (source.harmonic_fret.has_value() &&
                std::abs(*source.harmonic_fret - source.fret) > 1e-6)
            {
                note.touch = source.harmonic_fret;
            }
        }

        if (source.bend.has_value())
        {
            note.bend = buildBendPoints(*source.bend, note.sustain, notes);
        }

        // Duplicate onsets (two voices striking one string together) keep the first note.
        if (!built.empty())
        {
            const BuiltNote& previous = built.back();
            if (previous.global_beat == entry.global_beat && previous.note.string == note.string)
            {
                ++dropped_duplicates;
                continue;
            }
        }

        if (source.tie_origin)
        {
            open_note_per_string[source.string] = built.size();
        }
        else
        {
            open_note_per_string.erase(source.string);
        }
        previous_fret_per_string[source.string] = source.fret;
        built.push_back(std::move(entry));
    }

    // Slides resolve against the next onset on the same string, so they run after every onset
    // exists. A shift slide (flag 1) glides toward a re-picked target that keeps its own onset
    // and head. A legato slide (flag 2) is a continuation of the same note (user rule
    // 2026-07-21): the target is not re-picked, so it folds into the origin as a pitched
    // waypoint at the junction — the sustain extends through the target's notated end, its
    // sustain-carried techniques fold in, and its own onward slide continues the chain until a
    // shift, a slide-out, or the chain's end stops it. Slide-outs trail off unpitched.
    std::vector<bool> merged_away(built.size(), false);
    for (std::size_t index = 0; index < built.size(); ++index)
    {
        BuiltNote& entry = built[index];
        if (merged_away[index] || entry.slide_flags == 0)
        {
            continue;
        }
        ChartNote& note = entry.note;

        // Flags inherited from a tied continuation glide from the junction, not the merged
        // note's onset: a hold waypoint pins the pitch until the sliding segment begins (the
        // tied 6 holds through its chord, then slides — policy rule 15).
        if (entry.slide_from_beat.has_value())
        {
            const Fraction hold_offset = *entry.slide_from_beat - entry.global_beat;
            if (hold_offset.numerator > 0)
            {
                note.slides.push_back(SlideWaypoint{.offset = hold_offset, .fret = note.fret});
            }
        }

        int flags = entry.slide_flags;
        int glide_fret = note.fret;
        std::size_t search_from = index;
        while ((flags & (1 | 2)) != 0)
        {
            const BuiltNote* next = nullptr;
            std::size_t next_index = 0;
            for (std::size_t follower = search_from + 1; follower < built.size(); ++follower)
            {
                if (built[follower].gp_string == entry.gp_string && !merged_away[follower])
                {
                    next = &built[follower];
                    next_index = follower;
                    break;
                }
            }
            if (next == nullptr)
            {
                // No landing note exists; the glide degrades to an unpitched slide-out.
                flags |= 4;
                break;
            }
            const Fraction gap = next->global_beat - entry.global_beat;
            if ((flags & 2) != 0 && (flags & 1) == 0)
            {
                // Legato: the landing continues this note. Waypoint at the junction, sustain
                // through the target's notated end, techniques folded, chain continued.
                note.slides.push_back(SlideWaypoint{.offset = gap, .fret = next->note.fret});
                if (entry.end_global_beat < next->end_global_beat)
                {
                    entry.end_global_beat = next->end_global_beat;
                }
                note.sustain = entry.end_global_beat - entry.global_beat;
                note.vibrato = note.vibrato || next->note.vibrato;
                note.tremolo = note.tremolo || next->note.tremolo;
                for (BendPoint point : next->note.bend)
                {
                    point.offset = point.offset + gap;
                    if (note.bend.empty() || point.offset > note.bend.back().offset)
                    {
                        note.bend.push_back(point);
                    }
                }
                merged_away[next_index] = true;
                glide_fret = next->note.fret;
                flags = built[next_index].slide_flags;
                search_from = next_index;
                continue;
            }

            // Shift: an ordinary pitched waypoint glides to the re-picked landing's fret,
            // ending the minimum-sustain-distance margin before the landing's onset like any
            // trimmed tail (policy rule 13); the landing keeps its own onset and head. The
            // sustain ends at the glide end, floored at any payload the tie merge folded past
            // it and kept strictly after the last chain waypoint (a degenerate gap glides
            // through half of it instead).
            Fraction window = gap - sustainMarginAt(grid, note.position);
            if (window.numerator <= 0)
            {
                window = gap * Fraction{1, 2};
            }
            if (!note.bend.empty() && window < note.bend.back().offset)
            {
                window = note.bend.back().offset;
            }
            window = keptStrictlyAfterLastWaypoint(note, window);
            note.slides.push_back(SlideWaypoint{.offset = window, .fret = next->note.fret});
            note.sustain = window;
            flags = 0;
            break;
        }

        if ((flags & (4 | 8)) != 0)
        {
            const bool upward = (flags & 8) != 0;
            const int target = upward ? std::min(glide_fret + 4, common::core::g_max_fret)
                                      : std::max(glide_fret - 4, 0);
            // The slide-out ends at the sustain end, strictly after any chain waypoint so the
            // payload stays ascending.
            Fraction window = keptStrictlyAfterLastWaypoint(note, note.sustain);
            if (window.numerator <= 0)
            {
                window = g_minimum_slide_window;
            }
            if (note.sustain < window)
            {
                note.sustain = window;
            }
            note.slide_out = common::core::SlideOut{.offset = window, .fret = target};
        }
    }

    // Legato landings are no longer onsets; drop them before sustain normalization, chord
    // derivation, and fret-hand generation see the stream.
    std::size_t write_index = 0;
    for (std::size_t index = 0; index < built.size(); ++index)
    {
        if (!merged_away[index])
        {
            if (write_index != index)
            {
                built[write_index] = std::move(built[index]);
            }
            ++write_index;
        }
    }
    built.resize(write_index);

    if (dropped_duplicates > 0)
    {
        notes.push_back(
            std::to_string(dropped_duplicates) + " duplicate simultaneous notes were dropped");
    }

    // The generator reads onsets and waypoint positions only — never sustains — so it can run
    // before the sustain policy. Slide-in resolution needs the placements and must transform
    // its notes into ordinary slides before normalization decides which tails a technique
    // protects: a slide-in into a held landing keeps its hold (user rule 2026-07-28), trimmed
    // like any tail but never dropped as effect-free.
    chart.fret_hand_positions = generateFretHandPositions(built, tempo_map, phrase_boundary_beats);
    if (!chart.fret_hand_positions.empty())
    {
        notes.push_back(
            "generated " + std::to_string(chart.fret_hand_positions.size()) +
            " fret-hand positions (phrase-aware; verify)");
    }
    resolveSlideIns(built, chart.fret_hand_positions, grid, notes);

    // Runs after slide and slide-in resolution so slide-extended tails carry their payloads
    // into the trim's payload floor.
    normalizeImportedSustains(built, grid, notes);

    // Shapes read the notated (pre-trim) note ends, so this runs on the built entries before
    // their notes move into the chart.
    deriveChordShapes(built, grid, chart);
    if (!chart.shapes.empty())
    {
        notes.push_back(
            "derived " + std::to_string(chart.shapes.size()) + " chord spans (" +
            std::to_string(chart.templates.size()) + " postures)");
    }

    chart.notes.reserve(built.size());
    for (BuiltNote& entry : built)
    {
        chart.notes.push_back(std::move(entry.note));
    }

    return chart;
}

} // namespace

std::expected<GpBuiltSong, SongImportError> buildGpSong(const GpScore& score)
{
    if (score.master_bars.empty() || score.tracks.empty())
    {
        return std::unexpected{SongImportError{
            SongImportErrorCode::InvalidImportedSong,
            "score has no bars or no tracks",
        }};
    }

    GpBuiltSong song;
    song.metadata.title = score.title;
    song.metadata.artist = score.artist;
    song.metadata.album = score.album;

    const MeasureGrid grid = makeMeasureGrid(score);
    song.tempo_map = buildTempoMap(score, grid, song.notes);

    // Section markers live on the master bars shared by every track, so they build once at the
    // song level rather than being duplicated into each track's chart. Their global-beat positions
    // double as phrase boundaries for the phrase-aware fret-hand generator (ascending by
    // construction, since master bars are in order).
    std::vector<Fraction> phrase_boundary_beats;
    for (std::size_t measure = 0; measure < score.master_bars.size(); ++measure)
    {
        if (!score.master_bars[measure].section.empty())
        {
            song.sections.push_back(
                common::core::SongSection{
                    .position = GridPosition{.measure = static_cast<int>(measure) + 1, .beat = 1},
                    .name = score.master_bars[measure].section,
                });
            phrase_boundary_beats.push_back(Fraction{grid.first_global_beat[measure]});
        }
    }

    int whammy_beats = 0;
    for (const GpTrack& track : score.tracks)
    {
        for (const GpBar& bar : track.bars)
        {
            for (const std::vector<GpBeat>& voice : bar.voices)
            {
                for (const GpBeat& beat : voice)
                {
                    whammy_beats += beat.whammy ? 1 : 0;
                }
            }
        }
    }
    if (whammy_beats > 0)
    {
        song.notes.push_back(
            std::to_string(whammy_beats) +
            " whammy-bar beats were imported without their bar dives");
    }

    bool seen_non_bass = false;
    std::string part_guesses;
    for (const GpTrack& track : score.tracks)
    {
        GpBuiltArrangement arrangement;
        arrangement.part = partForTrack(track, !seen_non_bass);
        if (arrangement.part != common::core::Part::Bass)
        {
            seen_non_bass = true;
        }
        arrangement.chart =
            buildChart(track, grid, song.tempo_map, phrase_boundary_beats, song.notes);

        if (auto validation = common::core::validateChartRules(arrangement.chart, song.tempo_map);
            !validation.has_value())
        {
            return std::unexpected{SongImportError{
                SongImportErrorCode::InvalidImportedSong,
                "imported chart for track \"" + track.name +
                    "\" violates chart rules: " + validation.error().message,
            }};
        }

        part_guesses +=
            (part_guesses.empty() ? "" : ", ") + track.name + " -> " + partName(arrangement.part);
        song.arrangements.push_back(std::move(arrangement));
    }

    // The track-to-part mapping is a heuristic guess (see partForTrack); surface it so the user
    // can spot and correct a misfiled track. Tracked in docs/plans/todo/gp-track-part-mapping.md.
    song.notes.push_back("assigned parts by track order and name (verify): " + part_guesses);

    return song;
}

} // namespace rock_hero::editor::core
