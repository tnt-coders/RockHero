#include "project/gp_chart_builder.h"

#include "chart/pick_slide_defaults.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/shared/ascii_case.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::Chart;
using common::core::ChartNote;
using common::core::Fraction;
using common::core::GridPosition;
using common::core::Keyframe;
using common::core::NoteAttack;
using common::core::NoteEmphasis;
using common::core::VibratoState;

// The payload helpers the importer's synthesis shares with the presentation rules in core
// (chart_presentation.h): one set of questions decides where a fabricated gesture may land and
// what a surface may draw, so the importer asks the shared authority rather than carrying a
// private twin of it.
using common::core::clipPayloadsTo;
using common::core::g_minimum_slide_window;
using common::core::informativePayloadEnd;
using common::core::keptAfterLastStatedFret;
using common::core::ringStateAt;

// One note event on the global rational beat axis, before tie merging. The grid position is
// derived from `global_beat` where a note needs one rather than carried beside it: the two are one
// fact in two coordinate systems, and gridPositionForGlobalBeat is the conversion.
struct NoteEvent
{
    Fraction global_beat{};    // onset on the global beat axis
    Fraction duration_beats{}; // how long the string rings, in the onset measure's beat unit
    GpNote source;
    bool tremolo{false};

    // How much ring an ornament took from this event, whether a following before-beat grace run
    // (rule 17) or the alternation a trill on this note spells out. Guitar Pro states a bend's
    // points as PERCENTAGES of the NOTATED duration, so the curve is mapped over the ring plus
    // this and then clipped back to the ring — squeezing it into the shortened ring would state a
    // curve nobody wrote.
    Fraction stolen_lead{};

    // The fretting hand takes this stop SILENTLY — the point record `NoteAttack::None` stores. No
    // gpif element states one, so the flag lives here rather than on GpNote: it is a fact the
    // IMPORT derives (a rolled chord's not-yet-sounded members are fingers already down), while
    // the score model states only what the file does. Such an event's source note carries its
    // string and its fret and nothing else, because that is all a silent hold is allowed to be.
    bool silent_hold{false};
};

// What the source notated for an event: what the string rings plus whatever an ornament stole.
[[nodiscard]] Fraction notatedDuration(const NoteEvent& event)
{
    return event.duration_beats + event.stolen_lead;
}

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
// margin-per-note rule the importer's span close and synthesized glide windows derive from (the
// presentation rules ask common/core for the same constant on the read side). Note
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

// The corpus-derived default scrape lives in the shared seam so import and the editor's attack
// verb synthesize identical defaults (pick_slide_defaults.h); the minimum gesture window sits one
// level down in common::core (grid_arithmetic.h), beside the other shared duration bounds, because
// the presentation rules floor on the same window.

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

// One point of a Guitar Pro bend curve on its way into the chart's ONE bend channel. Guitar Pro
// states a curve as four percent-anchored values; the chart states an onset amount plus a
// statement on each keyframe the curve moves at, so this is the intermediate the mapping below
// produces and \ref applyBendCurve folds in.
struct BendCurvePoint
{
    Fraction offset{};
    double semitones{0.0};
};

// Finds or creates the keyframe at `offset`, keeping the array ascending. Every statement at one
// instant shares ONE keyframe — that is the model's whole point — so a producer that would have
// written a second entry beside an existing moment merges into it instead.
[[nodiscard]] Keyframe& keyframeAt(std::vector<Keyframe>& keyframes, const Fraction offset)
{
    const auto at =
        std::ranges::lower_bound(keyframes, offset, std::ranges::less{}, &Keyframe::offset);
    if (at != keyframes.end() && at->offset == offset)
    {
        return *at;
    }
    return *keyframes.insert(
        at,
        Keyframe{
            .offset = offset,
            .fret = std::nullopt,
            .bend = std::nullopt,
            .vibrato = std::nullopt,
        });
}

// The offset of the last keyframe that states a bend, or zero — the onset, which always states
// one — when none does. Where the note's bend channel currently ends, which is what a tie or
// legato merge folds its own curve in strictly after.
[[nodiscard]] Fraction lastBendOffset(const ChartNote& note)
{
    Fraction last{};
    for (const Keyframe& keyframe : note.keyframes)
    {
        if (keyframe.bend.has_value())
        {
            last = keyframe.offset;
        }
    }
    return last;
}

// The offset of the FIRST keyframe that states a fret, or zero when none does — where the note's
// path starts travelling, which a fabricated slide-in must arrive before.
[[nodiscard]] Fraction firstStatedFretOffset(const ChartNote& note)
{
    for (const Keyframe& keyframe : note.keyframes)
    {
        if (keyframe.fret.has_value())
        {
            return keyframe.offset;
        }
    }
    return Fraction{};
}

// States a folded-in segment's Guitar Pro vibrato WIDTH at `offset` — the instant that segment
// BEGINS on the ring that absorbed it. The width flows through unchanged: `Off` is as much a
// statement here as either shake, because a segment that does not shake ends the one it folded
// into, and a segment shaking at the other tier steps the channel rather than restating it.
//
// Guitar Pro writes the mark per note and names no instant inside it, so the import picks one (the
// carried sign-off in `docs/plans/todo/unified-waypoint-model.md`): a merged note anchors it at the
// LAST keyframe. At a legato slide that keyframe is the junction the glide arrives at — where the
// folded segment begins and where a shake after a glide actually starts, which is the corpus's
// dominant figure (31 of its 34 slide-then-vibrato occurrences arrive through this merge); at a tie
// it is the continuation's own onset; and a note that merges nothing states its flag at the onset,
// which is what \ref ChartNote::vibrato already is. Spelled as the folded segment's own START
// rather than "whichever keyframe is last", because an origin's bend curve can legally run past
// the junction and the literal reading would then hand the shake to a bend point; in the figure
// the sign-off measures, the two readings name the same instant.
//
// Both halves of the `||` this replaces were lies: a folded segment's flag used to shake the whole
// ring from the onset, and a folded segment WITHOUT one used to inherit the shake it arrived after.
// A statement equal to the state already in force says nothing new and is not written, so a chain
// that shakes end to end still stores exactly the onset flag it always did.
void stateVibratoAt(ChartNote& note, const Fraction offset, const VibratoState vibrato)
{
    if (offset.numerator <= 0)
    {
        // A statement AT the onset is the channel's opening one — the same reading
        // \ref applyBendCurve gives a bend point there. Reachable, not defensive: two voices can
        // hold one string at one instant, and the tie merge is keyed by string alone, so an upper
        // voice's continuation can fold into a note that begins at the very same beat. The
        // offset-zero keyframe that would otherwise author is the one shape validation refuses
        // outright, and refusing costs the WHOLE song rather than the one junk pairing.
        note.vibrato = vibrato;
        return;
    }
    // The channel holds each statement until the next, so what the folded segment's flag has to
    // disagree with is the state IN FORCE where it begins — asked of the one authority
    // (chart.h), whose "a statement standing AT the instant counts" reading is what makes stating
    // one idempotent: two segments can fold onto a single offset (a tie continuation whose legato
    // glide lands on a second voice's note at that very beat), and the second must be able to
    // restate what the first said there, exactly as their shared keyframe's fret already takes
    // the later value.
    if (vibrato == ringStateAt(note, offset).vibrato)
    {
        return;
    }
    keyframeAt(note.keyframes, offset).vibrato = vibrato;
}

// The note's bend channel read back as the curve it draws: the onset value first, then every
// keyframe stating one. Empty for a note whose channel never leaves rest, so a merge folding this
// into a neighbour cannot author a flat zero statement the source never wrote.
[[nodiscard]] std::vector<BendCurvePoint> bendCurveOf(const ChartNote& note)
{
    if (!common::core::noteIsBent(note))
    {
        return {};
    }
    std::vector<BendCurvePoint> curve;
    curve.reserve(note.keyframes.size() + 1);
    curve.push_back(BendCurvePoint{.offset = Fraction{}, .semitones = note.bend});
    for (const Keyframe& keyframe : note.keyframes)
    {
        // Bound to a local so the optional check and the access are provably the same object.
        const std::optional<double>& bend = keyframe.bend;
        if (bend.has_value())
        {
            curve.push_back(BendCurvePoint{.offset = keyframe.offset, .semitones = *bend});
        }
    }
    return curve;
}

// Folds a bend curve into the note's bend channel: a point at the onset IS the note's own opening
// value (all a pre-bend ever was), and every later point becomes a bend statement on the keyframe
// at that instant, merged into whatever else already stands there.
void applyBendCurve(ChartNote& note, const std::vector<BendCurvePoint>& curve)
{
    for (const BendCurvePoint& point : curve)
    {
        if (point.offset.numerator <= 0)
        {
            note.bend = point.semitones;
            continue;
        }
        keyframeAt(note.keyframes, point.offset).bend = point.semitones;
    }
}

// Maps one GP bend onto the chart's [offset, semitones] pairs across the note sustain.
[[nodiscard]] std::vector<BendCurvePoint> buildBendPoints(
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
            return {BendCurvePoint{.offset = Fraction{}, .semitones = bend.origin_value / 50.0}};
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

    std::vector<BendCurvePoint> points;
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
        points.push_back(BendCurvePoint{.offset = offset, .semitones = semitones});
    }

    // A flat zero curve carries no information.
    const bool all_zero = std::ranges::all_of(
        points, [](const BendCurvePoint& point) { return std::is_eq(point.semitones <=> 0.0); });
    return all_zero ? std::vector<BendCurvePoint>{} : points;
}

