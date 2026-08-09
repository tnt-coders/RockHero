#include "project/gp_chart_builder.h"

#include "chart/pick_slide_defaults.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <compare>
#include <cstddef>
#include <functional>
#include <iterator>
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

// The minimum gesture window and the corpus-derived default scrape live in the shared seam so
// import and the editor's attack verb synthesize identical defaults (pick_slide_defaults.h).

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

// A slide gesture's fret travel never shrinks below two frets — the minimum that reads as a
// slide. Widens an agreeing hand delta to that minimum; the constant alone supplies the default
// travel when the hand is still.
constexpr int g_minimum_slide_travel_frets = 2;

[[nodiscard]] constexpr int widenedToMinimumTravel(const int delta, const bool downward)
{
    return downward ? std::min(delta, -g_minimum_slide_travel_frets)
                    : std::max(delta, g_minimum_slide_travel_frets);
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
        // A sustainless note still sounds its instant's pitch: a non-zero origin survives as
        // the prebend point (the single bend shape a zero sustain can carry — and the shape
        // tremolo spell-out feeds, one flat sample per stroke). Curve motion after the onset
        // has nowhere to live, so it narrows to that point, reported when it existed.
        const bool moves = std::abs(bend.middle_value - bend.origin_value) > 1e-9 ||
                           std::abs(bend.destination_value - bend.origin_value) > 1e-9;
        if (std::abs(bend.origin_value) > 1e-9)
        {
            if (moves)
            {
                notes.emplace_back("flattened a bend on a sustainless note to its prebend");
            }
            return {BendPoint{.offset = Fraction{}, .semitones = bend.origin_value / 50.0}};
        }
        if (moves)
        {
            notes.emplace_back("dropped a bend on a note without sustain");
        }
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

// The GP bend model evaluated at a percent of the note duration: the origin value holds to
// its offset, rises to the middle plateau, holds between the middle offsets, rises to the
// destination, and holds to the end. Equal offsets read as a step.
[[nodiscard]] double bendValueAtPercent(const GpBend& bend, const double percent)
{
    const std::array<std::pair<double, double>, 4> points{
        std::pair{bend.origin_offset, bend.origin_value},
        std::pair{bend.middle_offset1, bend.middle_value},
        std::pair{bend.middle_offset2, bend.middle_value},
        std::pair{bend.destination_offset, bend.destination_value},
    };
    if (percent <= points.front().first)
    {
        return points.front().second;
    }
    // Walked as an iterator pair rather than by index: a runtime subscript on a fixed-size array
    // is unchecked bounds access, and each step needs the point before it anyway.
    for (auto segment = std::next(points.begin()); segment != points.end(); ++segment)
    {
        const auto [from_offset, from_value] = *std::prev(segment);
        const auto [to_offset, to_value] = *segment;
        if (percent > to_offset)
        {
            continue;
        }
        const double span = to_offset - from_offset;
        if (span <= 0.0)
        {
            return to_value;
        }
        return from_value + ((to_value - from_value) * ((percent - from_offset) / span));
    }
    return points.back().second;
}

// Spells out tremolo-picked beats as their individual strokes BEFORE event collection, so the
// strokes flow through positions, grace attachment, ties, and sustain trims exactly like
// hand-notated notes (the charting standard reserves the chart's `tremolo` for unmeasured
// noise, and Guitar Pro's tremolo is measured — the mark carries a precise stroke duration).
// Strokes re-pick: every stroke clears tie_destination, so a tie INTO the beat releases its
// origin when the first stroke's fresh onset lands, and only the last stroke keeps a notated
// onward tie so a ring-out continuation still binds. The first stroke keeps the accent and
// any hammer/pull arrival; later strokes are plain picks. A bent tremolo spells out too —
// each stroke samples the master curve at its own onset and carries the value as a flat
// prebend, so the run reads as progressively larger prebent picks.
// Only slide payloads keep the mark (per-stroke frets along a glide would be fabricated
// data, and the payloads include the pick-slide carriers), counted for the track report.
[[nodiscard]] std::vector<GpBeat> expandTremoloBeats(
    const std::vector<GpBeat>& beats, int& kept_marks)
{
    std::vector<GpBeat> expanded;
    expanded.reserve(beats.size());
    for (const GpBeat& beat : beats)
    {
        const Fraction stroke = beat.tremolo_stroke;
        if (stroke.numerator <= 0 || beat.grace != GpGracePlacement::None || beat.notes.empty())
        {
            expanded.push_back(beat);
            continue;
        }
        if (std::ranges::any_of(
                beat.notes, [](const GpNote& note) { return note.slide_flags != 0; }))
        {
            ++kept_marks;
            expanded.push_back(beat);
            continue;
        }
        const Fraction count_fraction =
            beat.duration_whole * Fraction{stroke.denominator, stroke.numerator};
        const auto count = static_cast<int>(count_fraction.numerator / count_fraction.denominator);
        if (count <= 1)
        {
            // A beat no longer than one stroke IS its single stroke; the mark adds nothing.
            GpBeat single = beat;
            single.tremolo_stroke = Fraction{};
            expanded.push_back(std::move(single));
            continue;
        }
        for (int index = 0; index < count; ++index)
        {
            GpBeat piece = beat;
            piece.tremolo_stroke = Fraction{};
            // The last stroke takes the remainder, so the beat's total duration survives an
            // indivisible span (dots and tuplets).
            piece.duration_whole =
                index + 1 == count ? beat.duration_whole - (Fraction{count - 1} * stroke) : stroke;
            const double onset_percent = 100.0 * static_cast<double>(index) *
                                         (static_cast<double>(stroke.numerator) *
                                          static_cast<double>(beat.duration_whole.denominator)) /
                                         (static_cast<double>(stroke.denominator) *
                                          static_cast<double>(beat.duration_whole.numerator));
            for (GpNote& note : piece.notes)
            {
                note.tie_destination = false;
                if (index + 1 < count)
                {
                    note.tie_origin = false;
                }
                if (index > 0)
                {
                    note.accent = false;
                    note.hopo_destination = false;
                }
                if (note.bend.has_value())
                {
                    // The stroke sounds the master curve's value at its own onset, carried as
                    // a flat prebend (a sustainless pick has exactly one pitch). Zero offsets
                    // collapse the shape to the single onset point through the builder's
                    // equal-offset merge.
                    const double value = bendValueAtPercent(*note.bend, onset_percent);
                    if (std::abs(value) > 1e-9)
                    {
                        note.bend = GpBend{
                            .origin_value = value,
                            .middle_value = value,
                            .destination_value = value,
                            .origin_offset = 0.0,
                            .middle_offset1 = 0.0,
                            .middle_offset2 = 0.0,
                            .destination_offset = 0.0,
                        };
                    }
                    else
                    {
                        note.bend.reset();
                    }
                }
            }
            expanded.push_back(std::move(piece));
        }
    }
    return expanded;
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
    int kept_tremolo_marks = 0;

    // Tremolo beats spell out first; the expanded copies live for the whole collection
    // because pending grace runs hold beat pointers across bar boundaries.
    std::vector<std::vector<std::vector<GpBeat>>> expanded_bars(track.bars.size());
    for (std::size_t bar_index = 0; bar_index < track.bars.size(); ++bar_index)
    {
        for (const std::vector<GpBeat>& voice : track.bars[bar_index].voices)
        {
            expanded_bars[bar_index].push_back(expandTremoloBeats(voice, kept_tremolo_marks));
        }
    }

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

        for (std::size_t voice_index = 0; voice_index < expanded_bars[bar_index].size();
             ++voice_index)
        {
            std::vector<const GpBeat*>& pending = pending_graces_per_voice[voice_index];
            Fraction position_beats{};
            for (const GpBeat& beat : expanded_bars[bar_index][voice_index])
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
                                        grace_note,
                                        grace->tremolo_stroke.numerator > 0,
                                        global,
                                        lead,
                                        principal_global);
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
                                        grace_note,
                                        grace->tremolo_stroke.numerator > 0,
                                        global,
                                        lead,
                                        principal_global);
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
                        beat.tremolo_stroke.numerator > 0,
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
    if (kept_tremolo_marks > 0)
    {
        notes.push_back(
            std::to_string(kept_tremolo_marks) +
            " tremolo beats kept their mark instead of spelling out (slide payloads)");
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

    // The beat the source notated this onset at (see NoteEvent::notated_beat). Grace machinery
    // is the only thing that makes the sounding onset diverge (a before-beat lead, an on-beat
    // shift): the note binds neighboring tails at its sounding beat while never faking a
    // deliberate hold at a beat the source never notated.
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

// The offset of the last payload point that CHANGES something — the last instant the tail still
// has information to present, and so the furthest a margin trim may be overridden (rule 2).
//
// A bend point repeating its predecessor's semitones and a waypoint repeating the previous fret
// (a HOLD, not a glide) both say what the tail already said, so a trailing run of them is not a
// reason to keep a tail open past the margin. The note starts unbent at its own fret, which is
// what the first point of each payload is measured against. Whole-note techniques — vibrato,
// tremolo, accent, muting, harmonics — cannot change mid-sustain and so never appear here at all.
// The unpitched slide-out is deliberately absent: its end is gesture geometry that trims back
// with the tail rather than pinning it (rule 2), and the trim compresses it separately.
[[nodiscard]] Fraction lastChangingPayloadOffset(const ChartNote& note)
{
    Fraction last{};
    double previous_semitones = 0.0;
    for (const BendPoint& point : note.bend)
    {
        if (std::is_neq(point.semitones <=> previous_semitones) && last < point.offset)
        {
            last = point.offset;
        }
        previous_semitones = point.semitones;
    }
    int previous_fret = note.fret;
    for (const SlideWaypoint& waypoint : note.slides)
    {
        if (waypoint.fret != previous_fret && last < waypoint.offset)
        {
            last = waypoint.offset;
        }
        previous_fret = waypoint.fret;
    }
    return last;
}

// Normalizes imported sustains for chart readability (import policy). The maintained
// plain-English spec is "GP chart normalization policy" in
// docs/developer/the-project-lifecycle.md — tweak behavior there first, then re-align this code.
//
// 1. A tail is trimmed to end at least the minimum-sustain-distance margin — the shared
//    constant in grid_arithmetic.h, the same margin the editor's duration verb clamps to —
//    before the next BINDING onset on ANY string. Binding follows the notated timeline:
//    events sharing a notated beat (chord members, and a strum's own grace-shifted notes)
//    never bind each other, even when grace leads stagger their sounding onsets. One hold is
//    exempt: a tail ringing strictly past the next binding onset's NOTATED beat — merged from
//    a tie or notated across voices — is a deliberate hold that neither this trim nor the
//    drop rule touches (that ring is exactly what the projections' arpeggio arrival rule
//    reads). An importer-fabricated early onset (a grace lead) binds the tail at its
//    sounding beat but cannot witness a hold: the source never notated an onset there, so a
//    ring past it proves nothing deliberate. Repeated chords trim like
//    everything else: their held reading lives in the merged shape span (rule 11), which is
//    derived from the notated pre-trim ends and already runs through every restrike.
// 2. Carrying a technique is not an exemption from rule 1; the margin yields only to
//    information, and only as far as the information reaches. The tail floors at the last
//    payload point that CHANGES something — a bend point differing from its predecessor, a
//    waypoint differing from the previous fret — and stops exactly there, never running on to
//    the notated end (exact adjacency stays legal per 40-Q2-B, so a slide still reaches its
//    target note). Payload that repeats what the tail already said holds nothing open: a
//    trailing hold waypoint is a pin, not a glide, and a repeated bend value is not news, so
//    points the trim passes leave with the tail. Whole-note techniques — vibrato, tremolo,
//    accent, muting, harmonics — cannot change mid-sustain and so never override the margin at
//    all.
// 3. A note with no sustain-carried technique NOTATED shorter than one beat loses its tail
//    entirely after trimming: Guitar Pro gives every note its full notated duration, and a
//    sub-beat effect-free ring reads as noise in a chart rather than as a deliberate sustain.
//    The comparison reads the notated length, not the trimmed one: a note held a full beat or
//    longer in the source keeps its tail even though the margin leaves it slightly shorter than
//    the beat. The decision belongs to the NOTATED STRUM, not the single string: every string of
//    a chord rings from one stroke, so a tail any member earned — a technique on it, a notated
//    ring of a full beat, or rule 1's hold exemption — keeps the whole strum's tails. Deciding
//    per string drew a lone tail on a chord's bent note beside partners that looked unsounded.
//
// The rules are import normalization only — the editor never rewrites spacing the user authored.
void normalizeImportedSustains(
    std::vector<BuiltNote>& built, const MeasureGrid& grid, std::vector<std::string>& notes)
{
    int trimmed = 0;

    // Rule 3 reads each tail's NOTATED ring, so it is captured before the trim pass rewrites it,
    // and the pass records which tails rule 1's hold exemption spared.
    std::vector<Fraction> notated_sustain;
    notated_sustain.reserve(built.size());
    for (const BuiltNote& entry : built)
    {
        notated_sustain.push_back(entry.note.sustain);
    }
    std::vector<bool> deliberate_hold(built.size(), false);

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
            // The next binding onset (rule 1): the first later event whose NOTATED beat differs
            // from this note's — notationally simultaneous events (chord members, a strum's own
            // grace-shifted notes) never bind. The tail keeps the margin before the binding
            // onset's SOUNDING beat, but the hold exemption reads its NOTATED beat: a grace
            // lead sounds inside an earlier ring without the source ever notating an onset
            // there, so it must not turn that ring into a "deliberate hold".
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
                deliberate_hold[index] = true;
                continue;
            }
            if (note.sustain.numerator > 0 && has_binding)
            {
                const Fraction margin = sustainMarginAt(grid, note.position);
                const Fraction limit = sounding_gap - margin;
                if (limit < note.sustain)
                {
                    Fraction target = limit.numerator < 0 ? Fraction{} : limit;
                    // A scrape's path is DERIVED gesture geometry, synthesized from the notated
                    // duration rather than authored, so moving its endpoint loses no
                    // information — which is why the trim squishes the gesture instead of
                    // flooring the tail on it the way an authored bend point does (rule 2).
                    // Scrapes carry no bend and no slide-out (the carrier conversion sheds
                    // both), so this branch is the whole payload story for them.
                    //
                    // Where the leg sits relative to the margin decides everything, and the
                    // two cases are the whole rule. A leg that STARTS before the margin line has
                    // room to end on it, so it does: the gap is the margin exactly, and no
                    // spacing is given up. A leg that starts ON OR AFTER that line cannot yield
                    // the margin at all — it is already inside the window — so it halves the
                    // distance to the onset, which is the one split that always leaves some gap
                    // whatever the crowding. This is the sanctioned exception: the gesture is
                    // LITERALLY defined inside the margin, which is exactly when the spacing rule
                    // steps aside.
                    //
                    // No compression floor. Both cases land strictly after the leg's start by
                    // construction — the first by its own branch condition, the second because
                    // half of a positive room is positive — so the payload stays ascending
                    // without one, and a floor here could only buy leg length by spending the
                    // spacing the rule exists to protect. g_minimum_slide_window keeps its other
                    // job, which is SYNTHESIS: a gesture built from nothing needs a default span.
                    // That is not this decision.
                    if (note.attack == NoteAttack::PickSlide && !note.slides.empty())
                    {
                        const Fraction leg_start = note.slides.size() > 1
                                                       ? note.slides[note.slides.size() - 2].offset
                                                       : Fraction{};
                        Fraction terminal = note.slides.back().offset;
                        if (leg_start < limit)
                        {
                            terminal = limit;
                        }
                        else if (leg_start < sounding_gap)
                        {
                            // Half the distance to the ONSET, not half the notated length: a leg
                            // notated past the onset would halve to something still past it. The
                            // hold check above already claims those notes, so this is belt and
                            // braces against that guard ever moving.
                            terminal = leg_start + ((sounding_gap - leg_start) * Fraction{1, 2});
                        }
                        // A leg starting at or beyond the onset has nothing to crunch against
                        // (a grace lead can shift a sounding onset under a notated ring), so it
                        // keeps its end and the assignment below only ever shortens.
                        if (terminal < note.slides.back().offset)
                        {
                            note.slides.back().offset = terminal;
                        }
                        target = note.slides.back().offset;
                    }
                    else
                    {
                        // Rule 2: the margin yields only to information, and only as far as the
                        // information reaches — the tail extends to the last payload point that
                        // CHANGES something and stops exactly there, never on to the notated end.
                        const Fraction informative = lastChangingPayloadOffset(note);
                        if (target < informative)
                        {
                            target = informative;
                        }
                        // Trailing points the target passed present nothing new (only
                        // non-changing ones can sit past the last changing one), so they leave
                        // with the tail. Clipping here rather than after the assignment below
                        // keeps the model's "payload within the sustain" invariant AND lets the
                        // slide-out measure itself against the path that survives — a trailing
                        // hold waypoint must not hold the gesture open through the margin.
                        std::erase_if(note.bend, [target](const BendPoint& point) {
                            return target < point.offset;
                        });
                        std::erase_if(note.slides, [target](const SlideWaypoint& waypoint) {
                            return target < waypoint.offset;
                        });
                    }
                    // The unpitched slide-out is NOT a protected payload: its end is gesture
                    // geometry derived from the notated duration, not a musical event, so it
                    // trims back with the tail to respect the margin. The trimmed end must stay
                    // strictly positive and strictly after the last waypoint (the model's
                    // ascending-payload invariant); a crowding that would crush it compresses to
                    // the smallest legal end instead of keeping its full length — the old
                    // keep-the-end fallback ran the gesture through the next sounding onset when
                    // a slide-in had moved its head into the gap (the slide-out-into-slide-in
                    // dip).
                    if (note.slide_out.has_value() && target < note.slide_out->offset)
                    {
                        const Fraction compressed = keptStrictlyAfterLastWaypoint(
                            note, std::max(target, g_minimum_slide_window));
                        if (compressed < note.slide_out->offset)
                        {
                            note.slide_out->offset = compressed;
                        }
                        target = note.slide_out->offset;
                    }
                    if (target < note.sustain)
                    {
                        note.sustain = target;
                        ++trimmed;
                    }
                }
            }
        }
        group_begin = group_end;
    }

    // Rule 3, decided per notated strum: a tail any member of the strum earned keeps every
    // member's tail, so a chord never shows one string ringing beside partners that look
    // unsounded. Grouping is the notated beat — the same identity rule 1's binding scan uses, so
    // grace-shifted strum members and cross-voice simultaneities count as one stroke here too.
    std::map<Fraction, bool> strum_earned_tail;
    for (std::size_t index = 0; index < built.size(); ++index)
    {
        const bool earned = deliberate_hold[index] || hasSustainTechnique(built[index].note) ||
                            notated_sustain[index] >= Fraction{1};
        const auto strum = strum_earned_tail.try_emplace(built[index].notated_beat, false).first;
        strum->second = strum->second || earned;
    }
    int dropped = 0;
    for (BuiltNote& entry : built)
    {
        ChartNote& note = entry.note;
        if (note.sustain.numerator > 0 && !strum_earned_tail.at(entry.notated_beat))
        {
            // Nothing to clip with the tail: a strum with no earned tail carries no payload on
            // any member (a bend or slide would have earned it).
            note.sustain = Fraction{};
            ++dropped;
        }
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

// Derives chord templates and hand-posture spans from the note stream (import policy). Guitar Pro
// scores in practice carry no handshape data (corpus chord collections are empty), so any onset
// striking two or more strings becomes a chord posture, deduplicated into the template table, and
// consecutive onsets holding the same posture merge into one shape span covering the strums'
// notated (pre-trim) durations — the grouping the tab renders as a chord box over repeated strums.
// Tap-only onsets are transparent to the whole derivation: taps are the tapping hand, so they
// neither form postures nor close held spans, letting a ringing chord's span cover the taps above
// it. ANY articulation difference is a new chord: span continuity compares each string's whole note
// with only its position and duration neutralized, so attack (hammer/pull/tap/slap/pop), muting,
// harmonics, vibrato, tremolo, accent, bends, and slides — and any technique added to ChartNote
// later — all split the span, while strum durations never do. The template table stays deduplicated
// by frets alone (the hand posture is identical; techniques render on the notes). A note still
// ringing through a chord's onset (tie-held from before, not re-struck) joins the posture on its
// string (policy rule 12); the projections' shared arrival rule then renders the partly-struck span
// as an arpeggio, while fully-strummed spans stay chord boxes — no other arpeggio grouping is
// derived (broken-chord grouping needs the corpus-informed pass). A span closed by a following
// event trims to the minimum-sustain-distance margin before it — the same margin every other
// element keeps (policy rule 12a). Derived templates are unnamed and unfingered.
// The maintained plain-English spec is "GP chart normalization policy" in
// docs/developer/the-project-lifecycle.md.
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
    // (policy rule 12a — spans keep the same minimum sustain distance as every other element),
    // floored at the last strum so the box always reaches its final restrike. A span that would
    // lose all length (a single strum crowded closer than the margin) falls back to exact
    // adjacency, mirroring the sustain rules' protected-adjacency precedent — chart validation
    // rejects zero-length spans.
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
            // Right-hand onsets are invisible to span derivation: they join no posture and
            // extend no ring, so a mixed onset is judged by its fretting-hand members alone.
            if (!common::core::rightHandOnset(note.attack))
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
                string_index < string_count &&
                !common::core::rightHandOnset(built[member].note.attack))
            {
                ringing[string_index] = &built[member];
            }
        }
        index = onset_end;
    }
    close_span(std::nullopt, std::nullopt);
}