// Classifies a track's part by a heuristic: four strings or a bass-named track become Bass, the
// first non-bass track becomes Lead, and the rest Rhythm. This is a stopgap — Guitar Pro tracks
// carry no Rock Hero part, so a robust import should let the user map each track to a part on
// import rather than guessing. Tracked in docs/plans/todo/gp-track-part-mapping.md.
[[nodiscard]] common::core::Part partForTrack(const GpTrack& track, bool first_track)
{
    const std::string lower_name = common::core::asciiLowered(track.name);
    if (track.tuning_midi.size() <= 4 || lower_name.find("bass") != std::string::npos)
    {
        return common::core::Part::Bass;
    }

    return first_track ? common::core::Part::Lead : common::core::Part::Rhythm;
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

// True when a position the builder DERIVED (rather than read from a beat) is a position the chart
// rules will accept. The measure always lands in range because the lookup above clamps it, but the
// beat does not: a global beat past the final bar's last beat — a trail-off or a scoop window
// running off the end of the score — yields a beat past that measure's own count, and a fabricated
// hand placement there fails validateChartRules and takes the WHOLE song's import down with it.
// Callers that fabricate furniture check this and simply omit the furniture: there is no board
// past the end of the song to put a hand on.
[[nodiscard]] bool withinGrid(const MeasureGrid& grid, const GridPosition& position)
{
    if (position.measure < 1 ||
        static_cast<std::size_t>(position.measure) > grid.beats_per_measure.size())
    {
        return false;
    }
    const auto measure_index = static_cast<std::size_t>(position.measure - 1);
    return position.beat >= 1 && position.beat <= grid.beats_per_measure[measure_index] &&
           position.offset.numerator >= 0 && position.offset < Fraction{1};
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
// strokes flow through positions, grace attachment, ties, and the tail rules exactly like
// hand-notated notes (the charting standard reserves the chart's `tremolo` for unmeasured
// noise, and Guitar Pro's tremolo is measured — the mark carries a precise stroke duration).
// Strokes re-pick: every stroke clears tie_destination, so a tie INTO the beat releases its
// origin when the first stroke's fresh onset lands, and only the last stroke keeps a notated
// onward tie so a ring-out continuation still binds. The first stroke keeps the emphasis and
// any hammer/pull arrival; later strokes are plain picks. A bent tremolo spells out too —
// each stroke samples the master curve at its own onset and carries the value as a flat
// prebend, so the run reads as progressively larger prebent picks.
// Only slide payloads keep the mark (per-stroke frets along a glide would be fabricated
// data, and the payloads include the pick-slide carriers), counted for the track report.
//
// A ROLL mark on a beat that splits is dropped from the strokes for that same reason, and the
// contradiction is settled HERE because this is where it becomes one: the mark says the grip is
// spread ONCE, and a stroke carrying it would state the fingers coming down again — a hand
// re-taking stops it never left, fabricated once per stroke out of a single notated act. The
// strokes strike together and the loss is counted. A beat the split declines (a slide payload, or
// one no longer than a stroke) keeps its mark and rolls normally.
[[nodiscard]] std::vector<GpBeat> expandTremoloBeats(
    const std::vector<GpBeat>& beats, int& kept_marks, int& dropped_rolls)
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
        dropped_rolls += beat.roll_direction != GpRollDirection::None ? 1 : 0;
        for (int index = 0; index < count; ++index)
        {
            GpBeat piece = beat;
            piece.tremolo_stroke = Fraction{};
            // The direction IS the roll mark, exactly as the stroke's numerator is the tremolo's,
            // so clearing it clears the mark; the spread and start time it leaves behind are the
            // settings of a mark that is no longer there, which is the shape the file itself has.
            piece.roll_direction = GpRollDirection::None;
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
                    // Later strokes are plain picks: the emphasis belongs to the stroke that was
                    // actually marked, and a ghosted run would be as wrong to repeat as an
                    // accented one.
                    note.emphasis = NoteEmphasis::Normal;
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

// The subdivision a spelled-out trill alternates at, as a fraction of a whole note. This is a
// knowing estimate the FORMAT forces rather than anything read from the score: gpif states only
// the auxiliary pitch, so a trill's speed is not a fact the file can supply (GP5's binary carried
// a period; gpif dropped it, and every reader of the format — alphaTab's included — assumes
// sixteenths, which is also the rate a trill is conventionally engraved at).
constexpr Fraction g_trill_step_whole{1, 16};

// Spells trilled notes out as the alternation the fretting hand actually performs: the note's own
// stop, the auxiliary, the note's stop again, one g_trill_step_whole apart for as long as the note
// rings. The NOTE-level sibling of expandTremoloBeats above — and note-level for the same reason
// that one is beat-level. Guitar Pro's tremolo marks a whole beat's picking hand, so the beat
// splits; a trill marks ONE note's fretting hand, so only that note's events multiply and a
// trilled note inside a chord alternates while the rest of the chord holds.
//
// The first note IS the source note: it keeps the attack and every mark the score wrote on the
// onset (emphasis, bend, vibrato, harmonic, slide). What it does not keep is the notated onward
// tie, which belongs to the last note of the run, exactly as a tremolo's strokes hand it along.
// Every continuation is the same hand hammering to the other stop and pulling back, so it carries
// only what the hands are still DOING — the palm and the dead mute — and claims legato. It states
// no direction: the resolver derives hammer or pull from the two frets, which is why an
// alternation needs nothing stored to read correctly in both directions.
//
// Runs after the collection loop rather than before it, so the alternation fills the ring the note
// ACTUALLY has: a following before-beat grace that stole part of it has already taken its lead.
// The first note remembers what the run took the same way that grace's victim does, because a
// bend's points are percentages of the duration the source NOTATED (`stolen_lead`).
//
// Refusals leave the source note exactly as it stands and are counted: a ring no longer than one
// step has nothing to alternate, and an auxiliary the hand cannot reach — below the capo'd open,
// off the board, or the note's own stop — is not a trill any hand could play. The capo'd open
// itself is fair game: the run pulls off to it and hammers back from it, which the resolver reads
// from the frets alone.
[[nodiscard]] std::vector<NoteEvent> expandTrilledEvents(
    const std::vector<NoteEvent>& events, const MeasureGrid& grid,
    const std::vector<int>& tuning_midi, const int capo, std::vector<std::string>& notes)
{
    std::vector<NoteEvent> expanded;
    expanded.reserve(events.size());
    int spelled_out = 0;
    int too_short = 0;
    int unreachable_auxiliaries = 0;
    for (const NoteEvent& event : events)
    {
        const GpNote& source = event.source;
        // Bound once so the presence test and the read below are provably the same object.
        const std::optional<int>& trill_value = source.trill_value;
        if (!trill_value.has_value() || source.string < 0 ||
            std::cmp_greater_equal(source.string, tuning_midi.size()))
        {
            // A note naming a string the tuning does not have has no open pitch to derive an
            // auxiliary fret from — and no lane to sound on either, so buildChart drops it with
            // its own count rather than this pass reporting the same loss twice.
            expanded.push_back(event);
            continue;
        }

        // Guitar Pro names the auxiliary by ABSOLUTE PITCH, so the fret it means is that pitch
        // above what the string sounds OPEN — which is the absolute fret the chart stores, the
        // capo cancelling from both sides of the subtraction. GpNote frets are capo-RELATIVE
        // (see buildChart's shift), so the auxiliary goes back into that numbering to replace one.
        // Widened to 64 bits because the value is an untrusted integer from the file; the bounds
        // below are what make narrowing it safe.
        const std::int64_t aux_absolute = static_cast<std::int64_t>(*trill_value) -
                                          tuning_midi[static_cast<std::size_t>(source.string)];
        const std::int64_t aux_fret = aux_absolute - capo;
        if (aux_absolute < capo || aux_absolute > common::core::g_max_fret ||
            aux_fret == source.fret)
        {
            ++unreachable_auxiliaries;
            expanded.push_back(event);
            continue;
        }

        // The step on this measure's signature-beat axis, which is the unit an event's duration is
        // carried in. The measure always resolves: gridPositionForGlobalBeat clamps into the grid.
        const GridPosition onset = gridPositionForGlobalBeat(grid, event.global_beat);
        const Fraction step =
            g_trill_step_whole *
            Fraction{grid.denominator[static_cast<std::size_t>(onset.measure - 1)]};
        const Fraction count_fraction =
            event.duration_beats * Fraction{step.denominator, step.numerator};
        const auto count = static_cast<int>(count_fraction.numerator / count_fraction.denominator);
        if (count <= 1)
        {
            // A ring no longer than one step sounds the note once; there is no alternation in it.
            ++too_short;
            expanded.push_back(event);
            continue;
        }

        NoteEvent principal = event;
        principal.duration_beats = step;
        principal.stolen_lead = notatedDuration(event) - step;
        principal.source.tie_origin = false;
        // The mark is consumed by the spell-out; leaving it would claim a trill still to expand.
        principal.source.trill_value.reset();
        expanded.push_back(std::move(principal));

        for (int index = 1; index < count; ++index)
        {
            const bool last = index + 1 == count;
            NoteEvent piece;
            piece.global_beat = event.global_beat + (Fraction{index} * step);
            // The last note absorbs the remainder, so the run's total ring equals the source's
            // exactly even where the span does not divide (dots and tuplets).
            piece.duration_beats =
                last ? event.duration_beats - (Fraction{count - 1} * step) : step;
            // Built rather than copied-and-cleared: what a hammered or pulled stop carries is a
            // short list, and stating it is what keeps a later GpNote field from silently joining
            // the run. The alternation itself is the fret, odd steps on the auxiliary.
            piece.source = GpNote{
                .string = source.string,
                .fret = index % 2 == 0 ? source.fret : static_cast<int>(aux_fret),
                .tie_origin = last && source.tie_origin,
                .hopo_destination = true,
                .palm_mute = source.palm_mute,
                .full_mute = source.full_mute,
                .harmonic_type = "",
            };
            expanded.push_back(std::move(piece));
        }
        ++spelled_out;
    }

    if (spelled_out > 0)
    {
        notes.push_back(
            std::to_string(spelled_out) +
            " trills were spelled out as legato alternation at sixteenths (the score states no "
            "trill speed)");
    }
    if (too_short > 0)
    {
        notes.push_back(
            std::to_string(too_short) +
            " trills rang no longer than one sixteenth and were left as single notes");
    }
    if (unreachable_auxiliaries > 0)
    {
        notes.push_back(
            std::to_string(unreachable_auxiliaries) +
            " trills named an auxiliary the note cannot alternate to and were left as single "
            "notes");
    }
    return expanded;
}

// Guitar Pro states a roll's spread in MIDI ticks at 480 to the quarter note, so 1920 to the
// whole — half the chart's own 1/3840-whole-note position lattice (fraction.h), which makes every
// whole number of ticks two whole quanta. A stagger reached by integer division of ticks
// therefore lands on the grid by construction and needs no rounding of its own.
constexpr int g_roll_ticks_per_whole{1920};

// The chart's own position lattice in the same unit, which states the 2:1 embedding above as a
// number rather than leaving it to the reader: a fractional tick still has a lattice line to
// round to, and only a partial slider value can ask for one.
constexpr int g_position_quanta_per_whole{2 * g_roll_ticks_per_whole};

// What one beat's roll spell-out did, which is what the caller counts: no figure at all, the
// figure where the score places it, or the figure forced onto its beat because the anticipation
// the score states had nowhere to start.
enum class RollSpread : std::uint8_t
{
    Refused,
    AsNotated,
    ClampedOnBeat,
};

// Spells a ROLLED beat out as the figure it states: one grip, sounded member by member. Guitar
// Pro's beat-level mark (engraving's vertical wavy line — this project's `arpeggio` means the
// span a chart is READ to imply, never this) says the hand is already holding every stop when the
// first string speaks, so the import writes exactly that. The first-sounded member is struck where
// the figure opens, with its own attack and marks; every member still to come gets a SILENT HOLD
// there, which is the one record for a stop the hand takes without sounding it; and each of those
// members' own onsets waits its turn over the stored spread, every one of them still ringing to
// the end its beat gave it. The derivation then reads one arpeggio span off those records with no
// new rule — silent members at the onset, each arrival answering its own claim and carrying the
// span on as a lone re-pick of a string the shape already holds.
//
// Works on the events one beat has just pushed, addressed by the RANGE they occupy, so the group
// is the language's own and never a key two beats at one instant could share. That is also what
// puts it after the staccato halving and any on-beat grace shift: a member's ring is whatever
// collection left it, and the figure only ever moves that ring's FRONT — the wait eats into it,
// the anticipation below gives it back — which is how the reference implementation times the
// stagger too (MidiFileGenerator.ts:974-978 adds the offset to the onset and subtracts it from
// every duration, so the members end together).
//
// Tied continuations are not members: nothing re-strikes them, and a claim on a string already
// ringing would be a finger coming down on its own sound (MidiFileGenerator.ts:2232-2238 leaves
// them out of the count the same way). The order is the stroke's rather than the naive reading of
// the file's word — see \ref GpRollDirection.
//
// Guitar Pro's SECOND roll slider, "Start time", places that figure against its beat: at 1 the
// first member is struck on it, and at 0 the roll ANTICIPATES — the LAST member lands on the beat
// and the figure opens a whole spread early. There is no reference implementation to copy, because
// every open-source reader (alphaTab and MuseScore included) ignores this property outright, so the
// recorded semantic is the linear reading of the tool's own two labelled endpoints. It is measured
// against the span the import actually WRITES — the staggers, after their truncating division —
// rather than the raw spread, which is what makes the 0 endpoint land the last member exactly on
// the beat even where that division lost a tick. The whole figure moves together, the claims with
// the first-sounded member, because the span opens where the hand takes the grip; the ENDS do not
// move, so an early member simply rings longer.
//
// Returns Refused, leaving the beat exactly as it stands for the caller to count, when the figure
// cannot be written at all: fewer than two members to spread, a spread the beat itself cannot
// contain, a stagger that rounds to nothing on the grid, or one that would leave a member no ring.
// Returns ClampedOnBeat when the figure is written but its stated anticipation had nowhere to open:
// before the song, or on a slot an earlier sounding already holds on the same string — which the
// same-string clamp cannot bound away, two notes at one (position, string) being a collision rather
// than an overlap.
[[nodiscard]] RollSpread expandRolledBeat(
    std::vector<NoteEvent>& events, const std::size_t first_event, const GpBeat& beat,
    const int denominator)
{
    std::vector<std::size_t> members;
    for (std::size_t index = first_event; index < events.size(); ++index)
    {
        if (!events[index].source.tie_destination)
        {
            members.push_back(index);
        }
    }
    if (members.size() < 2)
    {
        return RollSpread::Refused;
    }
    // A spread the beat cannot contain is no spread, and asking that before the arithmetic is what
    // bounds a junk value out of it: the ticks are a raw integer from the file.
    if (!(Fraction{beat.roll_spread_ticks, g_roll_ticks_per_whole} < beat.duration_whole))
    {
        return RollSpread::Refused;
    }

    const bool highest_first = beat.roll_direction == GpRollDirection::HighestFirst;
    std::ranges::sort(
        members, [&events, highest_first](const std::size_t lhs, const std::size_t rhs) {
            const int left = events[lhs].source.string;
            const int right = events[rhs].source.string;
            return highest_first ? left > right : left < right;
        });

    // The spread crosses the GAPS between members, so it divides by one less than their count and
    // the last member's onset is the spread's own end — truncating integer division, exactly as
    // MidiFileGenerator.ts:2308 does it.
    const int step_ticks = beat.roll_spread_ticks / (static_cast<int>(members.size()) - 1);
    if (step_ticks < 1)
    {
        return RollSpread::Refused;
    }
    const Fraction step = Fraction{step_ticks, g_roll_ticks_per_whole} * Fraction{denominator};
    for (std::size_t rank = 0; rank < members.size(); ++rank)
    {
        if (!(Fraction{static_cast<int>(rank)} * step < events[members[rank]].duration_beats))
        {
            // A member left with nothing to ring is not a member sounding late. The whole figure
            // is refused rather than written with a hole in it. Judged on the ON-BEAT placement,
            // which is the one an anticipation with nowhere to open falls back to, so whether a
            // roll is written at all never turns on the second slider.
            return RollSpread::Refused;
        }
    }

    // The slider is a raw float from the file — the reader hands back a NaN for the word "nan" —
    // so it is bounded to its own two endpoints before it scales anything, and a value that is no
    // number at all reads as the on-beat start.
    const double stated_start = beat.roll_start_time;
    const double anticipation =
        std::isnan(stated_start) ? 0.0 : std::clamp(1.0 - stated_start, 0.0, 1.0);
    // Rounded onto the chart's own lattice: a whole number of ticks is exact there, so only a
    // partial slider value ever has a fraction of a tick to place, and it places it on the nearest
    // line rather than off the grid.
    const int written_span_quanta = step_ticks * (static_cast<int>(members.size()) - 1) *
                                    (g_position_quanta_per_whole / g_roll_ticks_per_whole);
    const auto quanta =
        static_cast<int>(std::llround(anticipation * static_cast<double>(written_span_quanta)));
    Fraction shift = Fraction{quanta, g_position_quanta_per_whole} * Fraction{denominator};

    // Whether the figure may open that early. It may not reach back past the song's own start, and
    // it may not open on a slot an earlier sounding already holds on the same string: the clamp
    // that follows this pass bounds a ring RUNNING INTO a later onset, but two notes at one
    // (position, string) is a collision it has no bound for. Earlier events are every event already
    // pushed — this beat's own before-beat grace run included, which is how the two contend: the
    // ornament was placed first and already took its lead out of the beat before it, so a roll that
    // would reach into the ornament's own string yields rather than re-deciding a settled window.
    const auto opens_clear = [&events, &members, first_event](const Fraction back) {
        for (const std::size_t member : members)
        {
            if (events[member].global_beat - back < Fraction{})
            {
                return false;
            }
        }
        for (std::size_t index = 0; index < first_event; ++index)
        {
            const NoteEvent& earlier = events[index];
            for (const std::size_t member : members)
            {
                if (earlier.source.string == events[member].source.string &&
                    !(earlier.global_beat < events[member].global_beat - back))
                {
                    return false;
                }
            }
        }
        return true;
    };

    RollSpread placement = RollSpread::AsNotated;
    if (quanta > 0 && !opens_clear(shift))
    {
        shift = Fraction{};
        placement = RollSpread::ClampedOnBeat;
    }

    std::vector<NoteEvent> claims;
    claims.reserve(members.size() - 1);
    for (std::size_t rank = 0; rank < members.size(); ++rank)
    {
        NoteEvent& member = events[members[rank]];
        const Fraction wait = Fraction{static_cast<int>(rank)} * step;
        if (rank > 0)
        {
            // The finger is already down where the figure opened; only the string speaks late.
            // Built rather than copied and cleared, because a silent hold states its stop and
            // nothing else, and a GpNote field added later must not ride into one.
            NoteEvent claim;
            claim.global_beat = member.global_beat - shift;
            claim.source = GpNote{
                .string = member.source.string,
                .fret = member.source.fret,
                .harmonic_type = "",
            };
            claim.silent_hold = true;
            claims.push_back(std::move(claim));
        }

        // The grip is taken as one and released as one: the onset walks back by the anticipation
        // and forward by this member's own wait, while the end the beat stated stays exactly where
        // it is. An early member therefore rings longer, and a staccato member rings into the
        // halved end its own mark gave it.
        member.global_beat = member.global_beat - shift + wait;
        member.duration_beats = member.duration_beats + shift - wait;
    }
    // Appended only once the last member has been read, so the references above cannot be left
    // dangling by a reallocation.
    events.insert(events.end(), claims.begin(), claims.end());
    return placement;
}

// Collects the timed note events of one track across bars and voices. Grace beats take no time
// from the bar; each run attaches to the next sounding beat in its voice (the principal). A
// before-beat grace sounds a thirty-second-note lead ahead of the principal and STEALS that lead
// from the beat before it — Guitar Pro plays the ornament in the preceding note's time, so every
// event of the voice's previous sounding beat that would still be ringing at the run's first onset
// ends there instead. An on-beat grace sounds on the principal's position and delays the principal
// notes on its strings by the same lead (Guitar Pro's two grace placements). A lead shrinks to
// half the available gap when the neighboring onset sits closer than the full leads, and graces
// with no room at all are dropped.
//
// All three spell-outs happen here, each where its own input is final: tremolo beats split BEFORE
// collection (their strokes must flow through positions, graces and ties like hand-notated beats),
// a rolled beat's members stagger as that beat's events are pushed (the ring the stagger eats into
// is the one collection just gave them), and trilled notes spell out AFTER it (each alternation
// fills the ring the note is finally left with). The single sort at the end is what puts every
// fabricated onset back in stream order.
[[nodiscard]] std::vector<NoteEvent> collectEvents(
    const GpTrack& track, const MeasureGrid& grid, const int capo, std::vector<std::string>& notes)
{
    std::vector<NoteEvent> events;
    int overfull = 0;
    int dropped_graces = 0;
    int kept_tremolo_marks = 0;
    int rolls_on_tremolo = 0;
    int spread_rolls = 0;
    int unspread_rolls = 0;
    int clamped_anticipations = 0;

    // Tremolo beats spell out first; the expanded copies live for the whole collection
    // because pending grace runs hold beat pointers across bar boundaries.
    std::vector<std::vector<std::vector<GpBeat>>> expanded_bars(track.bars.size());
    for (std::size_t bar_index = 0; bar_index < track.bars.size(); ++bar_index)
    {
        for (const std::vector<GpBeat>& voice : track.bars[bar_index].voices)
        {
            expanded_bars[bar_index].push_back(
                expandTremoloBeats(voice, kept_tremolo_marks, rolls_on_tremolo));
        }
    }

    // The one place a source note's ring is established from what the beat states, which is why
    // the staccato halving lands here: Guitar Pro sounds a staccato note for exactly half its
    // stated duration, so the mark is duration truth and never a stored field. Per NOTE rather
    // than per beat, so a chord's single staccato member shortens alone; and before the trill
    // spell-out at the end of this function, so an alternation fills the ring the note actually
    // has. The removed half is NOT `stolen_lead`: no ornament took it and nothing sounds in it —
    // the note simply rings for half — so a bend's percentages map over the halved ring. A
    // tremolo beat's strokes were split before collection and each carry the mark, so a staccato
    // tremolo detaches every stroke, which is what the two marks together say.
    const auto emit_note = [&events](
                               const GpNote& source,
                               const bool tremolo,
                               const Fraction global,
                               const Fraction duration) {
        NoteEvent event;
        event.duration_beats = source.staccato ? duration * Fraction{1, 2} : duration;
        event.global_beat = global;
        event.source = source;
        event.tremolo = tremolo;
        events.push_back(std::move(event));
    };

    // The voice's previous sounding beat: where it landed, and the contiguous run of events it
    // pushed. A before-beat grace needs both — the onset floors the available gap, and the events
    // are what the run's lead is stolen from.
    struct SoundingBeat
    {
        Fraction onset{};
        std::size_t first_event{0};
        std::size_t event_count{0};
    };

    // Grace runs and conflict neighbors persist across bar lines within a voice, keyed by the
    // voice's index in its bar.
    std::map<std::size_t, std::vector<const GpBeat*>> pending_graces_per_voice;
    std::map<std::size_t, SoundingBeat> last_beat_per_voice;
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
                // This beat's own events start here; they are pushed contiguously, so the range
                // is what the NEXT beat's before-beat run steals its lead from.
                const std::size_t beat_first_event = events.size();
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
                        const auto last = last_beat_per_voice.find(voice_index);
                        const bool has_last = last != last_beat_per_voice.end();
                        const Fraction floor = has_last ? last->second.onset : Fraction{0};
                        const Fraction gap = principal_global - floor;
                        const Fraction lead = fitLeadToGap(full_lead, gap, before_count);
                        if (lead.numerator <= 0)
                        {
                            dropped_graces += before_count;
                        }
                        else
                        {
                            const Fraction first_onset =
                                principal_global - (Fraction{before_count} * lead);
                            // The run steals its lead from the beat before it: Guitar Pro sounds a
                            // before-beat grace in the preceding note's time, so that beat's
                            // events stop where the ornament starts rather than ringing under it.
                            // The lead was fitted strictly inside the gap above, so every
                            // shortened event keeps a positive duration. What was taken is
                            // remembered rather than just subtracted, because a bend's points are
                            // percentages of the duration the source NOTATED (`stolen_lead`).
                            // Only what is already sounding can yield: the gap is floored at the
                            // previous beat's STATED onset, and a rolled beat staggers members
                            // past it, so a member that has not spoken by the ornament's onset has
                            // no ring to give up and keeps the one the roll timed for it.
                            if (has_last)
                            {
                                for (std::size_t offset = 0; offset < last->second.event_count;
                                     ++offset)
                                {
                                    NoteEvent& earlier = events[last->second.first_event + offset];
                                    if (earlier.global_beat < first_onset &&
                                        earlier.global_beat + earlier.duration_beats > first_onset)
                                    {
                                        const Fraction rings = first_onset - earlier.global_beat;
                                        earlier.stolen_lead = notatedDuration(earlier) - rings;
                                        earlier.duration_beats = rings;
                                    }
                                }
                            }
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
                                        lead);
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
                                        lead);
                                    shifted_strings.push_back(grace_note.string);
                                }
                                ++slot;
                            }
                            principal_shift = Fraction{on_count} * lead;
                        }
                    }
                    pending.clear();
                }

                // Where this beat's OWN notes begin, which is the range the roll spell-out
                // addresses: a rolled beat's members are exactly these events, and naming them by
                // range is what keeps the grouping out of reach of any key two beats could share.
                const std::size_t first_own_event = events.size();
                for (const GpNote& source : beat.notes)
                {
                    const bool shifted = std::ranges::contains(shifted_strings, source.string);
                    emit_note(
                        source,
                        beat.tremolo_stroke.numerator > 0,
                        shifted ? (principal_global + principal_shift) : principal_global,
                        shifted ? (duration_beats - principal_shift) : duration_beats);
                }
                if (beat.roll_direction != GpRollDirection::None)
                {
                    const RollSpread placement =
                        expandRolledBeat(events, first_own_event, beat, denominator);
                    spread_rolls += placement != RollSpread::Refused ? 1 : 0;
                    unspread_rolls += placement == RollSpread::Refused ? 1 : 0;
                    clamped_anticipations += placement == RollSpread::ClampedOnBeat ? 1 : 0;
                }
                last_beat_per_voice[voice_index] = SoundingBeat{
                    .onset = principal_global + principal_shift,
                    .first_event = beat_first_event,
                    .event_count = events.size() - beat_first_event,
                };
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
    if (spread_rolls > 0)
    {
        notes.push_back(
            std::to_string(spread_rolls) +
            " rolled chords were spread into their stated stagger over a held grip");
    }
    if (unspread_rolls > 0)
    {
        notes.push_back(
            std::to_string(unspread_rolls) +
            " roll marks had no stagger their beat could hold and were left simultaneous");
    }
    if (clamped_anticipations > 0)
    {
        notes.push_back(
            std::to_string(clamped_anticipations) +
            " rolled chords had no room before their beat for the anticipation they state and were "
            "started on it (the song begins there, or an earlier sounding holds the slot)");
    }
    if (rolls_on_tremolo > 0)
    {
        notes.push_back(
            std::to_string(rolls_on_tremolo) +
            " tremolo-picked beats dropped their roll mark; the strokes strike together");
    }

    events = expandTrilledEvents(events, grid, track.tuning_midi, capo, notes);

    std::ranges::stable_sort(events, [](const NoteEvent& lhs, const NoteEvent& rhs) {
        if (lhs.global_beat != rhs.global_beat)
        {
            return lhs.global_beat < rhs.global_beat;
        }
        return lhs.source.string < rhs.source.string;
    });
    return events;
}