// A silence long enough to read as a phrase break: the hand re-anchors across it. 0.8s is the
// corpus sweet spot (4100-arrangement source-corpus study) — it holds the authored move rate
// (~13.2 anchors per 100 notes) while lifting exact anchor-fret agreement from 59% to 72%.
constexpr double g_fhp_phrase_rest_seconds = 0.8;

// The fret span of notes still ringing at a slide waypoint that are NOT themselves gliding there
// — each is a planted finger that pins the hand window's edge on its side. Returns false when no
// such note exists, so the slide is a genuine whole-hand travel (rule 9 drag) rather than a
// one-finger reshape. Taps float above the hand and open strings never anchor it, so both are
// excluded. A note that itself slid earlier is held at the fret it has reached; a note with a
// waypoint at this exact instant is a co-slider (its own event carries it, and a whole chord
// gliding in lockstep must translate, not reshape), so it is excluded too.
[[nodiscard]] bool heldHullAtSlideWaypoint(
    const std::vector<BuiltNote>& built, std::size_t moving_index, const Fraction& instant,
    int& held_min, int& held_max)
{
    bool any = false;
    for (std::size_t index = 0; index < built.size(); ++index)
    {
        if (index == moving_index)
        {
            continue;
        }
        const BuiltNote& other = built[index];
        if (!(other.global_beat <= instant && instant < other.end_global_beat))
        {
            continue; // not sounding at this instant
        }
        // A natural harmonic has no stop of its own, so its `fret` is the nut (or the capo) and
        // its fretting hand is at the NODE instead — reading `fret` here would drop a 12th-fret
        // harmonic passage out of hand derivation and leave the window at the nut.
        const int other_hand_fret = common::core::fretFor(other.note);
        if (common::core::rightHandOnset(other.note.attack) || other_hand_fret <= 0)
        {
            continue; // right-hand onsets float above the hand; open strings never anchor it
        }
        int fret = other_hand_fret;
        bool co_sliding = false;
        for (const SlideWaypoint& waypoint : other.note.slides)
        {
            const Fraction waypoint_beat = other.global_beat + waypoint.offset;
            if (waypoint_beat < instant)
            {
                fret = waypoint.fret; // already reached this waypoint
                continue;
            }
            // Waypoints are ascending, so nothing past here can precede the instant. A waypoint
            // landing exactly on it means the note is gliding in lockstep — treat it as moving.
            co_sliding = waypoint_beat == instant;
            break;
        }
        if (co_sliding || fret <= 0)
        {
            continue;
        }
        held_min = any ? std::min(held_min, fret) : fret;
        held_max = any ? std::max(held_max, fret) : fret;
        any = true;
    }
    return any;
}