// Built notes plus their onset on the global beat axis (needed for tie and slide spans). The note
// carries everything else, including how long it rings — the builder stores ACTUAL durations, so
// an end kept beside the sustain would be a second copy of the same fact.
struct BuiltNote
{
    ChartNote note;
    Fraction global_beat{};
    int gp_string{0};
    int slide_flags{0};

    // Onset of the tied continuation the slide flags were inherited from, when they were: the
    // glide leaves from the junction, not the merged note's onset (policy rule 15).
    std::optional<Fraction> slide_from_beat;
};

// Where the string stops ringing, on the global beat axis.
[[nodiscard]] Fraction ringEndOf(const BuiltNote& entry)
{
    return entry.global_beat + entry.note.sustain;
}

// The stored stream lifted out of the build records: the notes exactly as they will ship. Both
// the same-string clamp and the presentation derivation speak about a note stream, so this is
// what they are handed. No scrape suppression is applied on the way out — the pick-slide
// conversion already stores its carriers in saved form, and nothing later re-adds a latent mark.
[[nodiscard]] std::vector<ChartNote> storedNotes(const std::vector<BuiltNote>& built)
{
    std::vector<ChartNote> notes;
    notes.reserve(built.size());
    for (const BuiltNote& entry : built)
    {
        notes.push_back(entry.note);
    }
    return notes;
}