// Generates the fret-hand position track, corpus-derived from the source-corpus study
// (docs/plans/todo/fhp-corpus-derived-generation.md, 4100 authored arrangements). The hand covers
// a [fret, fret+width-1] window (struck onsets get width four unless one spans wider; a slide
// reshape follows the exact finger span and may be narrower), open strings never constrain it,
// and it tracks the LEFT hand. Three rules the earlier greedy walk could not capture:
//   1. A TAPPED note is not a coverage event. Two-hand taps sit a median seven frets above the
//      fretting hand, so the anchor stays on the fretted / left-hand notes and any held chord
//      shape while the tap floats above the window; the highway camera frames the tap separately.
//   2. The hand RE-ANCHORS at musical boundaries — section starts (phrase_boundary_beats) and
//      rests >= g_fhp_phrase_rest_seconds — biased to the phrase's floor fret, not only when a
//      note leaves the window (only ~35% of authored moves are forced). Within a segment it moves
//      minimally when forced and drags with pitched slides.
//   3. A slide taken while another finger stays PLANTED reshapes the window instead of translating
//      it: the held note pins its edge and the window becomes the exact sounding hull, so it
//      shrinks when an outer note slides inward, grows when it slides outward, and holds when
//      the slide is interior. Only a slide with nothing else held moves the whole hand (rule 9
//      drag). This reads the built notes' sounding spans — see heldHullAtSlideWaypoint — so the
//      generator is sustain-aware for held detection (see below).
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
    // fit like a struck onset. A reshape waypoint is a slide taken while another finger stays
    // planted: [min_fret, max_fret] is then the exact sounding hull (held frets plus the slide
    // target) and the walk fits it edge-for-edge with no drag and no width floor, so the hand
    // shrinks, grows, or holds with the slide instead of translating.
    struct CoverageEvent
    {
        Fraction global_beat{};
        GridPosition position;
        int min_fret{0};
        int max_fret{0};
        int shift{0};
        bool reshape{false};
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
            // Right-hand onsets float above the window and never anchor the hand.
            if (!common::core::rightHandOnset(note.attack))
            {
                const int hand_fret = common::core::fretFor(note);
                if (hand_fret > 0)
                {
                    onset.min_fret =
                        onset.min_fret == 0 ? hand_fret : std::min(onset.min_fret, hand_fret);
                    onset.max_fret = std::max(onset.max_fret, hand_fret);
                }
                int slide_source = note.fret;
                for (const SlideWaypoint& waypoint : note.slides)
                {
                    if (waypoint.fret <= 0)
                    {
                        continue;
                    }
                    // An equal-fret waypoint is a HOLD, not a glide: nothing travels across it, so
                    // it announces no new hand position and must not place one. Letting it place
                    // one moves the window mid-note for no reason — a tie chain that holds a fret
                    // and then trails off would shift the hand at the hold, beats into the held
                    // note, instead of leaving it put until the slide itself moves it. The
                    // projection's ramp derivation draws the same distinction for the same reason
                    // (see slide_ramp_starts in highway_projection.cpp).
                    if (waypoint.fret == slide_source)
                    {
                        continue;
                    }
                    const Fraction waypoint_beat = built[onset_end].global_beat + waypoint.offset;
                    const GridPosition waypoint_position = common::core::advanceGridPosition(
                        tempo_map, note.position, waypoint.offset);
                    int held_min = 0;
                    int held_max = 0;
                    if (heldHullAtSlideWaypoint(
                            built, onset_end, waypoint_beat, held_min, held_max))
                    {
                        // A finger stays planted: the window reshapes to the exact sounding hull
                        // (held frets pin their edge, the slide carries the other) — no drag.
                        events.push_back(
                            CoverageEvent{
                                .global_beat = waypoint_beat,
                                .position = waypoint_position,
                                .min_fret = std::min(waypoint.fret, held_min),
                                .max_fret = std::max(waypoint.fret, held_max),
                                .shift = 0,
                                .reshape = true,
                            });
                    }
                    else
                    {
                        // Nothing else is held: the whole hand travels with the slide (rule 9).
                        events.push_back(
                            CoverageEvent{
                                .global_beat = waypoint_beat,
                                .position = waypoint_position,
                                .min_fret = waypoint.fret,
                                .max_fret = waypoint.fret,
                                .shift = slide_source > 0 ? waypoint.fret - slide_source : 0,
                            });
                    }
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
            merged.back().reshape = merged.back().reshape || event.reshape;
            // Simultaneous slides drag as one hand only while their deltas agree (a whole chord
            // gliding by the same amount). Disagreeing deltas are a convergence or divergence —
            // the hand reshapes in place, so the drag is cancelled to 0 rather than adopting one
            // arbitrary delta.
            if (event.shift != 0)
            {
                merged.back().shift =
                    (merged.back().shift == 0 || merged.back().shift == event.shift) ? event.shift
                                                                                     : 0;
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

        int next_anchor = 0;
        int next_width = 0;
        if (event.reshape && !reanchor)
        {
            // Hull-exact reshape: a held finger pins its edge and the sliding finger carries the
            // other, so the window is exactly the sounding span — it shrinks when an outer note
            // slides inward, grows when it slides outward, and holds when the slide is interior.
            // No width floor and no drag: the hand deforms with the slide.
            next_anchor = event.min_fret;
            next_width = event.max_fret - event.min_fret + 1;
        }
        else
        {
            const bool covered = have_anchor && !reanchor && event.min_fret >= anchor &&
                                 event.max_fret <= anchor + width - 1;
            if (event.shift == 0 && covered)
            {
                continue;
            }
            next_width = std::max(4, event.max_fret - event.min_fret + 1);
            const int lowest_anchor = std::max(1, event.max_fret - next_width + 1);
            // At a boundary the hand re-places biased to the phrase's floor (the lowest fretted
            // note); otherwise it drags from the current anchor by the slide delta and clamps.
            next_anchor = reanchor
                              ? std::clamp(event.min_fret, lowest_anchor, event.min_fret)
                              : std::clamp(anchor + event.shift, lowest_anchor, event.min_fret);
        }
        if (have_anchor && next_anchor == anchor && next_width == width)
        {
            continue; // landed on the same window; nothing visible changed
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

    // An opening run of notes that anchor nothing (open strings, taps) must not pin the hand at
    // the nut-reference window: the song's starting position is wherever the first anchoring note
    // puts the hand, so the first placement retimes back to the chart's first note and the window
    // is already settled there when the song begins.
    if (!positions.empty() && !built.empty() &&
        built.front().note.position < positions.front().position)
    {
        positions.front().position = built.front().note.position;
    }
    return positions;
}

// The fret-hand placement window active at a position: the iterator PAST the last placement at
// or before it. Callers read `after - 1` as the active window and handle begin() ("no window
// yet") themselves — the two slide resolvers deliberately differ there (a scoop still applies
// with its default start; a trail-off gesture is skipped entirely).
[[nodiscard]] std::vector<common::core::FretHandPosition>::iterator firstPlacementAfter(
    std::vector<common::core::FretHandPosition>& placements, const GridPosition& position)
{
    return std::ranges::upper_bound(
        placements, position, std::ranges::less{}, &common::core::FretHandPosition::position);
}

// Merges a fabricated placement into the sorted track, keeping positions unique and ascending;
// a placement already at the instant wins.
void insertPlacementIfAbsent(
    std::vector<common::core::FretHandPosition>& placements,
    const common::core::FretHandPosition& placement)
{
    const auto at = std::ranges::lower_bound(
        placements,
        placement.position,
        std::ranges::less{},
        &common::core::FretHandPosition::position);
    if (at == placements.end() || !(at->position == placement.position))
    {
        placements.insert(at, placement);
    }
}

// The same merge, but the fabricated placement wins at its instant.
void upsertPlacement(
    std::vector<common::core::FretHandPosition>& placements,
    const common::core::FretHandPosition& placement)
{
    const auto at = std::ranges::lower_bound(
        placements,
        placement.position,
        std::ranges::less{},
        &common::core::FretHandPosition::position);
    if (at != placements.end() && at->position == placement.position)
    {
        *at = placement;
    }
    else
    {
        placements.insert(at, placement);
    }
}

// The active window ridden by a gesture's fret travel, clamped so the ridden window still
// covers covered_fret on the neck.
[[nodiscard]] int windowAnchorCovering(
    const common::core::FretHandPosition& active, const int travel, const int covered_fret)
{
    return std::clamp(
        active.fret + travel,
        std::max(1, covered_fret - active.width + 1),
        std::max(1, covered_fret));
}

// Resolves bare slide-in flags (16 from below, 32 from above) into ordinary slides — no new
// notation. The gesture is an ON-BEAT scoop: the ornament is the manner of the note's ATTACK and
// occupies the note's own time slot — notation practice and faithful score players pluck on the
// notated tick at an offset pitch and resolve to the target a quarter of the duration in. The head
// therefore keeps its notated position at a derived approach fret and glides to the notated fret
// over the scoop window: a quarter of the notated duration, capped at the sustain margin, floored
// at the minimum slide window, and kept strictly before the note's slide chain and trail-off end
// (bend curves order only against the sustain, so the scoop leaves them untouched). Anticipation —
// the approach sounding BEFORE the beat with the target landing ON it — is the before-beat
// grace-with-slide notation, which resolves through the ordinary chain; a bare slide-in must not
// fabricate it.
//
// Guitar Pro gives the gesture no start fret, so the fret-hand positions supply it: the window
// walk's delta arriving at the note, widened to a two-fret minimum, the flag's direction winning
// over a still hand or a contradicting delta. The hand stays planted while the approach fret sits
// inside the active window — a two-fret scoop is usually a finger gesture, not a hand move (the
// unpitched-slide precedent). An approach OUTSIDE the window drags the window with it for exactly
// the scoop's duration (a window anchored on the notated fret left the approach uncovered): the
// onset's window derives backward from the active one so the head keeps its slot, and the natural
// window returns at the scoop's end. An open string cannot be slid into, and a start clamped onto
// the notated fret has no travel; both count as unplaceable and stay plain. The transform runs
// before the sustain policy, so the transformed note is a slide when the trim rules run: a slide-in
// into a held landing keeps its hold like any notated slide.
void resolveSlideIns(
    std::vector<BuiltNote>& built, std::vector<common::core::FretHandPosition>& placements,
    const MeasureGrid& grid, std::vector<std::string>& notes)
{
    int unplaceable = 0;
    // Applied after the loop so every start fret derives from the pristine natural track — a
    // fabricated dip must never feed a later slide-in's slot math. Dips replace whatever sits
    // at their instant (the scoop owns its onset); restores yield to any real placement
    // already at the scoop's end.
    std::vector<common::core::FretHandPosition> dip_placements;
    std::vector<common::core::FretHandPosition> restore_placements;
    for (BuiltNote& entry : built)
    {
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

        int start = from_below ? note.fret - g_minimum_slide_travel_frets
                               : note.fret + g_minimum_slide_travel_frets;
        const auto after = firstPlacementAfter(placements, note.position);
        if (after != placements.begin())
        {
            const auto landing = after - 1;
            if (landing != placements.begin() && landing->position == note.position)
            {
                const int delta = (landing - 1)->fret - landing->fret;
                if (delta != 0 && (delta < 0) == from_below)
                {
                    // FHP-derived travel, widened to the same two-fret minimum as the default:
                    // a one-fret hand move must not shrink the approach below what reads as a
                    // slide.
                    start = note.fret + widenedToMinimumTravel(delta, from_below);
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

        // The scoop window (see the function comment); an existing chain waypoint keeps the
        // payload ascending by gliding through half its own offset instead.
        Fraction window = note.sustain * Fraction{1, 4};
        const Fraction margin = sustainMarginAt(grid, note.position);
        if (margin < window)
        {
            window = margin;
        }
        if (window < g_minimum_slide_window)
        {
            window = g_minimum_slide_window;
        }
        if (!note.slides.empty() && window >= note.slides.front().offset)
        {
            window = note.slides.front().offset * Fraction{1, 2};
        }
        // A slide-out is the other fret-travel payload the scoop must stay strictly before:
        // on a short note the floored window can reach the trail-off end the chain resolver
        // pinned at the sustain, and a waypoint at or past it fails chart validation.
        if (note.slide_out.has_value() && window >= note.slide_out->offset)
        {
            window = note.slide_out->offset * Fraction{1, 2};
        }
        if (note.sustain < window)
        {
            note.sustain = window;
        }

        // The active window at the onset; a start it does not cover drags it for the scoop.
        if (after != placements.begin())
        {
            const auto active = after - 1;
            if (start < active->fret || start >= active->fret + active->width)
            {
                const int dip_anchor = windowAnchorCovering(*active, start - note.fret, start);
                dip_placements.push_back(
                    common::core::FretHandPosition{
                        .position = note.position,
                        .fret = dip_anchor,
                        .width = active->width,
                    });
                restore_placements.push_back(
                    common::core::FretHandPosition{
                        .position = gridPositionForGlobalBeat(grid, entry.global_beat + window),
                        .fret = active->fret,
                        .width = active->width,
                    });
            }
        }

        note.slides.insert(note.slides.begin(), SlideWaypoint{.offset = window, .fret = note.fret});
        note.fret = start;
    }
    // Merge the fabricated windows: dips own their instant, restores yield to real placements.
    for (const common::core::FretHandPosition& dip : dip_placements)
    {
        upsertPlacement(placements, dip);
    }
    for (const common::core::FretHandPosition& restore : restore_placements)
    {
        insertPlacementIfAbsent(placements, restore);
    }
    if (unplaceable > 0)
    {
        notes.push_back(
            std::to_string(unplaceable) +
            " slide-ins had no representable start and were left plain");
    }
}

// Rides the hand window along every unpitched trail-off: the window always moves with the gesture
// — an exit placement at the trail-off's end, reached through the projection's standard margin
// morph so the motion lands with the perceptible release — and the hand's next move decides only
// the exit fret and what follows. When the next placement departs in the trail-off's direction AND
// arrives by the very next onset, the trail-off IS the departure: the exit fret rides that travel
// (widened to the slide-in rule's two-fret minimum) and the window flows onward into the arrival.
// Otherwise the trail-off is a release: the exit keeps the fixed four-fret gesture, the window dips
// with it, and a restore placement at the next onset brings the window back for the note that
// follows (so notes after the gesture are never stranded in the dipped window). Fabricated exits
// yield to real placements at their instant, restores yield to anything already there, and a
// trail-off ending at or past the next onset stays planted (no room to ride). Runs after the
// sustain trim so the end positions are the compressed ones the chart ships.
void resolveSlideOutExits(
    std::vector<BuiltNote>& built, std::vector<common::core::FretHandPosition>& placements,
    const MeasureGrid& grid)
{
    std::vector<common::core::FretHandPosition> exit_placements;
    std::vector<common::core::FretHandPosition> restore_placements;
    for (std::size_t index = 0; index < built.size(); ++index)
    {
        BuiltNote& entry = built[index];
        ChartNote& note = entry.note;
        if (!note.slide_out.has_value())
        {
            continue;
        }
        const int departing = note.slides.empty() ? note.fret : note.slides.back().fret;
        const bool downward = note.slide_out->fret < departing;
        const auto after = firstPlacementAfter(placements, note.position);
        if (after == placements.begin())
        {
            continue;
        }
        const auto active = after - 1;

        std::size_t next_note = index + 1;
        while (next_note < built.size() && built[next_note].global_beat <= entry.global_beat)
        {
            ++next_note;
        }
        const GridPosition end_position =
            gridPositionForGlobalBeat(grid, entry.global_beat + note.slide_out->offset);
        const bool has_next = next_note < built.size();
        const bool has_room = !has_next || end_position < built[next_note].note.position;
        if (has_next && !has_room)
        {
            // The gesture reaches the next onset (a hold-exempt trail-off the trim never
            // compressed, or a crush to exactly the gap): no room to ride, so the whole
            // gesture stays planted — default exit fret, no fabricated placements.
            continue;
        }

        // Departure: the next placement's move serves the very next onset and agrees with
        // the trail-off's direction, so the window flows onward instead of returning.
        const int delta = after == placements.end() ? 0 : after->fret - active->fret;
        const bool departs = delta != 0 && (delta < 0) == downward && has_next &&
                             !(built[next_note].note.position < after->position);
        if (departs)
        {
            const int travel = widenedToMinimumTravel(delta, downward);
            note.slide_out->fret = std::clamp(departing + travel, 0, common::core::g_max_fret);
        }
        else if (has_next)
        {
            restore_placements.push_back(
                common::core::FretHandPosition{
                    .position = built[next_note].note.position,
                    .fret = active->fret,
                    .width = active->width,
                });
        }
        // No note follows and the gesture is not a departure: the window may rest where the
        // gesture ends — an exit with no restore.

        // The riding window derives from the active one by the gesture's travel, clamped to
        // keep the exit fret covered on the neck.
        const int anchor =
            windowAnchorCovering(*active, note.slide_out->fret - departing, note.slide_out->fret);
        exit_placements.push_back(
            common::core::FretHandPosition{
                .position = end_position,
                .fret = anchor,
                .width = active->width,
            });
    }
    // Merge the fabricated windows: exits and restores both yield to real placements.
    for (const common::core::FretHandPosition& exit : exit_placements)
    {
        insertPlacementIfAbsent(placements, exit);
    }
    for (const common::core::FretHandPosition& restore : restore_placements)
    {
        insertPlacementIfAbsent(placements, restore);
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
    int unsupported_harmonics = 0;
    int semi_as_pinch = 0;
    int implausible_natural_labels = 0;
    int defaulted_fretted_nodes = 0;

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
            // stroke), which the hammer-on states accurately — no separate notation. Always
            // a hammer, never a pull: nothing is released to sound it. The Hammer attack also
            // gives the right downstream behavior automatically — the note anchors the fret hand,
            // closes chord spans, and never floats above the window, all of which are Tap-attack
            // special cases. Checked before the generic tap: a note carrying both marks is a
            // left-hand tap, the more specific articulation.
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
            // Guitar Pro's HarmonicFret means two different things. For a NATURAL harmonic it is
            // the node itself: the note's Fret already carries the touched position and
            // HarmonicFret refines it. For a harmonic over a real stop — pinch, artificial,
            // tapped — it is instead a *partial label* spelled as the familiar open-string
            // position (18 of 56 corpus pinches name a position BELOW their own fret, which no
            // thumb can reach). Fret positions are logarithmic, so the real node is the stop
            // plus the label's open-string offset.
            //
            // Labels are conventional roundings, so each is snapped to the true node it names —
            // a touch even slightly off a node chokes the harmonic. A label farther than half a
            // fret from every node names nothing: real labels land within 0.331 of a node, while
            // the integer frets with no harmonic near them (1, 11, 13, ...) miss by 0.669 or
            // more, and snapping those anyway would move the touch a whole fret and sound a
            // different partial.
            constexpr double plausible_label_error = 0.5;
            const bool fretted_harmonic =
                source.harmonic_type == "Pinch" || source.harmonic_type == "Semi" ||
                source.harmonic_type == "Artificial" || source.harmonic_type == "Tap";
            if (fretted_harmonic)
            {
                if (source.harmonic_type == "Pinch" || source.harmonic_type == "Semi")
                {
                    // GP can notate a legato or tap mark beside the pinch; the single-attack
                    // model keeps one onset, and the pinch is the one the squeal makes audible.
                    // A SEMI-harmonic is a pinch whose fundamental keeps ringing — a pinch not
                    // fully executed — and imports as one until the format distinguishes them.
                    note.attack = NoteAttack::Pinch;
                    semi_as_pinch += source.harmonic_type == "Semi" ? 1 : 0;
                }
                else if (source.harmonic_type == "Tap")
                {
                    note.attack = NoteAttack::Tap;
                }
                // The stop the harmonic speaks from: the note's own fret, or the capo when the
                // string is open — the capo is what stops a capo'd string.
                const int stop_fret = source.fret > 0 ? source.fret : chart.tuning.capo;
                // With no usable label the octave is the default: the 2nd partial is the
                // lowest-order harmonic available at any fret and so the easiest to ring. Using
                // the *fret* as a label here would read a stop as a partial number.
                double offset = 12.0;
                bool defaulted = true;
                if (source.harmonic_fret.has_value())
                {
                    const double snapped = common::core::snapHarmonicNode(
                        *source.harmonic_fret, common::core::g_max_snapped_partial);
                    if (std::abs(snapped - *source.harmonic_fret) <= plausible_label_error)
                    {
                        offset = snapped;
                        defaulted = false;
                    }
                }
                defaulted_fretted_nodes += defaulted ? 1 : 0;
                note.harmonic_node = static_cast<double>(stop_fret) + offset;
            }
            else if (source.harmonic_type == "Natural")
            {
                // A natural has no stop of its own — the string speaks from the nut or the capo,
                // and the node carries the position, so `fret` never doubles as a rounded copy
                // of it. The label (or, absent one, the note's own fret, which for a natural IS
                // the touched position) resolves against an open string and lands on the real
                // stop. That formula is right whether GP writes its numbers nut-referenced or
                // capo-relative; the frame question for ordinary frets is recorded in the
                // technique-compatibility plan doc.
                const double notated =
                    source.harmonic_fret.value_or(static_cast<double>(source.fret));
                const double offset =
                    common::core::snapHarmonicNode(notated, common::core::g_max_snapped_partial);
                if (std::abs(offset - notated) <= plausible_label_error)
                {
                    note.harmonic_node = static_cast<double>(chart.tuning.capo) + offset;
                    note.fret = chart.tuning.capo;
                }
                else
                {
                    // The label names no node; the note survives as an ordinary fretted note.
                    ++implausible_natural_labels;
                }
            }
            else
            {
                // Feedback harmonics are deliberately unsupported — feedback needs a real amp in
                // the room, which headphone play cannot produce — and unknown types land here
                // too. The note survives as an ordinary note; the count keeps the loss loud.
                ++unsupported_harmonics;
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
    // and head. A legato slide (flag 2) is a continuation of the same note: the target is not
    // re-picked, so it folds into the origin as a pitched waypoint at the junction — the sustain
    // extends through the target's notated end, its sustain-carried techniques fold in, and its own
    // onward slide continues the chain until a shift, a slide-out, or the chain's end stops it.
    // Slide-outs trail off unpitched.
    std::vector<bool> merged_away(built.size(), false);

    // Pick-slide carriers (Slide flags 64 down / 128 up) convert IN PLACE into pick-slide
    // notes before any slide chain resolves (plan 55, note-carried design): the dead carrier is
    // Guitar Pro's encoding vehicle for the gesture, so it sheds its mute and gains the attack plus
    // the corpus-derived default path (down 17 -> 3, up the mirror) across the notated span, ready
    // for the user to reshape.
    //
    // Simultaneous same-direction carriers are ONE scrape sounding on EVERY string it crosses, so
    // each carrier becomes its own note on its own string rather than collapsing to one. They share
    // the longest notated span, because they are one gesture and the pick reaches the end of its
    // travel once. A conflicting direction at the same onset is still dropped with a report — two
    // opposed scrapes at one instant is a notation error, not a chord. The converted notes then
    // participate in the ordinary minimum-distance trims like any note.
    int imported_pick_slides = 0;
    int conflicting_pick_slides = 0;
    for (std::size_t index = 0; index < built.size();)
    {
        if ((built[index].slide_flags & (64 | 128)) == 0)
        {
            ++index;
            continue;
        }
        const Fraction beat = built[index].global_beat;
        const bool upward = (built[index].slide_flags & 128) != 0;
        const auto notated_span = [](const BuiltNote& entry) {
            const Fraction span = entry.end_global_beat - entry.global_beat;
            return span.numerator > 0 ? span : g_minimum_slide_window;
        };
        // First pass over the onset: take the gesture's longest span and drop opposed directions.
        // Flags stay set here so the conversion pass can still find the survivors.
        Fraction span = notated_span(built[index]);
        std::size_t scan = index + 1;
        for (; scan < built.size() && built[scan].global_beat == beat; ++scan)
        {
            if ((built[scan].slide_flags & (64 | 128)) == 0)
            {
                continue;
            }
            if (((built[scan].slide_flags & 128) != 0) != upward)
            {
                ++conflicting_pick_slides;
                built[scan].slide_flags = 0;
                merged_away[scan] = true;
                continue;
            }
            if (span < notated_span(built[scan]))
            {
                span = notated_span(built[scan]);
            }
        }
        // Second pass: every surviving carrier at this onset becomes a scrape note in its own slot,
        // which keeps the stream's sort intact.
        for (std::size_t member = index; member < scan; ++member)
        {
            if ((built[member].slide_flags & (64 | 128)) == 0)
            {
                continue;
            }
            built[member].slide_flags = 0;
            BuiltNote& kept = built[member];
            ChartNote& note = kept.note;
            note.attack = NoteAttack::PickSlide;
            note.mute = NoteMute::None;
            note.harmonic_node.reset();
            note.vibrato = false;
            note.tremolo = false;
            note.accent = false;
            note.bend.clear();
            note.slide_out.reset();
            // Carriers are dead strings with meaningless frets, so the import owns the start too;
            // the editor's toggle keeps a real note's fret instead.
            note.fret = upward ? g_pick_slide_default_low_fret : g_pick_slide_default_high_fret;
            note.sustain = span;
            applyDefaultPickSlidePath(note, upward);
            kept.end_global_beat = kept.global_beat + span;
            ++imported_pick_slides;
        }
        index = scan;
    }
    if (imported_pick_slides > 0)
    {
        notes.push_back("imported " + std::to_string(imported_pick_slides) + " pick slides");
    }
    if (conflicting_pick_slides > 0)
    {
        notes.push_back(
            std::to_string(conflicting_pick_slides) +
            " conflicting simultaneous pick-slide directions kept the first");
    }

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
            // sustain ends at the glide end, floored at any INFORMATIVE payload the tie merge
            // folded past it (rule 2 — a repeated bend value or a hold waypoint pins nothing)
            // and kept strictly after the last chain waypoint (a degenerate gap glides through
            // half of it instead).
            Fraction window = gap - sustainMarginAt(grid, note.position);
            if (window.numerator <= 0)
            {
                window = gap * Fraction{1, 2};
            }
            const Fraction informative = lastChangingPayloadOffset(note);
            if (window < informative)
            {
                window = informative;
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
            // payload stays ascending. The four-fret exit is provisional: resolveSlideOutExits
            // rides the hand's next move instead when it agrees with the flag's direction.
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

    // The generator reads onsets, waypoint positions, and — for held-note detection at slide
    // waypoints — the notated (pre-trim) sounding spans, so it runs before the sustain policy on
    // purpose: the readability trim is a display concern, but where the fingers are planted is
    // governed by the notated holds, so pre-trim ends are the correct "still ringing" signal for
    // a reshape (a trimmed tail must never read as the finger lifting). Slide-in resolution needs
    // the placements and must transform its notes into ordinary slides before normalization
    // decides which tails a technique protects: a slide-in into a held landing keeps its hold,
    // trimmed like any tail but never dropped as effect-free.
    //
    // The generator runs on the natural stream — slide-ins still plain notes at their notated
    // positions — and the resolver then touches the placements only when a scoop's approach
    // leaves the active window: the window dips with the scoop for exactly its duration and the
    // natural window returns at the scoop's end; an approach the window already covers stays a
    // planted finger gesture, like an unpitched slide.
    chart.fret_hand_positions = generateFretHandPositions(built, tempo_map, phrase_boundary_beats);
    resolveSlideIns(built, chart.fret_hand_positions, grid, notes);
    if (!chart.fret_hand_positions.empty())
    {
        notes.push_back(
            "generated " + std::to_string(chart.fret_hand_positions.size()) +
            " fret-hand positions (phrase-aware; verify)");
    }

    // Runs after slide and slide-in resolution so slide-extended tails carry their payloads
    // into the trim's payload floor.
    normalizeImportedSustains(built, grid, notes);

    // Trail-off exits follow the hand's next move where it agrees; runs after the trim so
    // the compressed end positions are the ones the exit placements ride.
    resolveSlideOutExits(built, chart.fret_hand_positions, grid);

    // Shapes read the notated (pre-trim) note ends, so this runs on the built entries before
    // their notes move into the chart.
    deriveChordShapes(built, grid, chart);
    if (!chart.shapes.empty())
    {
        notes.push_back(
            "derived " + std::to_string(chart.shapes.size()) + " chord spans (" +
            std::to_string(chart.templates.size()) + " postures)");
    }

    if (unsupported_harmonics > 0)
    {
        notes.push_back(
            std::to_string(unsupported_harmonics) +
            " harmonics of unsupported types were imported without their harmonic");
    }
    if (semi_as_pinch > 0)
    {
        notes.push_back(
            std::to_string(semi_as_pinch) +
            " semi-harmonics were imported as pinch harmonics (the format does not distinguish "
            "them yet)");
    }
    if (implausible_natural_labels > 0)
    {
        notes.push_back(
            std::to_string(implausible_natural_labels) +
            " natural-harmonic labels matched no real node and were imported without their "
            "harmonic");
    }
    if (defaulted_fretted_nodes > 0)
    {
        notes.push_back(
            std::to_string(defaulted_fretted_nodes) +
            " stopped harmonics carried no usable node label and defaulted to the octave");
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
            phrase_boundary_beats.emplace_back(grid.first_global_beat[measure]);
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