// What the surfaces will draw from the stored stream, index-aligned with the build records. The
// two passes that ride readability — the trail-off's hand exit and the shape spans — read this
// rather than the actual rings behind it, so their output follows the picture the player sees.
[[nodiscard]] std::vector<ChartNote> presentedNotes(
    const std::vector<BuiltNote>& built, const common::core::TempoMap& tempo_map)
{
    return common::core::presentedChartNotes(storedNotes(built), tempo_map);
}

// The same-string clamp on the built stream (40-Q2-B): a re-strike stops the ring, so no stored
// tail crosses the next onset on its own string. Asked of the one authority in core rather than
// restated here, which is why the notes travel out and back — that authority speaks about a note
// stream, not about the builder's records.
void clampSameStringOverlaps(std::vector<BuiltNote>& built, const common::core::TempoMap& tempo_map)
{
    std::vector<ChartNote> stored = storedNotes(built);
    common::core::normalizeSustainOverlaps(stored, tempo_map);
    for (std::size_t index = 0; index < built.size(); ++index)
    {
        built[index].note = std::move(stored[index]);
    }
}

// A silence long enough to read as a phrase break: the hand re-anchors across it. 0.8s is the
// corpus sweet spot (4100-arrangement source-corpus study) — it holds the authored move rate
// (~13.2 anchors per 100 notes) while lifting exact anchor-fret agreement from 59% to 72%.
constexpr double g_fhp_phrase_rest_seconds = 0.8;

// The fret span of notes still ringing at a slide keyframe that are NOT themselves gliding there
// — each is a planted finger that pins the hand window's edge on its side. Returns false when no
// such note exists, so the slide is a genuine whole-hand travel (rule 9 drag) rather than a
// one-finger reshape. Taps float above the hand and open strings never anchor it, so both are
// excluded. A note that itself slid earlier is held at the fret it has reached; a note with a
// keyframe at this exact instant is a co-slider (its own event carries it, and a whole chord
// gliding in lockstep must translate, not reshape), so it is excluded too.
[[nodiscard]] bool heldHullAtSlideKeyframe(
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
        if (!(other.global_beat <= instant && instant < ringEndOf(other)))
        {
            continue; // not sounding at this instant
        }
        // A natural harmonic has no stop of its own, so its `fret` is 0 and its fretting hand is
        // at the NODE instead — reading `fret` here would drop a 12th-fret harmonic passage out
        // of hand derivation and leave the window at the nut.
        const int other_hand_fret = common::core::fretFor(other.note);
        if (common::core::rightHandOnset(other.note.attack) || other_hand_fret <= 0)
        {
            continue; // right-hand onsets float above the hand; open strings never anchor it
        }
        int fret = other_hand_fret;
        bool co_sliding = false;
        for (const Keyframe& keyframe : other.note.keyframes)
        {
            // Only a stated fret moves the hand; a keyframe carrying a bend or a vibrato change
            // says nothing about where this finger is and neither reaches nor co-slides.
            const std::optional<int>& stated_fret = keyframe.fret;
            if (!stated_fret.has_value())
            {
                continue;
            }
            const Fraction keyframe_beat = other.global_beat + keyframe.offset;
            if (keyframe_beat < instant)
            {
                fret = *stated_fret; // already reached this keyframe
                continue;
            }
            // Keyframes are ascending, so nothing past here can precede the instant. A keyframe
            // landing exactly on it means the note is gliding in lockstep — treat it as moving.
            co_sliding = keyframe_beat == instant;
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
//      drag). This reads the built notes' sounding spans — see heldHullAtSlideKeyframe — so the
//      generator is sustain-aware for held detection (see below).
// Scored against the corpus this reaches 72.5% exact anchor-fret agreement at the authored move
// rate. The maintained plain-English spec is "GP chart normalization policy" in
// docs/developer/the-project-lifecycle.md — tweak behavior there first, then re-align this code.
[[nodiscard]] std::vector<common::core::FretHandPosition> generateFretHandPositions(
    const std::vector<BuiltNote>& built, const common::core::TempoMap& tempo_map,
    const std::vector<Fraction>& phrase_boundary_beats, const int capo)
{
    // One instant the fret hand must cover: the fretted extent of an onset group, or a pitched
    // slide keyframe mid-sustain. A nonzero shift marks a slide keyframe carrying its fret delta
    // from the glide's source, which drags the anchor by that delta (rule 9) instead of being
    // fit like a struck onset. A reshape keyframe is a slide taken while another finger stays
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
                for (const Keyframe& keyframe : note.keyframes)
                {
                    // Only the POSITION channel announces a hand position: a bend or a vibrato
                    // change states nothing about where the hand sits, so it places no window.
                    //
                    // Bound to a local so the optional check and the access are provably the same
                    // object.
                    const std::optional<int>& stated_fret = keyframe.fret;
                    if (!stated_fret.has_value() || *stated_fret <= 0)
                    {
                        continue;
                    }
                    const int keyframe_fret = *stated_fret;
                    // An equal-fret keyframe is a HOLD, not a glide: nothing travels across it, so
                    // it announces no new hand position and must not place one. Letting it place
                    // one moves the window mid-note for no reason — a tie chain that holds a fret
                    // and then trails off would shift the hand at the hold, beats into the held
                    // note, instead of leaving it put until the slide itself moves it. The
                    // projection's ramp derivation draws the same distinction for the same reason
                    // (see slide_ramp_starts in highway_projection.cpp).
                    if (keyframe_fret == slide_source)
                    {
                        continue;
                    }
                    const Fraction keyframe_beat = built[onset_end].global_beat + keyframe.offset;
                    const GridPosition keyframe_position = common::core::advanceGridPosition(
                        tempo_map, note.position, keyframe.offset);
                    int held_min = 0;
                    int held_max = 0;
                    if (heldHullAtSlideKeyframe(
                            built, onset_end, keyframe_beat, held_min, held_max))
                    {
                        // A finger stays planted: the window reshapes to the exact sounding hull
                        // (held frets pin their edge, the slide carries the other) — no drag.
                        events.push_back(
                            CoverageEvent{
                                .global_beat = keyframe_beat,
                                .position = keyframe_position,
                                .min_fret = std::min(keyframe_fret, held_min),
                                .max_fret = std::max(keyframe_fret, held_max),
                                .shift = 0,
                                .reshape = true,
                            });
                    }
                    else
                    {
                        // Nothing else is held: the whole hand travels with the slide (rule 9).
                        events.push_back(
                            CoverageEvent{
                                .global_beat = keyframe_beat,
                                .position = keyframe_position,
                                .min_fret = keyframe_fret,
                                .max_fret = keyframe_fret,
                                .shift = slide_source > 0 ? keyframe_fret - slide_source : 0,
                            });
                    }
                    slide_source = keyframe_fret;
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

    // Keyframe events land mid-sustain, out of onset order, so the stream re-sorts before
    // same-instant events merge into one coverage demand (a keyframe coinciding with an onset
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
            // The window can never sit below the capo: its lowest legal anchor is the first
            // fret above it.
            const int lowest_anchor =
                std::max(common::core::firstPlayableFret(capo), event.max_fret - next_width + 1);
            // The floor wins if it ever crosses the covered extent. std::clamp is UB when its
            // low bound exceeds its high one, and the capo term makes that reachable in
            // principle: every guarantee that a covered fret sits above the capo (validation
            // refusing sub-capo notes, the import shift, E21 putting a node past the stop, the
            // skips for open strings and scrape travel) is external to this walk, and this walk
            // runs on untrusted files BEFORE validation.
            // The window must also FIT on the neck: a hand anchored high enough that its span runs
            // past the last fret describes frets that do not exist, and the 3D board would light
            // them. Where that would happen the hand sits LOWER instead, which is what a player
            // reaching the top of the neck actually does — the note stays inside the window either
            // way, at its top rather than its bottom. The outer max keeps the capo floor winning,
            // since std::clamp is UB when its low bound exceeds its high one.
            const int highest_anchor = std::max(
                lowest_anchor, std::min(event.min_fret, common::core::g_max_fret - next_width + 1));
            // At a boundary the hand re-places biased to the phrase's floor (the lowest fretted
            // note); otherwise it drags from the current anchor by the slide delta and clamps.
            next_anchor = reanchor
                              ? std::clamp(event.min_fret, lowest_anchor, highest_anchor)
                              : std::clamp(anchor + event.shift, lowest_anchor, highest_anchor);
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
// covers covered_fret on the neck; floor_fret is the lowest legal anchor (fret 1, or the first
// fret above the capo).
[[nodiscard]] int windowAnchorCovering(
    const common::core::FretHandPosition& active, const int travel, const int covered_fret,
    const int floor_fret)
{
    const int lowest = std::max(floor_fret, covered_fret - active.width + 1);
    // The window has to cover the fret AND fit on the neck: an anchor whose span runs past the last
    // fret names frets that do not exist, which near the top of the neck simply means the hand sits
    // lower and covers the fret at its top instead. The low bound wins if the two ever cross — the
    // capo floor is not negotiable, and std::clamp is UB with a low bound above its high one.
    const int highest =
        std::max(lowest, std::min(covered_fret, common::core::g_max_fret - active.width + 1));
    return std::clamp(active.fret + travel, lowest, highest);
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
// before anything reads the stream, so the transformed note is already a slide when the tail rules
// judge it: a slide-in into a held landing keeps its hold like any notated slide.
void resolveSlideIns(
    std::vector<BuiltNote>& built, std::vector<common::core::FretHandPosition>& placements,
    const MeasureGrid& grid, std::vector<std::string>& notes, const int capo)
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
        // The approach is a pressed position, so its floor is the first fret above the capo.
        start = std::clamp(start, common::core::firstPlayableFret(capo), common::core::g_max_fret);
        if (start == note.fret)
        {
            // Sliding into the neck's lowest playable fret from below (or the last fret from
            // above): no start exists.
            ++unplaceable;
            continue;
        }

        // The scoop window (see the function comment); an existing chain keyframe keeps the
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
        if (const Fraction first_travel = firstStatedFretOffset(note);
            first_travel.numerator > 0 && window >= first_travel)
        {
            window = first_travel * Fraction{1, 2};
        }
        // A slide-out is the other fret-travel payload the scoop must stay strictly before:
        // on a short note the floored window can reach the trail-off, which ends the ring, and a
        // stated fret at or past that end fails chart validation.
        if (note.slide_out.has_value() && window >= note.sustain)
        {
            window = note.sustain * Fraction{1, 2};
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
                const int dip_anchor = windowAnchorCovering(
                    *active, start - note.fret, start, common::core::firstPlayableFret(capo));
                dip_placements.push_back(
                    common::core::FretHandPosition{
                        .position = note.position,
                        .fret = dip_anchor,
                        .width = active->width,
                    });
                // The restore rides the scoop's end, which can run off the end of the score; a
                // placement outside the grid would fail validation for the whole song, and the
                // dip alone is a coherent state (the window simply stays where the scoop left it).
                if (const GridPosition restore_position =
                        gridPositionForGlobalBeat(grid, entry.global_beat + window);
                    withinGrid(grid, restore_position))
                {
                    restore_placements.push_back(
                        common::core::FretHandPosition{
                            .position = restore_position,
                            .fret = active->fret,
                            .width = active->width,
                        });
                }
            }
        }

        // The arrival is a fret STATEMENT at the scoop's end, merged into whatever already stands
        // at that instant rather than inserted beside it — a bend the source wrote there is the
        // same moment, not a competing one.
        keyframeAt(note.keyframes, window).fret = note.fret;
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
// trail-off ending at or past the next onset stays planted (no room to ride).
//
// The gesture it rides is the DRAWN one, so every question here — where the trail-off ends, which
// fret it leaves from, whether it still clears the next onset — is asked of the presented note,
// while the resolved exit fret is written into the stored one (presentation compresses a
// trail-off's end but never drops it, so the stored gesture is always there to write to).
void resolveSlideOutExits(
    std::vector<BuiltNote>& built, const std::vector<ChartNote>& presented,
    std::vector<common::core::FretHandPosition>& placements, const MeasureGrid& grid,
    const int capo)
{
    std::vector<common::core::FretHandPosition> exit_placements;
    std::vector<common::core::FretHandPosition> restore_placements;
    for (std::size_t index = 0; index < built.size(); ++index)
    {
        BuiltNote& entry = built[index];
        const ChartNote& note = presented[index];
        // A scrape's slide-out is authored travel, not a trail-off exit to resolve — and the
        // scrape never anchors the hand, so there is no placement to ride. Both forms of the
        // gesture are required up front: the drawn one is what the window rides, the stored one
        // is what the resolved exit fret is written back into. They always agree — presentation
        // compresses a trail-off's end and never drops it — so the second test costs nothing and
        // makes the write below provably safe rather than safe by argument.
        const int* const drawn = common::core::slideOutFretOrNull(note);
        if (drawn == nullptr || !entry.note.slide_out.has_value() || isScrape(note.attack))
        {
            continue;
        }
        const int departing = common::core::releasedFret(note);
        const bool downward = *drawn < departing;
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
        // The gesture ends where the DRAWN ring does, which is what a slide-out having no offset
        // of its own means: presentation compresses that end, and the trail-off comes with it.
        const GridPosition end_position =
            gridPositionForGlobalBeat(grid, entry.global_beat + note.sustain);
        if (!withinGrid(grid, end_position))
        {
            // The trail-off ends past the last bar (a hold-exempt ring presentation never
            // compressed, at the very end of the score). A placement there is not representable,
            // and fabricating one failed validation for the whole song; the gesture keeps its
            // default exit fret and the hand simply stays put, which is what happens anyway when
            // there is no room to ride.
            continue;
        }
        const bool has_next = next_note < built.size();
        const bool has_room = !has_next || end_position < built[next_note].note.position;
        if (has_next && !has_room)
        {
            // The gesture reaches the next onset (a hold-exempt trail-off presentation never
            // compressed, or a crush to exactly the gap): no room to ride, so the whole
            // gesture stays planted — default exit fret, no fabricated placements.
            continue;
        }

        // Departure: the next placement's move serves the very next onset and agrees with
        // the trail-off's direction, so the window flows onward instead of returning.
        const int delta = after == placements.end() ? 0 : after->fret - active->fret;
        const bool departs = delta != 0 && (delta < 0) == downward && has_next &&
                             !(built[next_note].note.position < after->position);
        int exit_fret = *drawn;
        if (departs)
        {
            const int travel = widenedToMinimumTravel(delta, downward);
            exit_fret = std::clamp(
                departing + travel,
                common::core::firstPlayableFret(capo),
                common::core::g_max_fret);
            // The resolved fret is the note's, not the picture's: it is stored, and the presented
            // stream is derived again from it.
            entry.note.slide_out = exit_fret;
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
        // keep the exit fret covered on the neck — never below the capo, where no hand can sit.
        const int anchor = windowAnchorCovering(
            *active, exit_fret - departing, exit_fret, common::core::firstPlayableFret(capo));
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
// slide-keyframe positions on the musical grid.
[[nodiscard]] Chart buildChart(
    const GpTrack& track, const MeasureGrid& grid, const common::core::TempoMap& tempo_map,
    const std::vector<Fraction>& phrase_boundary_beats, std::vector<std::string>& notes)
{
    Chart chart;
    // Every value below arrives unvalidated from the score file, and each one the chart rules
    // bound. Import is a commit point, so an out-of-range value is reduced and reported rather than
    // allowed to reach validation, where it would refuse the WHOLE song over one field.
    for (const int midi : track.tuning_midi)
    {
        if (static_cast<int>(chart.tuning.strings.size()) >= common::core::g_max_chart_strings)
        {
            // The model speaks about eight strings, so a wider instrument loses its extra courses
            // and the notes on them (dropped below, where a note names a string the tuning lacks).
            notes.push_back(
                "track declares more than " + std::to_string(common::core::g_max_chart_strings) +
                " strings; the extra ones and their notes are dropped");
            break;
        }
        chart.tuning.strings.push_back(midiNoteName(midi));
    }
    chart.tuning.capo = std::clamp(track.capo, 0, common::core::g_max_capo);
    if (chart.tuning.capo != track.capo)
    {
        notes.push_back(
            "capo at fret " + std::to_string(track.capo) + " is out of range and reads as " +
            std::to_string(chart.tuning.capo));
    }

    const std::vector<NoteEvent> events = collectEvents(track, grid, chart.tuning.capo, notes);

    std::vector<BuiltNote> built;
    std::map<int, std::size_t> open_note_per_string;
    int dropped_duplicates = 0;
    int notes_off_the_instrument = 0;
    int frets_off_the_neck = 0;
    int nodes_off_the_string = 0;
    int strikeless_taps = 0;
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
                if (event_end > ringEndOf(origin))
                {
                    origin.note.sustain = event_end - origin.global_beat;
                }
                // Where this continuation begins on the merged ring: the anchor its own
                // per-segment statements rebase onto, for the vibrato channel and the bend curve
                // alike.
                const Fraction base = event.global_beat - origin.global_beat;
                stateVibratoAt(origin.note, base, source.vibrato);
                // Tremolo is deliberately NOT a channel: it is re-picking, so a mid-ring "start"
                // would be new onsets rather than a state change (the keyframe model's admission
                // rule). A tied segment that re-picks makes the whole merged ring tremolo.
                origin.note.tremolo = origin.note.tremolo || event.tremolo;
                if (source.bend.has_value())
                {
                    for (BendCurvePoint point :
                         buildBendPoints(*source.bend, notatedDuration(event), notes))
                    {
                        if (event.duration_beats < point.offset)
                        {
                            // The curve is written over the notated duration; a continuation an
                            // ornament shortened folds in only the part that still sounds. Points
                            // ascend, so nothing after this one survives either.
                            break;
                        }
                        point.offset = point.offset + base;
                        // Strictly after where the origin's bend channel already ends. The onset
                        // counts as offset zero, which is why an origin stating no bend at all
                        // still admits every rebased point: a continuation begins strictly after
                        // its origin's onset.
                        if (point.offset > lastBendOffset(origin.note))
                        {
                            keyframeAt(origin.note.keyframes, point.offset).bend = point.semitones;
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
                continue;
            }
        }

        // A note naming a string the tuning does not have cannot be placed at all: the lane it
        // belongs on does not exist. Dropped and counted, rather than carried to validation, which
        // would refuse the song.
        if (source.string < 0 || std::cmp_greater_equal(source.string, chart.tuning.strings.size()))
        {
            ++notes_off_the_instrument;
            continue;
        }

        BuiltNote entry;
        entry.global_beat = event.global_beat;
        entry.gp_string = source.string;
        entry.slide_flags = source.slide_flags;

        ChartNote& note = entry.note;
        note.position = gridPositionForGlobalBeat(grid, event.global_beat);
        note.string = source.string + 1;
        // Guitar Pro's frets are CAPO-RELATIVE (confirmed by authored experiment: with a capo at
        // 3, an entered "1" sounds the pitch at absolute fret 4), while the chart stores absolute
        // frets with 0 meaning the open string, capo'd or not. So a fretted note shifts by the
        // capo and the open string stays 0. Clamped to the neck the model speaks about, which the
        // bound's own definition says import shares: a capo'd score can name a fret past the last
        // one, and that is junk rather than an instrument we do not know about.
        // Widened before the shift: the source fret is an untrusted raw integer, and adding the
        // capo to INT_MAX in int is undefined before the clamp below could ever see it.
        const std::int64_t shifted_fret =
            source.fret > 0 ? static_cast<std::int64_t>(source.fret) + chart.tuning.capo : 0;
        note.fret =
            static_cast<int>(std::min<std::int64_t>(shifted_fret, common::core::g_max_fret));
        if (shifted_fret > common::core::g_max_fret)
        {
            ++frets_off_the_neck;
        }
        note.sustain = event.duration_beats;
        note.vibrato = source.vibrato;
        note.tremolo = event.tremolo;
        note.emphasis = source.emphasis;

        if (source.left_hand_tapped)
        {
            // The fretting hand striking the note from nowhere, stored verbatim as the intent the
            // score states. It also gives the right downstream behavior automatically — the note
            // anchors the fret hand, closes chord spans, and never floats above the window, all of
            // which are Tap-attack special cases. Checked before the generic tap: a note carrying
            // both marks is a left-hand tap, the more specific articulation.
            note.attack = NoteAttack::LeftTap;
        }
        else if (source.tapped)
        {
            note.attack = NoteAttack::Tap;
        }
        else if (source.hopo_destination)
        {
            // The score says these notes connect but not which way — which is exactly what the
            // stored claim says, so the import needs no direction and invents none. The junk flags
            // real scores carry (a hopo mark with nothing before it, or with a predecessor at the
            // same stop) are settled by the sweep at the end of the build, where the slide chains
            // that decide a predecessor's RELEASED fret are finally built.
            note.attack = NoteAttack::Legato;
        }

        // Both marks carried through independently, because the score states them independently:
        // a dead string inside a palm-muted chord wears "Muted" and "PalmMuted" at once, and the
        // old single mute axis had to drop one of them (the palm one) to fit.
        note.palm_mute = source.palm_mute;
        note.dead = source.full_mute;

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
                // The stop the harmonic speaks from — the note's (already capo-shifted,
                // absolute) fret, or the capo when the string is open — asked of the same
                // authority E21 validates against.
                const int stop_fret = common::core::physicalStopFret(note, chart.tuning.capo);
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
                // The node has to be ON the note before the ceiling is asked, because which ceiling
                // binds depends on whether the note is a harmonic at all.
                const double proposed_node = static_cast<double>(stop_fret) + offset;
                note.harmonic_node = proposed_node;
                if (proposed_node > common::core::harmonicNodeCeiling(note))
                {
                    // A label naming a node this note cannot reach is junk, not data, so the
                    // octave takes over — the same fallback a missing label gets, and the
                    // lowest-order harmonic available at any stop. It always fits: a fret-hand
                    // stop is the capo, so at most g_max_capo + 12 against the neck's g_max_fret,
                    // and any other stop is at most g_max_fret, so g_max_fret + 12 against the
                    // string's g_max_harmonic_node.
                    note.harmonic_node = static_cast<double>(stop_fret) + 12.0;
                    ++nodes_off_the_string;
                }
            }
            else if (source.harmonic_type == "Natural")
            {
                // A natural has no stop of its own — the string speaks from the nut or the capo,
                // and the node carries the position, so `fret` never doubles as a rounded copy
                // of it. The label (or, absent one, the SOURCE fret, which for a natural IS the
                // touched position in GP's capo-relative frame) resolves against an open string
                // and lands on the real stop — capo + offset, which is exactly right now that
                // GP's frame is confirmed capo-relative.
                const double notated =
                    source.harmonic_fret.value_or(static_cast<double>(source.fret));
                const double offset =
                    common::core::snapHarmonicNode(notated, common::core::g_max_snapped_partial);
                if (std::abs(offset - notated) <= plausible_label_error)
                {
                    // The node is an absolute position (measured from the physical stop), while
                    // the stored fret follows the 0-means-open convention: 0 IS the capo'd open
                    // string, so the capo never appears as a fret number.
                    const double proposed_node = static_cast<double>(chart.tuning.capo) + offset;
                    note.harmonic_node = proposed_node;
                    note.fret = 0;
                    if (proposed_node > common::core::harmonicNodeCeiling(note))
                    {
                        // A natural's node carries the fretting finger, so the neck is its ceiling:
                        // high partials crowd toward the nut but their bridge-side alternates climb
                        // past the last fret, and a capo pushes every one of them further up. The
                        // octave takes over, as it does for a missing label.
                        note.harmonic_node = static_cast<double>(chart.tuning.capo) + 12.0;
                        ++nodes_off_the_string;
                    }
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

        // A tap needs somewhere to strike (E4), whichever hand delivers it: a fret, or a harmonic's
        // node. A `Tapped` or `LeftHandTapped` flag on an open string with no node is junk data,
        // and it has to be settled HERE rather than at the end of the build — the chord-shape and
        // hand-window passes read the attack, and they treat a two-hand tap as a picking-hand onset
        // that anchors nothing and closes no span, so a song would be shaped around an attack that
        // cannot survive validation. It needs no context at all, which is why it can be decided
        // this early where the relational settle cannot. Nothing later can fix it either: the
        // legato sweep never touches a local claim, exactly because no neighbour can withdraw one.
        if (common::core::flattenStrandedStrike(note))
        {
            ++strikeless_taps;
        }

        if (source.bend.has_value())
        {
            // Guitar Pro writes the curve as percentages of the NOTATED duration, so it is laid
            // out over that and then clipped to the ring: a note an ornament shortened loses the
            // part of its bend that no longer sounds instead of playing the whole curve faster.
            applyBendCurve(note, buildBendPoints(*source.bend, notatedDuration(event), notes));
            clipPayloadsTo(note, note.sustain);
        }

        if (event.silent_hold)
        {
            // A stop the fretting hand takes without sounding it — the rolled chord's fingers
            // already down when the first string speaks. It is a POINT record of slot, string and
            // stop, which is exactly what savedChartNote leaves of one, so the shape is asked of
            // that authority rather than assembled by declining to set the fields above: a
            // technique added to ChartNote later is then refused here without anyone remembering
            // to refuse it (chart.h, NoteAttack::None). The claim still travels the whole mapping
            // first so it shares the string check, the capo shift and the duplicate rule with
            // every other note; what it carries into that mapping is a bare string and fret.
            note.attack = NoteAttack::None;
            note = common::core::savedChartNote(note);
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
        built.push_back(std::move(entry));
    }

    // Slides resolve against the next onset on the same string, so they run after every onset
    // exists. A shift slide (flag 1) glides toward a re-picked target that keeps its own onset
    // and head. A legato slide (flag 2) is a continuation of the same note: the target is not
    // re-picked, so it folds into the origin as a pitched keyframe at the junction — the sustain
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
    // are drawn under the ordinary minimum-distance rules like any note.
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
            return entry.note.sustain.numerator > 0 ? entry.note.sustain : g_minimum_slide_window;
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
            // The suppression set lives in savedChartNote alone. Restating it here had already
            // drifted from it: this cleared the emphasis too, but an accented scrape is legal and
            // meaningful — an aggressively played one (H3/D4) — so a mark the score made was
            // silently discarded on import.
            note = common::core::savedChartNote(note);
            // Carriers are dead strings with meaningless frets, so the import owns the start too;
            // the editor's toggle keeps a real note's fret instead. The start is floored above the
            // capo like every fret a slide gesture names (user ruling 2026-08-20, which closed
            // W9-J: a scrape's start, its turnarounds, and its terminal all sit at or above the
            // first playable fret — the pick travels the sounding string, and a scrape at the nut
            // is no scrape). The default path floors its own terminal the same way.
            note.fret = upward ? pickSlideDefaultLowFret(chart.tuning.capo)
                               : std::max(
                                     common::core::firstPlayableFret(chart.tuning.capo),
                                     g_pick_slide_default_high_fret);
            note.sustain = span;
            applyDefaultPickSlidePath(note, upward, chart.tuning.capo);
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
        // note's onset: a hold keyframe pins the pitch until the sliding segment begins (the
        // tied 6 holds through its chord, then slides — policy rule 15).
        if (entry.slide_from_beat.has_value())
        {
            const Fraction hold_offset = *entry.slide_from_beat - entry.global_beat;
            if (hold_offset.numerator > 0)
            {
                keyframeAt(note.keyframes, hold_offset).fret = note.fret;
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
                // A silent hold is not a landing: nothing re-picks the string there, so a glide
                // cannot arrive at one and a legato chain must not swallow one. The same law
                // \ref sustainBoundOf applies on the read side, asked here of the model's own
                // predicate so the two cannot read a silent hold differently — this walk is over
                // the builder's records rather than over a note stream, so it cannot ask that
                // function itself.
                if (built[follower].gp_string == entry.gp_string && !merged_away[follower] &&
                    !common::core::silentHold(built[follower].note.attack))
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
            if (next->note.fret == 0)
            {
                // The landing is the open string: nothing is pressed to glide to, and a keyframe
                // at fret 0 is refused (user rule 2026-08-20), so the gesture degrades to the
                // unpitched trail-off exactly like a missing landing — which is what a slide
                // down toward the open string physically is. The landing keeps its own onset.
                flags |= 4;
                break;
            }
            const Fraction gap = next->global_beat - entry.global_beat;
            if ((flags & 2) != 0 && (flags & 1) == 0)
            {
                // Legato: the landing continues this note. Keyframe at the junction, sustain
                // through the target's notated end, techniques folded, chain continued.
                keyframeAt(note.keyframes, gap).fret = next->note.fret;
                if (ringEndOf(entry) < ringEndOf(*next))
                {
                    note.sustain = ringEndOf(*next) - entry.global_beat;
                }
                // The landing's shake lands ON the junction it arrives at — the same coupling the
                // bend fold below relies on, and the sign-off's anchor for Guitar Pro's anchorless
                // flag. A landing that does NOT shake ends the origin's shake there just as
                // honestly, and where the two agree the channel says nothing at all.
                stateVibratoAt(note, gap, next->note.vibrato);
                note.tremolo = note.tremolo || next->note.tremolo;
                // The merged note's own bend curve, rebased onto the junction. Its onset value
                // lands ON the junction keyframe, which is the coupling the model exists for: the
                // fret it glides to and the push it arrives with are one moment, not two.
                for (BendCurvePoint point : bendCurveOf(next->note))
                {
                    point.offset = point.offset + gap;
                    if (point.offset > lastBendOffset(note))
                    {
                        keyframeAt(note.keyframes, point.offset).bend = point.semitones;
                    }
                }
                merged_away[next_index] = true;
                glide_fret = next->note.fret;
                flags = built[next_index].slide_flags;
                search_from = next_index;
                continue;
            }

            // Shift: an ordinary pitched keyframe glides to the re-picked landing's fret and
            // ARRIVES the minimum-sustain-distance margin before the landing's onset (policy rule
            // 13); the landing keeps its own onset and head. Guitar Pro states no arrival time, so
            // the offset is synthesized here — floored at any INFORMATIVE payload the tie merge
            // folded past it (a repeated bend value or a hold keyframe pins nothing) and kept
            // strictly after the last chain keyframe (a degenerate gap glides through half of it
            // instead).
            //
            // The arrival ends the gesture's information but not the note: the origin keeps
            // ringing until the landing re-picks the string, so the sustain only ever GROWS to
            // reach the arrival, and the same-string clamp is what bounds it at the landing. What
            // the surfaces draw comes from the presentation rules, which trim this ring back to
            // exactly this arrival — the reason the old assignment here looked like the answer.
            //
            // Payload past the arrival still goes, and that clip is NOT a presentation trim
            // leaking into the importer: the arrival is where this synthesized gesture ends, and
            // rule 2 floors a drawn tail on the last CHANGING payload point. A bend point left
            // past the arrival would therefore re-float the drawn tail onto the landing's own
            // onset — a pitched tail holding the landing's fret up to its head, which is exactly
            // what the informative floor above declines to do when the information reaches the
            // landing. What the note keeps is what the gesture can still say.
            Fraction window = gap - sustainMarginAt(grid, note.position);
            if (window.numerator <= 0)
            {
                window = gap * Fraction{1, 2};
            }
            const Fraction informative = informativePayloadEnd(note);
            // The floor yields to the LANDING, which it does nowhere else: a pitched keyframe may
            // not sit on a later onset of its own string (that encoding stores no coordinates,
            // which is what keeps it undesyncable), and past the onset the glide would be holding
            // the landing's own fret. Where the folded payload reaches that far, the arrival keeps
            // the margin instead of the information.
            if (window < informative && informative < gap)
            {
                window = informative;
            }
            window = keptAfterLastStatedFret(note, window);
            if (!(window < gap))
            {
                // The chain's own keyframes already fill the gap, so there is nowhere left to
                // arrive before the landing sounds. The glide cannot be a pitched arrival at all
                // and degrades to the unpitched trail-off the no-landing case uses.
                flags |= 4;
                break;
            }
            keyframeAt(note.keyframes, window).fret = next->note.fret;
            if (note.sustain < window)
            {
                // A ring shorter than the glide cannot carry its own arrival keyframe; the note
                // sounds while it travels.
                note.sustain = window;
            }
            clipPayloadsTo(note, window);
            if (note.slide_out.has_value() && window < note.sustain)
            {
                // A trail-off the chain resolved earlier cannot outlive the gesture it trails off
                // from; the arrival is the gesture's end now.
                note.slide_out.reset();
            }
            flags = 0;
            break;
        }

        if ((flags & (4 | 8)) != 0)
        {
            const bool upward = (flags & 8) != 0;
            // Four frets of travel, held onto the playable board at both ends: the floor is the
            // first fret above the capo, never the nut — an exit below the floor was a form the
            // rules refuse, produced here and caught only at the track's validation.
            const int target =
                upward
                    ? std::min(glide_fret + 4, common::core::g_max_fret)
                    : std::max(glide_fret - 4, common::core::firstPlayableFret(chart.tuning.capo));
            // The slide-out ends the RING, so what the gesture needs is a ring end strictly after
            // any chain keyframe's stated fret — otherwise the trail-off would leave from a
            // position stated at the very instant it ends. The four-fret exit is provisional:
            // resolveSlideOutExits rides the hand's next move instead when it agrees with the
            // flag's direction. The answer is strictly positive without a floor of its own: a
            // sustainless note's zero never exceeds the last stated fret's offset, so it takes the
            // bumped branch and comes back a whole minimum window.
            const Fraction ring_end = keptAfterLastStatedFret(note, note.sustain);
            if (note.sustain < ring_end)
            {
                note.sustain = ring_end;
            }
            note.slide_out = target;
        }
    }

    // Legato landings are no longer onsets; drop them before fret-hand generation, the same-string
    // clamp, and chord derivation see the stream.
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
    if (notes_off_the_instrument > 0)
    {
        notes.push_back(
            std::to_string(notes_off_the_instrument) +
            " notes named a string the tuning does not have and were dropped");
    }
    if (frets_off_the_neck > 0)
    {
        notes.push_back(
            std::to_string(frets_off_the_neck) + " notes sat past fret " +
            std::to_string(common::core::g_max_fret) +
            " once shifted by the capo and were pulled "
            "back to it");
    }
    if (nodes_off_the_string > 0)
    {
        notes.push_back(
            std::to_string(nodes_off_the_string) +
            " harmonics named a node the note cannot reach and defaulted to the octave");
    }
    if (strikeless_taps > 0)
    {
        notes.push_back(
            std::to_string(strikeless_taps) +
            " tapped open strings had nothing to strike and read as plain picks");
    }

    // The generator reads onsets, keyframe positions, and — for held-note detection at slide
    // keyframes — the sounding spans, which the stored rings now simply are. It runs on the
    // natural stream, slide-ins still plain notes at their notated positions, and the resolver
    // then touches the placements only when a scoop's approach leaves the active window: the
    // window dips with the scoop for exactly its duration and the natural window returns at the
    // scoop's end; an approach the window already covers stays a planted finger gesture, like an
    // unpitched slide.
    chart.fret_hand_positions =
        generateFretHandPositions(built, tempo_map, phrase_boundary_beats, chart.tuning.capo);
    resolveSlideIns(built, chart.fret_hand_positions, grid, notes, chart.tuning.capo);
    if (!chart.fret_hand_positions.empty())
    {
        notes.push_back(
            "generated " + std::to_string(chart.fret_hand_positions.size()) +
            " fret-hand positions (phrase-aware; verify)");
    }

    // Every synthesis that can lengthen a ring is done, so the stored stream takes its final
    // shape here: the same-string clamp first (a re-strike stops the ring), then the picture the
    // surfaces will draw. The one pass below rides that picture rather than the rings behind it —
    // a trail-off's hand exit lands where the gesture is DRAWN to end. Hand-posture spans are NOT
    // an import decision any more: they are derived from the finished notes wherever they are read
    // (common/core's deriveChartShapes), so there is nothing to run here and nothing to report.
    clampSameStringOverlaps(built, tempo_map);
    const std::vector<ChartNote> presented = presentedNotes(built, tempo_map);

    // Trail-off exits follow the hand's next move where it agrees.
    resolveSlideOutExits(built, presented, chart.fret_hand_positions, grid, chart.tuning.capo);

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

    // Import is a commit point, so the chart leaves here in its normal form through the ONE
    // normalizer every load path calls: each note sheds what it cannot execute, every range is
    // brought onto the board, and the settle sweep runs last over the finished stream — released
    // frets after every slide chain, holds after the same-string clamp. The spans a reader derives
    // therefore describe the SETTLED stream, which is the stream the surfaces draw: a claim the
    // chart cannot justify plays as a plain pick, so it must not split a box from a neighbouring
    // strum that plays the same way. The rules live beside their repairs in `chart_rules`, because
    // a list of them kept here drifted from the list there twice, and a dead note carrying a bend
    // then reached validation intact and failed the WHOLE song's import. Counted by rule rather
    // than listed, like every other import conversion: an import converts wholesale, and a
    // position list for a dense score would be hundreds of lines.
    std::map<common::core::ChartRepair, int> repairs_by_rule;
    for (const common::core::ChartConversion& conversion :
         common::core::normalizeChart(chart, tempo_map))
    {
        ++repairs_by_rule[conversion.repair];
    }
    for (const auto& [repair, count] : repairs_by_rule)
    {
        notes.push_back(
            std::to_string(count) + (count == 1 ? " note: " : " notes: ") +
            std::string{common::core::chartRepairText(repair)});
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

        // The persisted part token is also what the note shows, so the conversion note and the
        // song document name the same part identically.
        part_guesses += (part_guesses.empty() ? "" : ", ") + track.name + " -> " +
                        std::string{common::core::partToken(arrangement.part)};
        song.arrangements.push_back(std::move(arrangement));
    }

    // The track-to-part mapping is a heuristic guess (see partForTrack); surface it so the user
    // can spot and correct a misfiled track. Tracked in docs/plans/todo/gp-track-part-mapping.md.
    song.notes.push_back("assigned parts by track order and name (verify): " + part_guesses);

    return song;
}

} // namespace rock_hero::editor::core
