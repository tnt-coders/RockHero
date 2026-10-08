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
#include <rock_hero/common/core/chart/chart_fret_hand.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/shared/ascii_case.h>
#include <rock_hero/common/core/song/song_section_rules.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
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

// One note event on the whole-note axis, before tie merging. The grid position is derived from
// `place` where a note needs one rather than carried beside it: the two are one fact in two
// coordinate systems, and the tempo map's gridPositionAtWholeNote is the conversion.
struct NoteEvent
{
    Fraction place{};          // onset on the whole-note axis
    Fraction duration_whole{}; // how long the string rings, in whole notes
    GpNote source;
    bool tremolo{false};

    // Which VOICE SLOT of its bar wrote this note. A voice is a line of the transcription — one
    // hand's part, running across bar lines in the same slot — which is why the slot index is the
    // identity rather than anything per bar: the let-ring figure walk segments the same lines
    // (\ref letRingFigureEnds).
    std::size_t voice{0};

    // How much ring an ornament took from this event, whether a following before-beat grace run
    // (rule 17) or the alternation a trill on this note spells out. Guitar Pro states a bend's
    // points as PERCENTAGES of the NOTATED duration, so the curve is mapped over the ring plus
    // this and then clipped back to the ring — squeezing it into the shortened ring would state a
    // curve nobody wrote.
    Fraction stolen_lead{};

    // Whether the source marks this note LET RING and Guitar Pro's own playback honors the mark
    // (a dead, palm-muted or staccato note is pre-empted at the emission). The mark is a bare
    // fact here rather than a computed end because the figure law that answers "how long" runs
    // once over the finished built stream (\ref letRingFigureEnds): a bend's points are
    // percentages of what the source wrote, so `duration_whole` has to keep saying that while
    // the ring is normalized past it.
    bool let_ring{false};
};

// What the source notated for an event: what the string rings plus whatever an ornament stole.
[[nodiscard]] Fraction notatedDuration(const NoteEvent& event)
{
    return event.duration_whole + event.stolen_lead;
}

// The master bar a track's bar reads its meter and feel from: a track with more bars than the
// score has master bars clamps onto the last one. The meter and the downbeat's place are read from
// the import's tempo map, built from the same master bars one to one; the feel, which the map does
// not carry, from the master bar itself.
[[nodiscard]] std::size_t measureIndexForBar(
    const std::vector<GpMasterBar>& master_bars, const std::size_t bar_index)
{
    return std::min(bar_index, master_bars.size() - 1);
}

// THE AUDIBILITY HORIZON: how far a struck note can still be asked to ring — one ORIGIN-BAR
// metric length past the onset it is measured from, which is Guitar Pro's own let-ring audibility
// rule. ONE statement of that length for the two readers of it in the let-ring figure law
// (\ref letRingFigureEnds): the seam that decides which marks share a figure, and the cap that
// bounds the figure's one end. Two spellings of "how long is this still audible" would be free to
// disagree, applying the horizon at one place and not the other.
//
// The length is ORIGIN-relative in both readers: it is the bar the anchor itself sits in that
// states the duration, so a mark under a meter change is bounded by the metre it was written in
// rather than the one it rings into. A place past the final bar reads the last bar's meter, which
// is what a ring running off the end of the score asks for.
[[nodiscard]] Fraction audibilityHorizonFrom(
    const common::core::TempoMap& tempo_map, const Fraction place)
{
    const int measure = tempo_map.gridPositionAtWholeNote(place).measure;
    return place + tempo_map.timeSignatureAt(measure).meter.measureWholeNotes();
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

// The corpus-derived default scrape and the minimum gesture window live in the shared seam so
// import and the editor's attack verb synthesize identical defaults (pick_slide_defaults.h).

// A slide gesture's fret travel never shrinks below two frets — the minimum that reads as a
// slide. Widens an agreeing hand delta to that minimum; the constant alone supplies the default
// travel when the hand is still.
constexpr int g_minimum_slide_travel_frets = 2;

[[nodiscard]] constexpr int widenedToMinimumTravel(const int delta, const bool downward)
{
    return downward ? std::min(delta, -g_minimum_slide_travel_frets)
                    : std::max(delta, g_minimum_slide_travel_frets);
}

// Pitch-class names for MIDI note numbers, in the two spellings a score can ask for. Which one a
// track uses is stated by the file (\ref GpTrack::tuning_prefers_flats), never derived here: the
// same pitch is D#2 in one score and Eb2 in another, and only the file knows which was written.
// The naturals are the same entry in both tables, so a tuning without black keys reads alike.
constexpr std::array<const char*, 12> g_midi_note_names{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
constexpr std::array<const char*, 12> g_midi_flat_note_names{
    "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"
};

// Names the open-string pitch of a MIDI note number ("E2" for 40, and 39 as either "D#2" or
// "Eb2" depending on the spelling the score asked for).
[[nodiscard]] std::string midiNoteName(int midi, bool prefer_flats)
{
    const int octave = midi / 12 - 1;
    const std::array<const char*, 12>& names =
        prefer_flats ? g_midi_flat_note_names : g_midi_note_names;
    return std::string{names.at(static_cast<std::size_t>(((midi % 12) + 12) % 12))} +
           std::to_string(octave);
}

// Builds the warp-anchor tempo map from the score's sync points, extending a downbeat terminal
// anchor past the final bar so every note position lies inside the map. Unusable sync points are
// dropped with a conversion note, so the build itself cannot fail.
[[nodiscard]] common::core::TempoMap buildTempoMap(
    const GpScore& score, std::vector<std::string>& notes)
{
    // One change wherever a master bar's meter differs from the bar before it, so the map's meter
    // at every measure IS that master bar's: the builder reads its meters and downbeats from here.
    const std::vector<GpMasterBar>& bars = score.master_bars;
    std::vector<common::core::TimeSignatureChange> signatures;
    for (std::size_t measure = 0; measure < bars.size(); ++measure)
    {
        if (measure == 0 || bars[measure].meter != bars[measure - 1].meter)
        {
            signatures.push_back(
                common::core::TimeSignatureChange{
                    .measure = static_cast<int>(measure) + 1,
                    .meter = bars[measure].meter,
                });
        }
    }

    // The grid without its timing: the whole-note axis every sync point is placed on and every
    // extrapolation is measured along, which is the tempo map's own rather than a copy of it.
    const common::core::TempoMap grid_only{signatures, {}};
    const int total_measures = static_cast<int>(bars.size());
    const GridPosition origin{.measure = 1, .beat = 1, .offset = {}};
    const GridPosition terminal{.measure = total_measures + 1, .beat = 1, .offset = {}};

    std::vector<common::core::BeatAnchor> anchors;
    double last_tempo = score.base_tempo_quarter_bpm;
    int dropped_syncs = 0;
    for (const GpSyncPoint& sync : score.sync_points)
    {
        // A sync point names a fraction of one of the score's bars; anything else points outside
        // the score. Asked as a positive range test so a NaN fraction fails it too.
        const bool fraction_inside_bar = sync.bar_fraction >= 0.0 && sync.bar_fraction <= 1.0;
        if (sync.bar < 0 || sync.bar >= total_measures || !fraction_inside_bar)
        {
            ++dropped_syncs;
            continue;
        }

        // The whole tick nearest the stated fraction, walked from the bar's downbeat: a sync
        // within half a tick of a beat lands on it, and the end of a bar carries onto the next
        // downbeat (the terminal one after the last bar, which pins the song's end exactly).
        const auto bar_index = static_cast<std::size_t>(sync.bar);
        const double ticks_in_bar = (bars[bar_index].meter.measureWholeNotes() *
                                     Fraction{common::core::g_tick_quantum_denominator})
                                        .toDouble();
        const auto ticks =
            static_cast<int>(common::core::nearestTick(sync.bar_fraction * ticks_in_bar));
        const GridPosition position = grid_only.gridPositionAtWholeNote(
            grid_only.wholeNotePositionAt(common::core::measureDownbeat(sync.bar + 1)) +
            Fraction{ticks, common::core::g_tick_quantum_denominator});
        const std::int64_t milliseconds = common::core::nearestAnchorMilliseconds(sync.seconds);

        // Anchors must advance strictly in both grid position and stored time. A sync point that
        // regresses on either axis would corrupt the map, and one within a millisecond of its
        // predecessor is noise below the stored unit, so either is dropped.
        if (!anchors.empty() &&
            (position <= anchors.back().position || milliseconds <= anchors.back().milliseconds))
        {
            ++dropped_syncs;
            continue;
        }
        anchors.push_back(
            common::core::BeatAnchor{.position = position, .milliseconds = milliseconds});
        if (sync.modified_tempo > 0.0)
        {
            last_tempo = sync.modified_tempo;
        }
    }

    if (dropped_syncs > 0)
    {
        notes.push_back(
            "dropped " + std::to_string(dropped_syncs) +
            " audio sync points outside the score or not after the previous one");
    }

    if (anchors.empty())
    {
        anchors.push_back(common::core::BeatAnchor{.position = origin, .milliseconds = 0});
        notes.emplace_back("score has no audio sync points; timing uses the base tempo");
    }

    if (anchors.front().position != origin)
    {
        // Back-extrapolate the missing lead-in at the first known tempo, clamped at zero so the
        // map never starts before the audio.
        const common::core::BeatAnchor& first = anchors.front();
        const double first_tempo = score.sync_points.empty()
                                       ? score.base_tempo_quarter_bpm
                                       : std::max(1.0, score.sync_points.front().modified_tempo);
        const double lead_in_quarters = grid_only.quarterPositionAt(first.position);
        std::int64_t milliseconds = common::core::nearestAnchorMilliseconds(
            first.seconds() - (lead_in_quarters * 60.0 / first_tempo));
        if (milliseconds < 0)
        {
            milliseconds = 0;
            notes.emplace_back("score starts before the audio; the lead-in was clamped");
        }
        anchors.insert(
            anchors.begin(),
            common::core::BeatAnchor{.position = origin, .milliseconds = milliseconds});
        if (anchors[1].milliseconds <= anchors[0].milliseconds)
        {
            anchors.erase(anchors.begin() + 1);
        }
    }

    // Terminal anchor on the downbeat after the final bar, extrapolated at the last tempo. A
    // final sync point can land exactly there (a rollover from the last bar's end); it already
    // pins the song's end to the audio, so no extrapolated anchor is added on top of it. A
    // terminal less than a millisecond past its predecessor is held a millisecond after it, since
    // the terminal, unlike a sync point, cannot be dropped.
    const common::core::BeatAnchor last = anchors.back();
    if (last.position != terminal)
    {
        const double terminal_quarters =
            grid_only.quarterPositionAt(terminal) - grid_only.quarterPositionAt(last.position);
        const std::int64_t milliseconds = std::max(
            last.milliseconds + 1,
            common::core::nearestAnchorMilliseconds(
                last.seconds() + (terminal_quarters * 60.0 / std::max(1.0, last_tempo))));
        anchors.push_back(
            common::core::BeatAnchor{.position = terminal, .milliseconds = milliseconds});
    }

    // Warn when audio sync points leave most of the song to constant-tempo extrapolation: those
    // bars start aligned but drift from any recording that is not metronomically steady, which is
    // a source-data limitation the import cannot recover (the sync points simply are not there).
    const int last_sync_measure = last.position.measure;
    if (!score.sync_points.empty() && (total_measures - last_sync_measure) * 4 > total_measures)
    {
        notes.emplace_back(
            "audio sync points cover only up to measure " + std::to_string(last_sync_measure) +
            " of " + std::to_string(total_measures) +
            "; later timing is extrapolated at the last tempo and may drift from the recording");
    }

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

// The offset of the FIRST keyframe that states a fret the hand sounds, or zero when none does —
// where the note's path starts travelling, which a fabricated slide-in must arrive before. The
// statement AT the end is not one: it is where the path stops, and the scoop's own bound against it
// is asked beside this. NOTE-LOCAL, and mid-build: no successor exists yet for a relation to be
// resolved against, and the scoop needs the same bound whichever gesture that end proves.
[[nodiscard]] Fraction firstStatedFretOffset(const ChartNote& note)
{
    const Keyframe* const end = common::core::endFretStatement(note);
    for (const Keyframe& keyframe : note.keyframes)
    {
        if (keyframe.fret.has_value() && &keyframe != end)
        {
            return keyframe.offset;
        }
    }
    return Fraction{};
}

// States a folded-in segment's Guitar Pro vibrato WIDTH at `offset` — the instant that segment
// BEGINS on the ring that absorbed it. A width is the leg's own (chart.h), so the import writes
// exactly what the score says: a keyframe already standing at the instant (a legato junction)
// takes the segment's width, None included; where none stands, a segment whose width differs
// from the leg before it begins a leg of its own — a keyframe carrying the width, or, for a
// segment WITHOUT the flag after a vibrated one, a bare keyframe, the one stored form of vibrato
// ending where nothing else changes (a tie). A segment whose width equals the leg before it
// states nothing, the leg already covering it.
//
// Guitar Pro writes the mark per note and names no instant inside it, so the import picks one: a
// merged note anchors it at the LAST keyframe. At a legato slide that keyframe is the junction the
// glide arrives at — where the folded segment begins and where vibrato after a glide actually
// starts, which is the corpus's dominant figure (31 of its 34 slide-then-vibrato occurrences arrive
// through this merge); at a tie it is the continuation's own onset; and a note that merges nothing
// states its flag at the onset, which is what \ref ChartNote::vibrato already is. Spelled as the
// folded segment's own START rather than "whichever keyframe is last", because an origin's bend
// curve can legally run past the junction and the literal reading would then hand the vibrato to a
// bend point; in the corpus figure above, the two readings name the same instant.
//
// Stated per SEGMENT rather than as an onset-level `||` over the whole chain, both halves of which
// would lie: a folded segment's flag would vibrate the entire ring from the onset, and a folded
// segment WITHOUT one would inherit the vibrato it arrived after.
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
    // A segment without the flag ends the vibrato at its start, through the one authority for a
    // vibrato ending's stored form (chart.h endVibratoAt): nothing where the leg before was not
    // vibrated, a keyframe stating nothing else where the ending is all that changes there.
    if (!hasVibrato(vibrato))
    {
        endVibratoAt(note, offset);
        return;
    }
    // Two segments can fold onto a single offset (a tie continuation whose legato glide lands on
    // a second voice's note at that very beat), and the LATER one's width is the leg's, exactly
    // as their shared keyframe's fret already takes the later value. Where no keyframe stands, a
    // width equal to the leg before it states nothing — that leg already covers the segment.
    const bool standing = standingKeyframe(note, offset) != nullptr;
    if (standing || vibrato != vibratoBefore(note, offset))
    {
        keyframeAt(note, offset).vibrato = vibrato;
    }
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
        keyframeAt(note, point.offset).bend = point.semitones;
    }
}

// Maps one GP bend onto the chart's [offset, semitones] pairs across the note sustain.
//
// Each point lands at its exact percentage of the note, which is almost never a whole tick; the
// commit point rounds it onto the lattice with every other stored instant (roundOntoTickLattice).
// Two points written at one percentage are a step and fold into one here, the later value winning.
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

// The unit a feel swings and the share of each pair its first beat plays; nothing for straight
// time.
struct FeelShape
{
    Fraction unit;
    Fraction first_share;
};

[[nodiscard]] std::optional<FeelShape> feelShape(const GpTripletFeel feel)
{
    switch (feel)
    {
        case GpTripletFeel::None:
            return std::nullopt;
        case GpTripletFeel::Triplet8th:
            return FeelShape{.unit = Fraction{1, 8}, .first_share = Fraction{2, 3}};
        case GpTripletFeel::Triplet16th:
            return FeelShape{.unit = Fraction{1, 16}, .first_share = Fraction{2, 3}};
        case GpTripletFeel::Dotted8th:
            return FeelShape{.unit = Fraction{1, 8}, .first_share = Fraction{3, 4}};
        case GpTripletFeel::Dotted16th:
            return FeelShape{.unit = Fraction{1, 16}, .first_share = Fraction{3, 4}};
        case GpTripletFeel::Scottish8th:
            return FeelShape{.unit = Fraction{1, 8}, .first_share = Fraction{1, 4}};
        case GpTripletFeel::Scottish16th:
            return FeelShape{.unit = Fraction{1, 16}, .first_share = Fraction{1, 4}};
    }
    // Total above; a value outside the enum is a parser bug, and playing it straight would be
    // an invented answer.
    std::unreachable();
}

// Plays a voice's beats with its bar's feel. Guitar Pro leaves the written rhythm straight on the
// page and swings it on playback, and the chart stores what SOUNDS, so the durations are
// rewritten to the played ones before anything reads them. The rule is Guitar Pro's own, as its
// playback (and alphaTab's reading of it) applies it: the feel names a unit — the eighth for the
// `8th` feels, the sixteenth for the `16th` ones — and moves exactly the PAIRS: two consecutive
// time-taking beats of one voice, each lasting exactly one unit (no dot, no tuplet), the first
// starting a whole number of pairs after the bar's downbeat. The first plays long and the second
// starts where the first now ends, the pair's total unchanged — two thirds and one third for a
// triplet feel, three quarters and one quarter for a dotted one, one quarter and three quarters
// for a Scottish one. A rest is a beat like any other, so a rest-then-note pair lands the note
// late exactly as a note-then-note pair does. Everything else keeps its written time: a lone unit
// on an off slot, a unit followed by a longer or shorter value, a dotted or tupleted value. Grace
// beats take no time from the bar, so one between a pair's two beats neither breaks the pair nor
// moves with it.
//
// The rewrite is purely one of durations — the first beat's runs long, the second's short — so the
// ordinary onset accumulation places the second beat at the first's new end and the beat after the
// pair exactly where the page has it. It runs before the tremolo split so a tremolo-picked unit
// swings as a slot and its strokes fill the played length.
[[nodiscard]] std::vector<GpBeat> swungBeats(
    const std::vector<GpBeat>& beats, const GpTripletFeel feel)
{
    std::vector<GpBeat> played = beats;
    const std::optional<FeelShape> shape = feelShape(feel);
    if (!shape.has_value())
    {
        return played;
    }
    const Fraction unit = shape->unit;
    const Fraction pair = unit + unit;
    const Fraction long_value = pair * shape->first_share;
    const Fraction short_value = pair - long_value;

    // The first time-taking beat at or after `from`, or the end.
    const auto next_timed = [&played](std::size_t from) {
        while (from < played.size() && played[from].grace != GpGracePlacement::None)
        {
            ++from;
        }
        return from;
    };

    Fraction position{};
    for (std::size_t index = next_timed(0); index < played.size(); index = next_timed(index + 1))
    {
        GpBeat& first = played[index];
        const Fraction onset = position;
        position = position + first.duration_whole;
        if (first.duration_whole != unit)
        {
            continue;
        }
        // A pair starts a whole number of pairs into the bar.
        const Fraction pairs_in = onset * Fraction{pair.denominator, pair.numerator};
        if (pairs_in.denominator != 1)
        {
            continue;
        }
        const std::size_t partner = next_timed(index + 1);
        if (partner == played.size() || played[partner].duration_whole != unit)
        {
            continue;
        }
        first.duration_whole = long_value;
        played[partner].duration_whole = short_value;
        position = position + unit;
        index = partner;
    }
    return played;
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
//
// Takes the beats by value: the swing hands over a fresh copy of the voice, so every beat the
// split leaves whole moves through rather than being copied a second time (each carries its own
// note vector, so a copy is a heap allocation per beat).
[[nodiscard]] std::vector<GpBeat> expandTremoloBeats(
    std::vector<GpBeat> beats, int& kept_marks, int& dropped_rolls)
{
    std::vector<GpBeat> expanded;
    expanded.reserve(beats.size());
    for (GpBeat& beat : beats)
    {
        const Fraction stroke = beat.tremolo_stroke;
        if (stroke.numerator <= 0 || beat.grace != GpGracePlacement::None || beat.notes.empty())
        {
            expanded.push_back(std::move(beat));
            continue;
        }
        if (std::ranges::any_of(
                beat.notes, [](const GpNote& note) { return note.slide_flags != 0; }))
        {
            ++kept_marks;
            expanded.push_back(std::move(beat));
            continue;
        }
        const Fraction count_fraction =
            beat.duration_whole * Fraction{stroke.denominator, stroke.numerator};
        const auto count = static_cast<int>(count_fraction.numerator / count_fraction.denominator);
        if (count <= 1)
        {
            // A beat no longer than one stroke IS its single stroke; the mark adds nothing.
            beat.tremolo_stroke = Fraction{};
            expanded.push_back(std::move(beat));
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
    const std::vector<NoteEvent>& events, const std::vector<int>& tuning_midi, const int capo,
    std::vector<std::string>& notes)
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

        const Fraction step = g_trill_step_whole;
        const Fraction count_fraction =
            event.duration_whole * Fraction{step.denominator, step.numerator};
        const auto count = static_cast<int>(count_fraction.numerator / count_fraction.denominator);
        if (count <= 1)
        {
            // A ring no longer than one step sounds the note once; there is no alternation in it.
            ++too_short;
            expanded.push_back(event);
            continue;
        }

        NoteEvent principal = event;
        principal.duration_whole = step;
        principal.stolen_lead = notatedDuration(event) - step;
        principal.source.tie_origin = false;
        // The mark is consumed by the spell-out; leaving it would claim a trill still to expand.
        principal.source.trill_value.reset();
        // What rings on after the run is the run's LAST note, not its first, so the let-ring
        // mark travels to the end with the remainder and the onward tie below. (A tremolo's
        // strokes ARE re-picked, which is exactly why those split into real beats before the
        // figure walk and each is asked the question afresh.)
        principal.let_ring = false;
        expanded.push_back(std::move(principal));

        for (int index = 1; index < count; ++index)
        {
            const bool last = index + 1 == count;
            NoteEvent piece;
            // The alternation is the trilled note's own line continuing, so every piece stays in
            // the voice that wrote the trill.
            piece.voice = event.voice;
            piece.place = event.place + (Fraction{index} * step);
            // The last note absorbs the remainder, so the run's total ring equals the source's
            // exactly even where the span does not divide (dots and tuplets).
            piece.duration_whole =
                last ? event.duration_whole - (Fraction{count - 1} * step) : step;
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
            piece.let_ring = last && event.let_ring;
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
// first string speaks. THE ROLL IS AN ACCUMULATION FIGURE PLAYED FAST, so what the import writes
// is exactly the sound: each member struck at its turn over the stored spread, every one of them
// ringing to the end its beat gave it. The derivation reads one arpeggio span off those rings by
// the ordinary opening law — the members' rings overlap, the span dates from the first of them,
// and the arrivals are absorbed — with no rule of its own.
//
// NO FRONTED CLAIMS (a claim authored at the figure's front for every member still to come). That
// is scaffolding for a derivation that cannot see the rings, and the accumulation law sees them —
// so such a claim would state a fact the sound already states, which is exactly the empty
// statement the inert sweep removes. IMPORTS AUTHOR ZERO CLAIMS: the statement model is sound
// states and AUTHORED states, and "fronted span" is not a derivation concept. The claim machinery
// itself — supersession and the inert sweep — keeps the residue it is for: the stop a right-hand
// onset holds under it.
//
// The bracket's DRAWN LENGTH follows from that: a claims-produced span would run only as far as
// the roll gesture, because that is all the claims state, whereas the hold is the RING, and the
// accumulation derives it.
//
// Works on the events one beat has just pushed, addressed by the RANGE they occupy, so the group
// is the language's own and never a key two beats at one instant could share. That is also what
// puts it after the staccato halving and any on-beat grace shift: a member's ring is whatever
// collection left it, and the figure only ever moves that ring's FRONT — the wait eats into it,
// the anticipation below gives it back — which is how the reference implementation times the
// stagger too (MidiFileGenerator.ts:974-978 adds the offset to the onset and subtracts it from
// every duration, so the members end together).
//
// Tied continuations are not members: nothing re-strikes them, so there is no stagger to give them
// (MidiFileGenerator.ts:2232-2238 leaves them out of the count the same way). The order is the
// stroke's rather than the naive reading of the file's word — see \ref GpRollDirection.
//
// Guitar Pro's SECOND roll slider, "Start time", places that figure against its beat: at 1 the
// first member is struck on it, and at 0 the roll ANTICIPATES — the LAST member lands on the beat
// and the figure opens a whole spread early. There is no reference implementation to copy, because
// every open-source reader (alphaTab and MuseScore included) ignores this property outright, so the
// recorded semantic is the linear reading of the tool's own two labelled endpoints. It is measured
// against the span the import actually WRITES — the staggers, after their truncating division —
// rather than the raw spread, which is what makes the 0 endpoint land the last member exactly on
// the beat even where that division lost a tick. The whole figure moves together, because the span
// opens where the hand takes the grip; the ENDS do not move, so an early member simply rings
// longer.
//
// Returns Refused, leaving the beat exactly as it stands for the caller to count, when the figure
// cannot be written at all: fewer than two members to spread, a spread the beat itself cannot
// contain, a stagger that rounds to nothing on the grid, or one that would leave a member no ring.
// Returns ClampedOnBeat when the figure is written but its stated anticipation had nowhere to open:
// before the song, or on a slot an earlier sounding already holds on the same string — which the
// same-string clamp cannot bound away, two notes at one (position, string) being a collision rather
// than an overlap.
[[nodiscard]] RollSpread expandRolledBeat(
    std::vector<NoteEvent>& events, const std::size_t first_event, const GpBeat& beat)
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
    const Fraction step{step_ticks, g_roll_ticks_per_whole};
    for (std::size_t rank = 0; rank < members.size(); ++rank)
    {
        if (!(Fraction{static_cast<int>(rank)} * step < events[members[rank]].duration_whole))
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
    Fraction shift{quanta, g_position_quanta_per_whole};

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
            if (events[member].place - back < Fraction{})
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
                    !(earlier.place < events[member].place - back))
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

    for (std::size_t rank = 0; rank < members.size(); ++rank)
    {
        NoteEvent& member = events[members[rank]];
        const Fraction wait = Fraction{static_cast<int>(rank)} * step;
        // The grip is taken as one and released as one: the onset walks back by the anticipation
        // and forward by this member's own wait, while the end the beat stated stays exactly where
        // it is. An early member therefore rings longer, and a staccato member rings into the
        // halved end its own mark gave it.
        member.place = member.place - shift + wait;
        member.duration_whole = member.duration_whole + shift - wait;
    }
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
// The bar's feel is played first of all, because every duration below is read from the beats it
// rewrites. All three spell-outs then happen here, each where its own input is final: tremolo
// beats split BEFORE collection (their strokes must flow through positions, graces and ties like
// hand-notated beats), a rolled beat's members stagger as that beat's events are pushed (the ring
// the stagger eats into is the one collection just gave them), and trilled notes spell out AFTER it
// (each alternation fills the ring the note is finally left with). The single sort at the end is
// what puts every fabricated onset back in stream order.
[[nodiscard]] std::vector<NoteEvent> collectEvents(
    const GpTrack& track, const std::vector<GpMasterBar>& master_bars,
    const common::core::TempoMap& tempo_map, const int capo, std::vector<std::string>& notes)
{
    std::vector<NoteEvent> events;
    int overfull = 0;
    int dropped_graces = 0;
    int kept_tremolo_marks = 0;
    int rolls_on_tremolo = 0;
    int spread_rolls = 0;
    int unspread_rolls = 0;
    int clamped_anticipations = 0;
    int letring_marks_preempted = 0;

    // Each voice plays its bar's feel and then its tremolo beats spell out; the played copies live
    // for the whole collection because pending grace runs hold beat pointers across bar
    // boundaries.
    std::vector<std::vector<std::vector<GpBeat>>> expanded_bars(track.bars.size());
    for (std::size_t bar_index = 0; bar_index < track.bars.size(); ++bar_index)
    {
        const std::size_t measure_index = measureIndexForBar(master_bars, bar_index);
        for (const std::vector<GpBeat>& voice : track.bars[bar_index].voices)
        {
            expanded_bars[bar_index].push_back(expandTremoloBeats(
                swungBeats(voice, master_bars[measure_index].triplet_feel),
                kept_tremolo_marks,
                rolls_on_tremolo));
        }
    }

    // The one place a source note's ring is established from what the beat states, which is why
    // both of Guitar Pro's duration marks land here. STACCATO: playback sounds such a note for
    // exactly half its stated duration, so the mark is duration truth and never a stored field.
    // Per NOTE rather than per beat, so a chord's single staccato member shortens alone; and
    // before the trill spell-out at the end of this function, so an alternation fills the ring the
    // note actually has. The removed half is NOT `stolen_lead`: no ornament took it and nothing
    // sounds in it — the note simply rings for half — so a bend's percentages map over the halved
    // ring. A tremolo beat's strokes were split before collection and each carry the mark, so a
    // staccato tremolo detaches every stroke, which is what the two marks together say.
    //
    // LET RING is the same idea in the other direction — it lengthens where staccato halves — but
    // it cannot be folded into the ring here, because the ring is also what a bend's percentages
    // are laid out over; the mark rides the event instead and the figure law lengthens the ring
    // once the built stream is final (\ref letRingFigureEnds). What IS decided here is whether
    // the mark applies at all: Guitar Pro's playback pre-empts it on the notes it already
    // shortens, returning before the let-ring block on a dead, palm-muted or staccato note
    // (MidiFileGenerator._getNoteDuration), so those three keep exactly the ring the marks above
    // give them. Only that PRE-EMPTION is taken from the reference's block: the static durations
    // it returns on those notes (a fixed fraction of a quarter for dead and palm-muted alike)
    // are declined, because the notated duration is the timing information the chart reads and
    // presentation hides a dead tail on the drawn side instead.
    const auto emit_note = [&events, &letring_marks_preempted](
                               const GpNote& source,
                               const std::size_t voice,
                               const bool tremolo,
                               const Fraction place,
                               const Fraction duration,
                               const bool grace) {
        NoteEvent event;
        event.duration_whole = source.staccato ? duration * Fraction{1, 2} : duration;
        event.place = place;
        event.source = source;
        event.tremolo = tremolo;
        event.voice = voice;
        // A grace's mark states nothing about length — the ornament's ring IS its lead — so it
        // never extends; its fret statement still joins the voice's grammar like any sounding
        // onset.
        if (!grace && source.let_ring)
        {
            if (source.full_mute || source.palm_mute || source.staccato)
            {
                // The law's reach, counted where it is lost: the source's own playback already
                // shortened this note, so its mark states nothing the figure could lengthen.
                ++letring_marks_preempted;
            }
            else
            {
                event.let_ring = true;
            }
        }
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
        const common::core::GridPosition downbeat = common::core::measureDownbeat(
            static_cast<int>(measureIndexForBar(master_bars, bar_index)) + 1);
        const Fraction bar_place = tempo_map.wholeNotePositionAt(downbeat);
        const Fraction bar_length =
            tempo_map.timeSignatureAt(downbeat.measure).meter.measureWholeNotes();
        // A thirty-second note.
        const Fraction full_lead{1, 32};

        for (std::size_t voice_index = 0; voice_index < expanded_bars[bar_index].size();
             ++voice_index)
        {
            std::vector<const GpBeat*>& pending = pending_graces_per_voice[voice_index];
            Fraction position_whole{};
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

                const Fraction duration = beat.duration_whole;
                const Fraction onset = position_whole;
                position_whole = position_whole + duration;
                if (beat.notes.empty())
                {
                    // A rest cannot host a grace run: graces ornament a sounding principal.
                    dropped_graces += static_cast<int>(pending.size());
                    pending.clear();
                    continue;
                }
                if (onset >= bar_length)
                {
                    ++overfull;
                    dropped_graces += static_cast<int>(pending.size());
                    pending.clear();
                    continue;
                }

                const Fraction principal_place = bar_place + onset;
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
                        const Fraction gap = principal_place - floor;
                        const Fraction lead = fitLeadToGap(full_lead, gap, before_count);
                        if (lead.numerator <= 0)
                        {
                            dropped_graces += before_count;
                        }
                        else
                        {
                            const Fraction first_onset =
                                principal_place - (Fraction{before_count} * lead);
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
                                    if (earlier.place < first_onset &&
                                        earlier.place + earlier.duration_whole > first_onset)
                                    {
                                        const Fraction rings = first_onset - earlier.place;
                                        earlier.stolen_lead = notatedDuration(earlier) - rings;
                                        earlier.duration_whole = rings;
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
                                const Fraction grace_place = principal_place - back;
                                for (const GpNote& grace_note : grace->notes)
                                {
                                    // A grace beat takes no bar time and its mark never extends,
                                    // but it still belongs to the voice that wrote it: the
                                    // ornament is that line's, and the grip it states is too.
                                    emit_note(
                                        grace_note,
                                        voice_index,
                                        grace->tremolo_stroke.numerator > 0,
                                        grace_place,
                                        lead,
                                        true);
                                }
                                --remaining;
                            }
                        }
                    }

                    // On-beat run: graces sound on the principal's position and the principal
                    // notes on their strings land one lead later per grace, ends unchanged.
                    if (on_count > 0)
                    {
                        const Fraction lead = fitLeadToGap(full_lead, duration, on_count);
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
                                const Fraction grace_place = principal_place + forward;
                                for (const GpNote& grace_note : grace->notes)
                                {
                                    emit_note(
                                        grace_note,
                                        voice_index,
                                        grace->tremolo_stroke.numerator > 0,
                                        grace_place,
                                        lead,
                                        true);
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
                        voice_index,
                        beat.tremolo_stroke.numerator > 0,
                        shifted ? (principal_place + principal_shift) : principal_place,
                        shifted ? (duration - principal_shift) : duration,
                        false);
                }
                if (beat.roll_direction != GpRollDirection::None)
                {
                    const RollSpread placement = expandRolledBeat(events, first_own_event, beat);
                    spread_rolls += placement != RollSpread::Refused ? 1 : 0;
                    unspread_rolls += placement == RollSpread::Refused ? 1 : 0;
                    clamped_anticipations += placement == RollSpread::ClampedOnBeat ? 1 : 0;
                }
                last_beat_per_voice[voice_index] = SoundingBeat{
                    .onset = principal_place + principal_shift,
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
    if (letring_marks_preempted > 0)
    {
        notes.push_back(
            std::to_string(letring_marks_preempted) +
            " let-ring marks were pre-empted by the source's own shortening (dead, palm-muted or "
            "staccato) and never extend");
    }

    events = expandTrilledEvents(events, track.tuning_midi, capo, notes);

    std::ranges::stable_sort(events, [](const NoteEvent& lhs, const NoteEvent& rhs) {
        if (lhs.place != rhs.place)
        {
            return lhs.place < rhs.place;
        }
        return lhs.source.string < rhs.source.string;
    });
    return events;
}

// Built notes plus their onset on the whole-note axis (needed for tie and slide spans). The note
// carries everything else, including how long it rings — the builder stores ACTUAL durations, so
// an end kept beside the sustain would be a second copy of the same fact.
struct BuiltNote
{
    ChartNote note;
    Fraction place{};
    int gp_string{0};
    int slide_flags{0};

    // The VOICE SLOT that wrote this note, carried from its event. The chart itself has no voices
    // — the format stores one stream of notes — so this is a fact about the SOURCE, kept only
    // while the build still has to reason about the transcription's separate lines: the
    // let-ring figure walk is scoped to them (\ref letRingFigureEnds).
    // A tie continuation merges into its origin, so a merged note wears the ORIGIN's voice, which
    // is the line that struck it.
    std::size_t voice{0};

    // Onset of the tied continuation the slide flags were inherited from, when they were: the
    // glide leaves from the junction, not the merged note's onset (policy rule 15).
    std::optional<Fraction> slide_from_place;

    // Whether this note carries a live LET RING mark, carried from the event that opened it and
    // answered once every other pass that can lengthen the ring is done (\ref letRingFigureEnds).
    // Only the note the picking hand STRUCK carries one: a tied continuation states nothing about
    // when the string was struck, so the struck note's mark answers for the whole merged chain.
    bool let_ring{false};
};

// Where the string stops ringing, on the whole-note axis.
[[nodiscard]] Fraction ringEndOf(const BuiltNote& entry)
{
    return entry.place + entry.note.sustain;
}

// The next HEAD on this record's own string, or nullptr — the successor the slide-out/arrival
// relation is a fact about. The stream is in onset order, so the first later record on the string
// is it. A record `merged_away` marks was folded into its predecessor and is no head; a null table
// says the stream holds heads only, which is what it holds once the merges have been compacted out.
[[nodiscard]] const BuiltNote* nextHeadOnString(
    const std::vector<BuiltNote>& built, const std::size_t index,
    const std::vector<bool>* const merged_away)
{
    for (std::size_t ahead = index + 1; ahead < built.size(); ++ahead)
    {
        if (merged_away != nullptr && (*merged_away)[ahead])
        {
            continue;
        }
        if (built[ahead].note.string == built[index].note.string)
        {
            return &built[ahead];
        }
    }
    return nullptr;
}

// The exit fret a SLIDE-OUT may take. The importer INVENTS this fret — four frets out in the flag's
// direction, or the hand's next move where it agrees — and a fret at a ring's end naming the stop
// the next head on that string is struck at, at that same instant, is what the chart reads as a
// shift slide's ARRIVAL. The source said this note SLIDES OUT, so the invented fret is the
// importer's to keep clear of that stop: where the candidate would collide, the choice moves one
// fret further in the gesture's own direction, and the other way at the board's edge.
//
// Asked of the one authority rather than by restating its clauses, on a trial carrying the ring the
// same-string clamp will settle on: that clamp only ever shortens, so a ring already reaching the
// next head ends exactly on it while a shorter one never can. One step is always enough, the
// relation comparing the candidate against a single stop.
[[nodiscard]] int slideOutExitClearOfNextHead(
    const BuiltNote& entry, const BuiltNote* const next_head, const int candidate,
    const bool upward, const int capo)
{
    if (next_head == nullptr)
    {
        return candidate;
    }
    const Fraction gap = next_head->place - entry.place;
    if (entry.note.sustain < gap)
    {
        return candidate;
    }
    ChartNote trial = entry.note;
    static_cast<void>(common::core::clipPayloadsToSustain(trial, gap));
    common::core::setSlideOut(trial, candidate);
    if (!common::core::arrivesIntoNextHead(
            common::core::LegatoPredecessor{.note = trial, .onset_gap = gap}, next_head->note))
    {
        return candidate;
    }
    const int floor = common::core::firstPlayableFret(capo);
    const int onward = upward ? candidate + 1 : candidate - 1;
    if (onward >= floor && onward <= common::core::g_max_fret)
    {
        return onward;
    }
    const int back = upward ? candidate - 1 : candidate + 1;
    // A board with no second candidate at all leaves the collision standing; the fret rules judge
    // what such a tuning can express, and nothing here can invent room that is not on the neck.
    return back >= floor && back <= common::core::g_max_fret ? back : candidate;
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

// The stored stream's final shape, settled once every synthesis that can lengthen a ring is done.
// The one rule a note cannot obey alone, asked of the one authority in core rather than restated
// here (which is why the notes travel out and back — that authority speaks about a note stream,
// not about the builder's records): a re-strike stops the ring, so no stored tail
// crosses the next onset on its own string. A slide-out or an end bend authored on a tiled ring
// then ends EXACTLY on that head, which is what the material says the hands did; presentation
// stops the ink one margin before that head and leaves the mark at its stored instant.
//
// After it, the payload is trimmed to the ring — ONCE, and for every note. Only one producer ever
// writes past the ring: an imported bend, whose points Guitar Pro states as percentages of the
// NOTATED duration, so the curve outruns the ring of any note an ornament stole from. Every other
// payload is placed inside a ring the pass that placed it lengthened to fit, and the clamp trims
// what it shortens. Trimming inside the bend's own mapping instead would quietly lose a let-ring
// note's bend destination: the ring it is cut against is the stolen one, and the let-ring pass then
// hands the ring back with the curve already gone.
void clampSameStringOverlaps(std::vector<BuiltNote>& built, const common::core::TempoMap& tempo_map)
{
    std::vector<ChartNote> stored = storedNotes(built);
    static_cast<void>(common::core::normalizeSustainOverlaps(stored, tempo_map));
    for (ChartNote& note : stored)
    {
        static_cast<void>(common::core::clipPayloadsToSustain(note, note.sustain));
    }
    for (std::size_t index = 0; index < built.size(); ++index)
    {
        built[index].note = std::move(stored[index]);
    }
}

// The stop a note STATES on its string at an instant, or nothing where it states none there. ONE
// reader for both halves of the contradiction test below — the stop the sounding ring is holding,
// and the statement the arriving onset makes — because those are the same question asked of two
// notes, and two spellings of "where is this finger" would be free to disagree. The answer is the
// chart's own (fretHandStopAt); this only moves the instant onto the note's ring.
//
// A tap's fretting-hand stop is the stop its pull-off plants beneath it, as it is in the chart's
// own held table (chartHeldStops): so a tap over the gripped stop rides it (gripStatementAt) and a
// tap over other ground states that ground and closes the figure AT ITSELF, exactly as a fretting
// source sounding over other ground does. A bare tap, with no plant, states nothing; a tapped
// harmonic states the stop its fretting hand presses, which is its own fret.
[[nodiscard]] std::optional<common::core::ChartStop> statedStopAt(
    const std::vector<BuiltNote>& built, const std::vector<std::optional<int>>& planted_stops,
    const std::size_t index, const Fraction instant)
{
    const BuiltNote& entry = built[index];
    // fretHandStopAt rather than heldFretAt: an open note is a statement here, one a grip can
    // contradict.
    return common::core::fretHandStopAt(entry.note, planted_stops[index], instant - entry.place);
}

// A note's GRIP STATEMENT for the let-ring figure law below: the stop it states at the asked
// instant — except a pull-off source arriving above the stop the figure ALREADY GRIPS on its
// string, which states that stop, the fret it sounds being the ornament riding above it
// (\ref rock_hero::common::core::gripStatement, the span machine's own authority, so the cut law
// seams where the spans break and the import stays span-blind). Such a source and the slide-out
// returning beneath it both restate the grip and close nothing. A source over any other ground
// states what it sounds, so its release is an ordinary new statement and the figure closes THERE,
// as it would were the same notes plainly picked: a pull-off proves a finger at its release and at
// no earlier instant.
[[nodiscard]] std::optional<common::core::ChartStop> gripStatementAt(
    const std::vector<BuiltNote>& built, const std::vector<std::optional<int>>& planted_stops,
    const std::size_t index, const Fraction onset,
    const std::optional<common::core::ChartStop>& gripped)
{
    const std::optional<common::core::ChartStop> beneath =
        common::core::gripStatement(built[index].note, planted_stops[index], gripped);
    return beneath.has_value() ? beneath : statedStopAt(built, planted_stops, index, onset);
}

// THE LET-RING FIGURE LAW. Three rules, held in one breath: a marked tail rings to the first onset
// its own voice states after its figure's last mark; a figure ends where its grip is contradicted
// or where its own audibility horizon expires before the next onset arrives; written is the floor
// and the same-string clamp is physics. The tails anchor at the marked run, with no step back to
// an anacrusis.
//
// GRIP: each voice accumulates the stop last stated on each string SINCE THE FIGURE BEGAN. A
// first-time string GROWS it, a same-stop statement CONFIRMS it, and nothing else touches it —
// so a chug can never split anything, and a repetition of a figure can never be divided: with no
// retreat mechanism in the law, the repetition invariant is structural, not satisfied. An onset
// stating a different stop on a gripped string closes the figure and founds the next AT ITSELF;
// only TIME seams besides it. The comparison judges GRIP STATEMENTS (\c gripStatementAt), so a
// pull-off source over the gripped stop closes nothing, while one over any other ground closes the
// figure at its SLIDE-OUT — the same seam the span machine breaks at. The figure's whole job for
// the tails is grouping the MARKS — which let-ring stack a mark belongs to, and therefore where
// that stack's marked run ends — with one correction to the grouping, the FRAGMENT DONATION below:
// a figure closed by a GRIP contradiction while too small to ever found a span hands its
// non-contradicting notes to the figure that closed it. Never across the horizon, which mis-groups
// nothing.
//
// TAIL: the mark is the transcriber asking material to ring on, so the ring runs exactly as far
// as the asking does — to the first onset the figure's OWN VOICE states after its LAST marked
// note. Material past the marked run never asked: a marked drone must not ring into the unmarked
// chords that follow it in its own line. Where the voice states nothing more, the first onset
// anywhere in the track answers (the voice-exhausted trailing case); where nothing follows at all,
// the figure's latest written end. The SEAM never appears in the tail arithmetic — it is always at
// or past the first onset after the figure's marks, so the anchor subsumes it, and the figure
// boundary only decides which marks count as one stack. And the end is never more than THE
// AUDIBILITY HORIZON past the last marked onset (\ref audibilityHorizonFrom) — Guitar Pro's own
// audibility rule, which bounds a marked drone under a marked same-voice texture that nothing ever
// contradicts.
//
// TIME SEAMS TOO: a figure also closes when the arriving onset lies PAST the audibility horizon of
// the figure's most recent member — the same length the cap reads, measured member to member. The
// mark is the transcriber asking material to ring ON, and material arriving after that asking has
// already expired cannot belong to the same asking. Without this arm, membership is time-blind:
// silence states nothing, so a rest of any length closes nothing and material re-entering bars
// later on the SAME grip merely CONFIRMS it and joins the stack (open strings state stop 0, so
// they confirm too). One corpus chart would carry such a figure across 33 bars, whose single end —
// computed from its LAST member, as the law's own arithmetic requires — reaches five short notes
// struck over 32 bars earlier. Measuring the horizon member to
// member rather than from the figure's first note is what keeps a continuous texture of any length
// ONE figure: only real silence expires an asking. A per-member cap is not the fix — it patches
// the symptom at the tail while leaving the mis-grouping upstream intact, and it re-rags the stack
// that the one-end law exists to hold together.
//
// THE GRIP IS FIGURE-SCOPED MEMORY, not sound. Reading the SOUNDING grip instead needs a staleness
// guard, and that pair fails in opposite directions: late strikes confirming a dying grip escape
// its shared clip, while a new figure's own opener is clipped by the contradiction it belongs to.
// Figure membership is the one fact that separates those, so the grip lives and dies with the
// figure and no staleness rule exists. Both halves of every comparison read through the one
// statement authority (`statedStopAt`), so a slid finger carries its statement forward instead of
// manufacturing a contradiction, and a tap speaks through its resolved claim.
//
// PER VOICE, because events must not cut rings in another voice: grammar takes the voice; only the
// same-string clamp — physics — is cross-voice. The walk reads onsets and statements only, never a
// ring, so it is a pure function of the written stream: no fixpoint, one forward pass per voice.
//
// Returns the figure end for every marked note, index-parallel to `built`.
[[nodiscard]] std::vector<std::optional<Fraction>> letRingFigureEnds(
    const std::vector<BuiltNote>& built, const std::vector<std::optional<int>>& planted_stops,
    const common::core::TempoMap& tempo_map)
{
    // A grip is string -> the note that last stated it, so a slid finger carries its statement
    // forward: the stop is re-asked of that note at the instant under judgment.
    using Grip = std::map<int, std::size_t>;
    const auto gripped_at = [&built, &planted_stops](
                                const Grip& grip,
                                const int string,
                                const Fraction instant) -> std::optional<common::core::ChartStop> {
        const auto held = grip.find(string);
        if (held == grip.end())
        {
            return std::nullopt;
        }
        // Never before the gripping note began: the donation asks a LATER figure's grip about an
        // earlier note, and a note states nothing ahead of its own onset.
        return statedStopAt(
            built, planted_stops, held->second, std::max(instant, built[held->second].place));
    };
    // Writes one note into a grip as its string's latest statement — unless it rides above the
    // stop already gripped there (\ref gripStatementAt), which it restates WITHOUT rewriting: the
    // entry stays the note that really states that stop, so the slide-out beneath the ornament
    // confirms it and nothing downstream reads the ornament as the grip.
    const auto state_into = [&built, &planted_stops, &gripped_at](
                                Grip& grip, const std::size_t index, const Fraction instant) {
        const int string = built[index].note.string;
        const bool rides_above =
            common::core::gripStatement(
                built[index].note, planted_stops[index], gripped_at(grip, string, instant))
                .has_value();
        if (!rides_above && statedStopAt(built, planted_stops, index, instant).has_value())
        {
            grip[string] = index;
        }
    };

    // One figure is its member notes, nothing more: no seam is stored, because no tail reads one.
    std::vector<std::vector<std::size_t>> figures;
    // Whether each figure was opened by a HORIZON seam (or opens its voice) rather than a GRIP
    // seam — the one fact the phrases below need from the walk. Index-parallel to `figures`.
    std::vector<bool> opened_by_silence;

    std::map<std::size_t, std::vector<std::size_t>> voices;
    for (std::size_t index = 0; index < built.size(); ++index)
    {
        voices[built[index].voice].push_back(index);
    }

    for (const auto& [voice, indices] : voices)
    {
        Grip grip;
        std::optional<std::size_t> current;

        // Whether one slot contradicts the CURRENT grip. Co-struck notes are judged against the
        // pre-instant state, so they never contradict each other — the same convention the span
        // machine reads by.
        const auto contradicts = [&](const std::vector<std::size_t>& slot, const Fraction onset) {
            for (const std::size_t index : slot)
            {
                const std::optional<common::core::ChartStop> gripped =
                    gripped_at(grip, built[index].note.string, onset);
                const std::optional<common::core::ChartStop> stated =
                    gripStatementAt(built, planted_stops, index, onset, gripped);
                if (stated.has_value() && gripped.has_value() && *gripped != *stated)
                {
                    return true;
                }
            }
            return false;
        };

        std::size_t at = 0;
        while (at < indices.size())
        {
            std::vector<std::size_t> slot;
            const Fraction onset = built[indices[at]].place;
            while (at < indices.size() && built[indices[at]].place == onset)
            {
                slot.push_back(indices[at]);
                ++at;
            }

            // THE HORIZON SEAM: the figure's most recent member is the last thing its asking was
            // stated over, so an onset arriving past THAT member's audibility horizon arrives
            // after the asking expired and founds its own figure. Member to member, never from
            // the figure's opening, so only real silence expires an asking.
            const bool expired =
                current.has_value() &&
                audibilityHorizonFrom(tempo_map, built[figures[*current].back()].place) < onset;
            if (!current.has_value() || expired || contradicts(slot, onset))
            {
                // The hand moved, or the asking expired: only the second can end a phrase here.
                opened_by_silence.push_back(!current.has_value() || expired);
                figures.emplace_back();
                current = figures.size() - 1;
                grip.clear();
            }
            std::vector<std::size_t>& members = figures[*current];
            members.insert(members.end(), slot.begin(), slot.end());
            for (const std::size_t index : slot)
            {
                state_into(grip, index, onset);
            }
        }
    }

    // THE FRAGMENT DONATION: a figure closed while holding too few notes to ever FOUND a span is
    // not a statement — it is the GRIP seam mis-grouping a remnant with the next figure's opening
    // notes. Each of its notes that does not contradict the closing figure's own grip joins that
    // figure; the rest stay. Bounded by the span machine's own founding law, asked of its one
    // authority (\ref common::core::foundsSpan) rather than restated: this walk runs before any
    // ring is decided — the figures are what decide them — so it cannot ask whether a figure DID
    // found a span, only whether it EVER could, which is its largest stroke and its whole
    // membership put to that law. A real figure never donates, which is what keeps a repetition
    // undividable and a closing confirmation with its own stack. Left to right, so a donation can
    // carry a still-too-small figure's question to the next seam.
    const auto largest_stroke = [&built](const std::vector<std::size_t>& figure) {
        // Counted by onset rather than by adjacency: a donation appends out of onset order.
        std::map<Fraction, std::size_t> strokes;
        std::size_t largest = 0;
        for (const std::size_t index : figure)
        {
            largest = std::max(largest, ++strokes[built[index].place]);
        }
        return largest;
    };
    for (std::size_t at = 0; at + 1 < figures.size(); ++at)
    {
        std::vector<std::size_t>& fragment = figures[at];
        std::vector<std::size_t>& closer = figures[at + 1];
        if (fragment.empty() || closer.empty() ||
            built[fragment.front()].voice != built[closer.front()].voice)
        {
            continue; // a voice's last figure was closed by nothing and donates to nothing
        }
        if (common::core::foundsSpan(largest_stroke(fragment), fragment.size()))
        {
            continue;
        }
        // NEVER ACROSS THE HORIZON: donation repairs a GRIP seam, which can mis-group a stack's
        // opening notes. A horizon seam never mis-groups — it says the asking expired — so
        // handing the remnant forward would re-join the very asking the seam closed, and a lone
        // mark before a long silence would take the returning stack's end as its own. Same
        // question as the seam's, asked of the same authority: does the closer arrive within the
        // fragment's own audibility horizon?
        if (audibilityHorizonFrom(tempo_map, built[fragment.back()].place) <
            built[closer.front()].place)
        {
            continue;
        }
        Grip closer_grip;
        for (const std::size_t index : closer)
        {
            state_into(closer_grip, index, built[index].place);
        }
        std::vector<std::size_t> kept;
        for (const std::size_t index : fragment)
        {
            const Fraction onset = built[index].place;
            const std::optional<common::core::ChartStop> gripped =
                gripped_at(closer_grip, built[index].note.string, onset);
            const std::optional<common::core::ChartStop> stated =
                gripStatementAt(built, planted_stops, index, onset, gripped);
            if (stated.has_value() && gripped.has_value() && *stated != *gripped)
            {
                kept.push_back(index);
            }
            else
            {
                closer.push_back(index);
            }
        }
        fragment = std::move(kept);
    }

    // WHERE A MARKED RUN'S RING IS ANSWERED, AND HOW FAR IT IS STILL AUDIBLE — one rule, because
    // they are one question put to one scope. The ANCHOR is the first onset the run's own voice
    // states after its last mark, else the first anywhere in the track (the voice-exhausted
    // trailing case), else nothing; the CAP is the audibility horizon of that same last mark.
    // Asked at two scopes: the FIGURE's marks for a ring the hand holds, the PHRASE's for one it
    // does not.
    //
    // BOTH HALVES COME FROM THE SCOPE THAT ANSWERS THE RING, and the cap is never read from an
    // individual ring's own onset. A per-ring cap bounds each member of a stack separately and
    // stops it raggedly — the disease the one-end law exists to cure — holding every open drone to
    // a single bar while its fretted stackmates ring on to the figure's answer.
    const auto scope_end = [&built, &voices, &tempo_map](
                               const Fraction last_marked,
                               const std::size_t voice) -> std::optional<Fraction> {
        const auto capped = [&tempo_map, last_marked](const Fraction anchor) {
            return std::min(anchor, audibilityHorizonFrom(tempo_map, last_marked));
        };
        const std::vector<std::size_t>& voiced = voices.at(voice);
        const auto own_next = std::ranges::upper_bound(
            voiced, last_marked, std::ranges::less{}, [&built](const std::size_t index) {
                return built[index].place;
            });
        if (own_next != voiced.end())
        {
            return capped(built[*own_next].place);
        }
        const auto track_next =
            std::ranges::upper_bound(built, last_marked, std::ranges::less{}, &BuiltNote::place);
        if (track_next != built.end())
        {
            return capped(track_next->place);
        }
        return std::nullopt;
    };

    // THE PHRASE each marked figure belongs to: the MARKED RUN at chain scope. Consecutive marked
    // figures joined by GRIP seams are one asking continued under a moving hand, and the asking
    // ends where the figure's own does — at real silence (a HORIZON seam) or at a figure carrying
    // no mark, because material past the marked run never asked (the TAIL rule above). Without the
    // second bound a song with no bar-long silence is ONE phrase, and an open drone rings toward
    // a mark stated dozens of bars later. Drawn after the donation, which settles membership; a
    // figure donated whole states nothing, so it neither continues a run nor breaks one.
    std::vector<std::optional<Fraction>> figure_last_marked(figures.size());
    std::vector<std::optional<std::size_t>> phrase_of(figures.size());
    std::vector<Fraction> phrase_last_marked;
    std::vector<std::size_t> phrase_voice;
    std::optional<std::size_t> run;
    for (std::size_t figure = 0; figure < figures.size(); ++figure)
    {
        if (opened_by_silence[figure])
        {
            run.reset();
        }
        const std::vector<std::size_t>& members = figures[figure];
        if (members.empty())
        {
            continue;
        }
        std::optional<Fraction>& last_marked = figure_last_marked[figure];
        for (const std::size_t index : members)
        {
            const BuiltNote& entry = built[index];
            if (entry.let_ring)
            {
                last_marked = std::max(last_marked.value_or(entry.place), entry.place);
            }
        }
        if (!last_marked.has_value())
        {
            run.reset();
            continue;
        }
        if (!run.has_value())
        {
            run = phrase_last_marked.size();
            phrase_last_marked.push_back(*last_marked);
            phrase_voice.push_back(built[members.front()].voice);
        }
        const std::size_t phrase = *run;
        phrase_last_marked[phrase] = std::max(phrase_last_marked[phrase], *last_marked);
        phrase_of[figure] = phrase;
    }
    // THE PHRASE'S OWN END: the same question asked over every mark the run carries.
    std::vector<std::optional<Fraction>> phrase_ends(phrase_last_marked.size());
    for (std::size_t phrase = 0; phrase < phrase_ends.size(); ++phrase)
    {
        phrase_ends[phrase] = scope_end(phrase_last_marked[phrase], phrase_voice[phrase]);
    }

    std::vector<std::optional<Fraction>> ends(built.size());
    for (std::size_t figure = 0; figure < figures.size(); ++figure)
    {
        const std::vector<std::size_t>& members = figures[figure];
        // The figure's marked anchor and written reach; a figure with no live mark asks nothing.
        const std::optional<Fraction>& last_marked = figure_last_marked[figure];
        if (!last_marked.has_value())
        {
            continue;
        }
        Fraction written_reach{};
        for (const std::size_t index : members)
        {
            const BuiltNote& entry = built[index];
            if (entry.let_ring)
            {
                written_reach = std::max(written_reach, entry.place + entry.note.sustain);
            }
        }
        // THE FIGURE'S END (\ref scope_end asked of the figure): the first onset the figure's own
        // voice states after its last mark, capped at that mark's audibility horizon — the ring
        // runs exactly as far as the marks ask, and the seam (always at or past this onset) never
        // needs consulting.
        const std::optional<Fraction> scoped =
            scope_end(*last_marked, built[members.front()].voice);
        // THE WRITTEN-REACH FLOOR: a marked member's WRITTEN length is authored truth, not an
        // estimate, so where one member's tie-merged written end outruns the anchor, the figure
        // runs there and the whole stack rings to it — the anchor and the cap bound only what the
        // law is estimating. Without this the lengthen-only application keeps the long written ring
        // while stopping its stackmates short, and the stack stops raggedly — the very disease the
        // one-end law exists to cure. A figure with no answer at all (nothing the voice states
        // after the mark) runs to the written reach alone.
        const Fraction end = std::max(written_reach, scoped.value_or(written_reach));
        // THE OPEN STRING'S LIFT. A figure ends where the HAND stops asking, and an open string is
        // not held by the hand: nothing about the grip moving away stops it sounding, so a grip
        // seam is no answer to it. Its ring runs to the PHRASE's own anchor instead — the first
        // onset the voice states after the whole chain's last mark — and what still stops it is a
        // direct contradiction on ITS OWN string, applied later by the same-string clamp every
        // ring is under (\ref clampSameStringOverlaps), which is physics rather than grammar and
        // needs nothing here.
        //
        // CAPPED BY THE PHRASE'S OWN LAST MARK, exactly as a figure's end is capped by the
        // figure's: the anchor and the cap are one rule (\ref scope_end), and a ring is bounded by
        // the scope that answers it or by nothing coherent at all. Capping instead from each
        // ring's own onset holds every open drone to a single bar while its fretted stackmates
        // ring on to the figure's answer, and stops open rings within one stack raggedly from each
        // other. What bounds the phrase itself is the end of its marked run — real silence, or the
        // first figure that carries no mark — which is why a chain of continued asking cannot run
        // away.
        //
        // LENGTHEN-ONLY, like every other arm of this law: the lift can only carry an open ring
        // PAST its figure's end, never pull one back, so a phrase that answers sooner than the
        // figure does changes nothing.
        //
        // Deliberately the open string alone. A natural harmonic's ring is hand-free by the same
        // physics, but a harmonic's marked ring is rare enough that the lift stays scoped to open
        // notes until such a ring has been checked by eye.
        // Bound once so the presence test and the read are provably the same object.
        const std::optional<std::size_t>& phrase = phrase_of[figure];
        const std::optional<Fraction> phrase_end =
            phrase.has_value() ? phrase_ends[*phrase] : std::nullopt;
        for (const std::size_t index : members)
        {
            const BuiltNote& entry = built[index];
            if (!entry.let_ring)
            {
                continue;
            }
            ends[index] = end;
            const bool open_ring = entry.note.fret == 0 && !entry.note.harmonic_node.has_value();
            if (open_ring && phrase_end.has_value())
            {
                ends[index] = std::max(end, *phrase_end);
            }
        }
    }
    return ends;
}

// Resolves bare slide-in flags (16 from below, 32 from above) into ordinary slides — no new
// notation. The gesture is an ON-BEAT scoop: the ornament is the manner of the note's ATTACK and
// occupies the note's own time slot — notation practice and faithful score players pluck on the
// notated tick at an offset pitch and resolve to the target just after it. The head therefore
// keeps its notated position at a derived approach fret and glides to the notated fret over the
// scoop window: the minimum slide window measured forward from the onset, kept strictly before the
// note's slide chain and slide-out end (bend curves order only against the sustain, so the scoop
// leaves them untouched). Anticipation —
// the approach sounding BEFORE the beat with the target landing ON it — is the before-beat
// grace-with-slide notation, which resolves through the ordinary chain; a bare slide-in must not
// fabricate it.
//
// Guitar Pro gives the gesture no start fret, so the hand events supply it: the natural stream's
// delta arriving at the note, widened to a two-fret minimum, the flag's direction winning
// over a still hand or a contradicting delta. The window needs nothing of its own: the stored scoop
// is struck at its approach fret and states the notated one at its arrival, so the positions a
// reader derives cover both. An open string cannot be slid into, and a start clamped onto the
// notated fret has no travel; both count as unplaceable and stay plain. The transform runs before
// anything reads the stream, so the transformed note is already a slide when the tail rules judge
// it: a slide-in into a held landing keeps its hold like any notated slide.
void resolveSlideIns(
    std::vector<BuiltNote>& built, const std::vector<common::core::FretHandEvent>& hand,
    const common::core::TempoMap& tempo_map, std::vector<std::string>& notes, const int capo)
{
    int unplaceable = 0;
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
        const auto after = common::core::firstFretHandEventAfter(hand, entry.place);
        if (after != hand.begin())
        {
            const auto landing = after - 1;
            if (landing != hand.begin() && landing->front == entry.place)
            {
                const int delta = (landing - 1)->window.fret - landing->window.fret;
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
        Fraction window = minimumSlideWindow(tempo_map, entry.place);
        if (const Fraction first_travel = firstStatedFretOffset(note);
            first_travel.numerator > 0 && window >= first_travel)
        {
            window = first_travel * Fraction{1, 2};
        }
        // A slide-out is the other fret-travel payload the scoop must stay strictly before:
        // on a short note the floored window can reach the slide-out, which ends the ring, and a
        // stated fret at or past that end fails chart validation.
        if (common::core::endStatedFretOrNull(note) != nullptr && window >= note.sustain)
        {
            window = note.sustain * Fraction{1, 2};
        }
        if (note.sustain < window)
        {
            note.sustain = window;
        }

        // The arrival is a fret STATEMENT at the scoop's end, merged into whatever already stands
        // at that instant rather than inserted beside it — a bend the source wrote there is the
        // same moment, not a competing one.
        keyframeAt(note, window).fret = note.fret;
        note.fret = start;
    }
    if (unplaceable > 0)
    {
        notes.push_back(
            std::to_string(unplaceable) +
            " slide-ins had no representable start and were left plain");
    }
}

// Chooses every unpitched slide-out's exit fret from the hand's next move. When the next event of
// the natural stream departs in the slide-out's direction AND arrives by the very next onset, the
// slide-out IS the departure: the exit fret rides that travel (widened to the slide-in rule's
// two-fret minimum). Otherwise the slide-out keeps the fixed four-fret gesture it was built with,
// as does one ending past the next onset.
//
// Every question here — where the slide-out ends, which fret it leaves from, whether it still
// clears the next onset — is asked of the STORED note, whose ring is the truth; spacing before the
// next head is display's alone.
void resolveSlideOutExits(
    std::vector<BuiltNote>& built, const std::vector<bool>& arrives_into,
    const std::vector<common::core::FretHandEvent>& hand, const int capo)
{
    for (std::size_t index = 0; index < built.size(); ++index)
    {
        BuiltNote& entry = built[index];
        const ChartNote& note = entry.note;
        // A scrape's slide-out is authored travel, not an exit to resolve.
        //
        // THE SLIDE-OUT IS THE RESOLVED FACT: a shift slide's ARRIVAL is the same statement at
        // the same place and no exit at all — the hand is landing on the stop the next head
        // takes, not leaving the board (\ref common::core::arrivesIntoNextHead).
        const bool arrives = arrives_into[index];
        const int* const slide_out = common::core::slideOutFretOrNull(note, arrives);
        if (slide_out == nullptr || isScrape(note.attack))
        {
            continue;
        }
        // Where the slide-out DEPARTS from, which is the note-local question: the guard above has
        // already established that this end slides out, so the last stop stated inside the ring is
        // the fret the gesture leaves.
        const int departing = common::core::fretBeforeEnd(note);
        const bool downward = *slide_out < departing;
        const auto after = common::core::firstFretHandEventAfter(hand, entry.place);
        if (after == hand.begin())
        {
            continue;
        }
        const auto active = after - 1;

        std::size_t next_note = index + 1;
        while (next_note < built.size() && built[next_note].place <= entry.place)
        {
            ++next_note;
        }
        // The gesture ends where the ring does, which is what a slide-out having no offset of its
        // own means. One running past the next onset keeps its default exit fret.
        const bool has_next = next_note < built.size();
        if (has_next && built[next_note].place < entry.place + note.sustain)
        {
            continue;
        }

        // Departure: the next event's move serves the very next onset and agrees with the
        // slide-out's direction.
        const int delta = after == hand.end() ? 0 : after->window.fret - active->window.fret;
        const bool departs = delta != 0 && (delta < 0) == downward && has_next &&
                             !(built[next_note].place < after->front);
        if (departs)
        {
            const int travel = widenedToMinimumTravel(delta, downward);
            common::core::setSlideOut(
                entry.note,
                slideOutExitClearOfNextHead(
                    entry,
                    nextHeadOnString(built, index, nullptr),
                    std::clamp(
                        departing + travel,
                        common::core::firstPlayableFret(capo),
                        common::core::g_max_fret),
                    !downward,
                    capo));
        }
    }
}

// THE LATTICE AT THE COMMIT POINT: every instant the chart stores — an onset, a ring's end, a
// keyframe — lands on the tick lattice, the finest position a chart may state.
// Guitar Pro places some between two ticks (a septuplet's rhythm, a bend point's percentage) and
// the builder derives more from those, so they are rounded here, once, rather than at each source:
// the same exact instant always rounds to the same tick (the tick grid, common::core::nearestTick),
// so a ring ending exactly on the next onset still ends exactly on it and every relation read above
// survives, and an imported septuplet lands on the very tick a septuplet grid's line does.
//
// Two keyframes of one ring that round onto one tick stay apart, the later taking the next tick,
// so a hold followed by a release keeps its hold; only where no tick is left inside the ring does
// the later fold into the earlier, its stated channels winning. A ring keeps at least one tick.
//
// Every instant is rounded as a PLACE on the whole-note axis, where the lattice lies, and a
// duration is then the difference of two rounded places — a whole number of ticks by construction.
void roundOntoTickLattice(std::vector<ChartNote>& notes, const common::core::TempoMap& tempo_map)
{
    const Fraction tick = common::core::g_tick_quantum_note_value;
    for (ChartNote& note : notes)
    {
        const Fraction place = tempo_map.wholeNotePositionAt(note.position);
        const Fraction onset = common::core::nearestTickPlace(place);
        Fraction end = common::core::nearestTickPlace(place + note.sustain);
        if (!(onset < end))
        {
            end = onset + tick;
        }
        std::vector<Keyframe> keyframes;
        keyframes.reserve(note.keyframes.size());
        for (const Keyframe& keyframe : note.keyframes)
        {
            const Fraction previous = keyframes.empty() ? onset : onset + keyframes.back().offset;
            Fraction at = common::core::nearestTickPlace(place + keyframe.offset);
            if (!(previous < at))
            {
                at = previous + tick;
            }
            if (end < at && !keyframes.empty())
            {
                Keyframe& kept = keyframes.back();
                if (keyframe.fret.has_value())
                {
                    kept.fret = keyframe.fret;
                }
                if (keyframe.bend.has_value())
                {
                    kept.bend = keyframe.bend;
                }
                // The kept keyframe's leg is the one that sounds, so its width stands.
                continue;
            }
            Keyframe rounded = keyframe;
            rounded.offset = at - onset;
            keyframes.push_back(rounded);
        }
        note.position = tempo_map.gridPositionAtWholeNote(onset);
        note.sustain = end - onset;
        note.keyframes = std::move(keyframes);
    }
}

// Builds one track's chart: tie merging, technique mapping, bends, slide resolution and sustain
// normalization. It stores no fret-hand positions: every one is derived at read from the finished
// notes. The tempo map places mid-sustain slide-keyframe positions on the musical grid.
//
// The conversion notes and the let-ring report are both SONG-level accumulators the track
// adds to, for the one reason: a reader asks what the whole import did, and a per-track answer
// would have to be summed by every caller.
[[nodiscard]] Chart buildChart(
    const GpTrack& track, const std::vector<GpMasterBar>& master_bars,
    const common::core::TempoMap& tempo_map, std::vector<std::string>& notes,
    GpLetRingReport& let_ring)
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
        chart.tuning.strings.push_back(midiNoteName(midi, track.tuning_prefers_flats));
    }
    chart.tuning.capo = std::clamp(track.capo, 0, common::core::g_max_capo);
    if (chart.tuning.capo != track.capo)
    {
        notes.push_back(
            "capo at fret " + std::to_string(track.capo) + " is out of range and reads as " +
            std::to_string(chart.tuning.capo));
    }

    const std::vector<NoteEvent> events =
        collectEvents(track, master_bars, tempo_map, chart.tuning.capo, notes);

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
        const Fraction event_end = event.place + event.duration_whole;

        if (source.tie_destination)
        {
            // Continuations extend the open note on the string instead of creating an onset.
            const auto open = open_note_per_string.find(source.string);
            if (open != open_note_per_string.end())
            {
                BuiltNote& origin = built[open->second];
                if (event_end > ringEndOf(origin))
                {
                    origin.note.sustain = event_end - origin.place;
                }
                // Where this continuation begins on the merged ring: the anchor its own
                // per-segment statements rebase onto, for the vibrato channel and the bend curve
                // alike.
                const Fraction base = event.place - origin.place;
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
                        if (event.duration_whole < point.offset)
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
                            keyframeAt(origin.note, point.offset).bend = point.semitones;
                        }
                    }
                }
                // A slide notated on the tied continuation belongs to the merged note (policy
                // rule 15): the flags fold in rather than vanishing with the merged onset, and
                // the continuation's own onset marks where the glide leaves from.
                origin.slide_flags |= source.slide_flags;
                if (source.slide_flags != 0)
                {
                    origin.slide_from_place = event.place;
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
        entry.place = event.place;
        entry.gp_string = source.string;
        entry.slide_flags = source.slide_flags;
        entry.voice = event.voice;
        entry.let_ring = event.let_ring;

        ChartNote& note = entry.note;
        note.position = tempo_map.gridPositionAtWholeNote(event.place);
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
        note.sustain = event.duration_whole;
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
            // that decide a predecessor's FRET AT THE RING'S END are finally built.
            note.attack = NoteAttack::Legato;
        }

        // Both marks carried through independently, because the score states them independently:
        // a dead string inside a palm-muted chord wears "Muted" and "PalmMuted" at once, and a
        // single mute axis would have to drop one of them to fit.
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
            // Labels are conventional roundings, so each is resolved to the true node it names —
            // a touch even slightly off a node chokes the harmonic — through the one enumerator
            // the editor's harmonic verb reads too. An empty candidate list is a label that names
            // NOTHING (the integer frets with no harmonic near them: 1, 11, 13, ...); with a
            // charter to ask, the verb offers the list, and import takes the nearest of it.
            //
            // Artificial and tapped harmonics import as the record they are, and the chart
            // normalization this build ends with reduces each to a plain note at its pressed stop
            // and reports it (ChartRepair::DisabledHarmonic), because those two forms are not
            // supported yet; nothing here restates that rule.
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
                    // The tapping finger only touches the node, so the fret the score states stays
                    // the note's PRESSED stop — the fretting hand's — with the node above it. The
                    // same record an artificial harmonic writes, differing in the attack alone.
                    note.attack = NoteAttack::Tap;
                }
                // The stop the harmonic speaks from — the note's (already capo-shifted,
                // absolute) fret, or the capo when the string is open — asked of the same
                // authority the node-past-the-stop rule validates against.
                const int stop_fret = common::core::physicalStopFret(note, chart.tuning.capo);
                // With no usable label the octave is the default: the 2nd partial is the
                // lowest-order harmonic available at any fret and so the easiest to ring. Using
                // the *fret* as a label here would read a stop as a partial number.
                double offset = 12.0;
                bool defaulted = true;
                if (source.harmonic_fret.has_value())
                {
                    // Bound once so the presence test and the reads below are provably one object.
                    const double label = *source.harmonic_fret;
                    if (const std::vector<common::core::HarmonicNodeCandidate> candidates =
                            common::core::harmonicNodeCandidates(
                                label, common::core::g_max_snapped_partial);
                        !candidates.empty())
                    {
                        offset = candidates[common::core::nearestHarmonicNode(candidates, label)]
                                     .position;
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
                // and lands on the real stop — capo + offset, which is exactly right because GP's
                // frame is capo-relative.
                const double notated =
                    source.harmonic_fret.value_or(static_cast<double>(source.fret));
                const std::vector<common::core::HarmonicNodeCandidate> candidates =
                    common::core::harmonicNodeCandidates(
                        notated, common::core::g_max_snapped_partial);
                if (!candidates.empty())
                {
                    const double offset =
                        candidates[common::core::nearestHarmonicNode(candidates, notated)].position;
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

        // A tap needs somewhere to strike, whichever hand delivers it: a fret, or a harmonic's
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
            // out over that: a note an ornament shortened plays the part of its bend that still
            // sounds rather than the whole curve faster. The curve is therefore the one payload
            // that can reach past the ring, and it is trimmed back at the end of the build, where
            // the ring is finally settled — trimming it here would cut it against a ring the
            // let-ring pass is still going to lengthen.
            applyBendCurve(note, buildBendPoints(*source.bend, notatedDuration(event), notes));
        }

        // NO CLAIM IS EVER AUTHORED HERE, and that is the whole of what the import states about
        // the fretting hand's silent stops. The accumulation law derives a rolled chord's
        // figure from its members' own rings, so nothing here needs to claim one: a held stop is a
        // record the CHARTER writes and the import never does.

        // Duplicate onsets (two voices striking one string together) keep the first note.
        if (!built.empty())
        {
            const BuiltNote& previous = built.back();
            if (previous.place == entry.place && previous.note.string == note.string)
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
    // Slide-outs are unpitched.
    std::vector<bool> merged_away(built.size(), false);

    // Pick-slide carriers (Slide flags 64 down / 128 up) convert IN PLACE into pick-slide
    // notes before any slide chain resolves: the dead carrier is Guitar Pro's encoding vehicle for
    // the gesture, so it sheds its mute and gains the attack plus the corpus-derived default path
    // (down 17 -> 3, up the mirror) across the notated span, ready for the user to reshape.
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
        const Fraction onset = built[index].place;
        const bool upward = (built[index].slide_flags & 128) != 0;
        const auto notated_span = [&tempo_map](const BuiltNote& entry) {
            return entry.note.sustain.numerator > 0 ? entry.note.sustain
                                                    : minimumSlideWindow(tempo_map, entry.place);
        };
        // First pass over the onset: take the gesture's longest span and drop opposed directions.
        // Flags stay set here so the conversion pass can still find the survivors.
        Fraction span = notated_span(built[index]);
        std::size_t scan = index + 1;
        for (; scan < built.size() && built[scan].place == onset; ++scan)
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
            // The suppression set lives in savedChartNote alone. A second copy of it here is free
            // to drift — clearing the emphasis, say, when an accented scrape is legal and
            // meaningful (an aggressively played one) — and would silently discard a mark the
            // score made.
            note = common::core::savedChartNote(note);
            // Carriers are dead strings with meaningless frets, so the import owns the start too;
            // the editor's toggle keeps a real note's fret instead. The start is floored above the
            // capo like every fret a slide gesture names (a scrape's start, its turnarounds, and
            // its terminal all sit at or above the first playable fret — the pick travels the
            // sounding string, and a scrape at the nut is no scrape). The default path floors its
            // own terminal the same way.
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
        if (entry.slide_from_place.has_value())
        {
            const Fraction hold_offset = *entry.slide_from_place - entry.place;
            if (hold_offset.numerator > 0)
            {
                keyframeAt(note, hold_offset).fret = note.fret;
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
                // The next record on this string that still exists: a merged-away successor was
                // folded into its predecessor, so it is no landing.
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
            if (next->note.fret == 0)
            {
                // The landing is the open string: nothing is pressed to glide to, and a keyframe
                // at fret 0 is refused, so the gesture degrades to the unpitched slide-out exactly
                // like a missing landing — which is what a slide down toward the open string
                // physically is. The landing keeps its own onset.
                flags |= 4;
                break;
            }
            const Fraction gap = next->place - entry.place;
            if ((flags & 2) != 0 && (flags & 1) == 0)
            {
                // Legato: the landing continues this note. Keyframe at the junction, sustain
                // through the target's notated end, techniques folded, chain continued.
                keyframeAt(note, gap).fret = next->note.fret;
                if (ringEndOf(entry) < ringEndOf(*next))
                {
                    note.sustain = ringEndOf(*next) - entry.place;
                }
                // The landing's vibrato lands ON the junction it arrives at — the same coupling the
                // bend fold below relies on, and the sign-off's anchor for Guitar Pro's anchorless
                // flag. A landing that does NOT vibrate begins an unvibrated leg there.
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
                        keyframeAt(note, point.offset).bend = point.semitones;
                    }
                }
                merged_away[next_index] = true;
                glide_fret = next->note.fret;
                flags = built[next_index].slide_flags;
                search_from = next_index;
                continue;
            }

            // Shift: the glide states the re-picked landing's fret AT the ring's END, exactly on
            // that head (policy rule 13). Nothing marks it as the pitched arrival a shift slide
            // is, and nothing has to — the chart PROVES it: an end statement naming the same stop
            // the next head on this string is struck at, at the same instant, is a glide into
            // position and a pick (\ref common::core::arrivesIntoNextHead). Guitar Pro states no
            // arrival time, and none is synthesized: the arrival's instant is the head's own, which
            // is what the hands did. The landing keeps its own onset and head; the arrival stays
            // at its stored instant and the ink stops one margin before the head
            // (`chartPresentation` rule 1).
            //
            // A slide-out the chain resolved earlier cannot outlive the gesture it slides out of:
            // its fret would become an interior stop once the ring grows past it, so the end's
            // fret statement is cleared before the arrival takes that instant. A BEND standing at
            // that same end is not cleared and rides on — an arrival and a bend coexist, both
            // completing as the ring ends.
            common::core::clearSlideOut(note);
            // The ring only ever GROWS: the origin keeps ringing until the landing re-picks the
            // string — the note sounds while it travels — and the same-string clamp is what bounds
            // it at the landing. The growth comes first because the end's own writer stamps the
            // ring's end on what it states (`setEndStatement`), and it overlays rather than
            // replaces, so a tied curve's final point already standing there keeps its bend.
            if (note.sustain < gap)
            {
                note.sustain = gap;
            }
            static_cast<void>(common::core::setEndStatement(
                note, Keyframe{.offset = {}, .fret = next->note.fret, .bend = {}, .vibrato = {}}));
            flags = 0;
            break;
        }

        if ((flags & (4 | 8)) != 0)
        {
            const bool upward = (flags & 8) != 0;
            // Four frets of travel, held onto the playable board at both ends: the floor is the
            // first fret above the capo, never the nut — an exit below the floor is a form the
            // rules refuse, and producing one here would surface only at the track's validation.
            const int target =
                upward
                    ? std::min(glide_fret + 4, common::core::g_max_fret)
                    : std::max(glide_fret - 4, common::core::firstPlayableFret(chart.tuning.capo));
            // The slide-out ends the RING, so what the gesture needs is a ring end strictly after
            // any chain keyframe's stated fret — otherwise the slide-out would leave from a
            // position stated at the very instant it ends — and one minimum gesture window past it
            // is the smallest legal answer. A bend or vibrato at the end does not bind it: a
            // slide-out states its fret and nothing else, so a statement standing where the string
            // is let go says nothing by the model's own law (shedEndStatementVibrato). The
            // four-fret exit is provisional: resolveSlideOutExits rides the hand's next move
            // instead when it agrees with the flag's direction. The answer is strictly positive
            // without a floor of its own: a sustainless note's zero never exceeds the last stated
            // fret's offset, so it comes back a whole minimum window.
            common::core::clearSlideOut(note);
            Fraction last_stated_fret{};
            for (const Keyframe& keyframe : note.keyframes)
            {
                if (keyframe.fret.has_value())
                {
                    last_stated_fret = keyframe.offset;
                }
            }
            if (note.sustain <= last_stated_fret)
            {
                note.sustain = last_stated_fret +
                               minimumSlideWindow(tempo_map, entry.place + last_stated_fret);
            }
            // Read after the ring is settled, because the relation the exit must stay clear of
            // asks whether that ring reaches the next head.
            common::core::setSlideOut(
                note,
                slideOutExitClearOfNextHead(
                    entry,
                    nextHeadOnString(built, index, &merged_away),
                    target,
                    upward,
                    chart.tuning.capo));
        }
    }

    // A legato landing is not an onset; drop them before fret-hand generation, the same-string
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

    // A bare slide-in's approach fret is chosen from where the hand arrives at the note, so it is
    // asked of a derivation over the NATURAL stream — slide-ins still plain notes at their notated
    // positions — and before the rings are final, which is why it cannot wait for the positions a
    // reader derives from the finished chart (plan 60 §9 Q11). The import stores no positions: it
    // authors nothing, so every position is derived at read. The same natural stream later chooses
    // each slide-out's exit fret.
    //
    // Resolved on the stream the slide passes have left — the shift branch grew each origin's ring
    // to exactly the gap, so the arrivals the derivation merges at their landings already hold
    // before the same-string clamp runs. THE FIRST OF THREE WALKS, each after a pass that MOVED a
    // ring: none can be shared, and every one says below it what changed since the last.
    //
    // The rings are its only holds: the spans measure their margins from places on the tick
    // lattice, which the stream reaches only at the commit point below, so no span is known yet.
    const common::core::ChartConnections slide_connections =
        common::core::chartConnections(storedNotes(built), tempo_map);
    const std::vector<common::core::FretHandEvent> natural_hand =
        common::core::deriveFretHandEvents(
            slide_connections,
            common::core::fretHandHolds(
                slide_connections, common::core::ChartShapes{}, chart.tuning.capo),
            tempo_map,
            chart.tuning.capo,
            {});
    resolveSlideIns(built, natural_hand, tempo_map, notes, chart.tuning.capo);

    // THE LET-RING FIGURE LAW's application: every marked note rings to its FIGURE's end —
    // max(written, min(same-string clamp, figure end)) — and the three bounds compose without any
    // of them restating another: this pass lengthens only (written is the law's floor), and the
    // clamp below is physics and the strongest bound.
    //
    // It runs here rather than where the ring was first established because the mark yields to a
    // note that states its OWN end — an unpitched slide-out IS the ring's end by definition
    // (physically forced, not stylistic), and the gesture is only resolved above.
    //
    // WELL-FOUNDED, ONE PASS: the figure walk reads onsets and statements only (\ref
    // letRingFigureEnds), so nothing here reads a span or a ring and there is no circularity to
    // unwind. The claims resolve before any ring is lengthened for the same reason — the walk is
    // a pure function of the written stream.
    //
    // A note that ABSORBED a same-string merge — a tie continuation, a legato-slide landing —
    // extends here like any other marked note (rule 1 of the baseline law: ties combine into a
    // single note at its true WRITTEN duration, and the mark then extends that note normally).
    // Guitar Pro itself audibly rings tied let-ring notes past the written duration.
    //
    // The connections are RESOLVED AGAIN because `resolveSlideIns` has since moved rings: a scoop
    // shorter than its floored window lengthens the landing's sustain and plants a keyframe inside
    // it, so the adjacency and the end statement of every scooped note are not what the walk above
    // saw.
    const common::core::ChartConnections let_ring_connections =
        common::core::chartConnections(storedNotes(built), tempo_map);
    const std::vector<std::optional<int>> let_ring_planted =
        common::core::chartPlantedStops(let_ring_connections);
    const std::vector<std::optional<Fraction>> figure_ends =
        letRingFigureEnds(built, let_ring_planted, tempo_map);
    int let_ring_marks_kept = 0;
    // What each mark's ring was BEFORE the law spoke, remembered rather than counted now,
    // because the clamp below can take an extension back whole; the report belongs to the rings
    // as the chart finally stores them.
    std::vector<std::pair<std::size_t, Fraction>> let_ring_written;
    for (std::size_t index = 0; index < built.size(); ++index)
    {
        BuiltNote& entry = built[index];
        if (!entry.let_ring)
        {
            continue;
        }
        if (common::core::endStatedFretOrNull(entry.note) != nullptr)
        {
            ++let_ring_marks_kept;
            continue;
        }
        let_ring_written.emplace_back(index, entry.note.sustain);
        const std::optional<Fraction>& end = figure_ends[index];
        if (end.has_value())
        {
            const Fraction ring = *end - entry.place;
            // LENGTHEN ONLY: written is the law's floor, and truncation is the same-string
            // clamp's job below — physics this pass deliberately does not restate.
            if (ring > entry.note.sustain)
            {
                entry.note.sustain = ring;
            }
        }
    }

    // Every synthesis that can lengthen a ring is done, so the stored stream takes its final
    // shape here — the clamp and the payload trimmed to the ring — and the one pass below reads
    // those settled rings: a slide-out's exit fret is chosen where the stored gesture ends.
    // Hand-posture spans are NOT an import decision: they are derived from the finished notes
    // wherever they are read (common/core's deriveChartShapes), so there is nothing to run here and
    // nothing to report.
    clampSameStringOverlaps(built, tempo_map);

    // The let-ring report, now that the clamp has had its say: a ring longer than its written
    // duration is one the figure lengthened and the physics let stand; a ring at exactly its
    // written duration is a mark that changed nothing — the census's mis-seam detector (\ref
    // GpLetRingReport), since under this law the figure end is the only thing that ever lengthens
    // a marked ring. A ring the clamp cut BELOW written is neither: physics overrode the
    // notation, and that population is the clamp's to report.
    int figure_extended = 0;
    int figure_at_written = 0;
    for (const auto& [index, written] : let_ring_written)
    {
        if (built[index].note.sustain > written)
        {
            ++figure_extended;
        }
        else if (built[index].note.sustain == written)
        {
            ++figure_at_written;
        }
    }
    let_ring.extended += figure_extended;
    let_ring.at_written += figure_at_written;
    if (figure_extended > 0)
    {
        notes.push_back(
            std::to_string(figure_extended) +
            " let-ring rings were extended to their figure's end");
    }
    // The law's REACH, stated rather than assumed: a mark on a note that states its own end — an
    // unpitched slide-out — keeps the ring the build gave it.
    if (let_ring_marks_kept > 0)
    {
        notes.push_back(
            std::to_string(let_ring_marks_kept) +
            " let-ring marks kept their shipped rings (the note states its own end)");
    }

    // The RELATION the stored stream proves: a fret at a ring's end is a slide-out or a shift
    // slide's arrival, and the pass below must not ride an arrival as an exit.
    // RESOLVED AGAIN because two passes have since moved rings: the let-ring law lengthened every
    // marked ring to its figure's end, and `clampSameStringOverlaps` then cut each ring back to the
    // next strike on its string and trimmed the payload to it — which is exactly where a slide-out
    // or an arrival comes to stand, so no earlier walk can answer for this one.
    const common::core::ChartConnections connections =
        common::core::chartConnections(storedNotes(built), tempo_map);

    // Slide-out exit frets follow the hand's next move where it agrees: the natural stream.
    resolveSlideOutExits(built, connections.arrives_into, natural_hand, chart.tuning.capo);

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
    roundOntoTickLattice(chart.notes, tempo_map);

    // Import is a commit point, so the chart leaves here in its normal form through the ONE
    // normalizer every load path calls: each note sheds what it cannot execute, every range is
    // brought onto the board, and the settle sweep runs last over the finished stream — released
    // frets after every slide chain, holds after the same-string clamp. The spans a reader derives
    // therefore describe the SETTLED stream, which is the stream the surfaces draw: a claim the
    // chart cannot justify plays as a plain pick, so it must not split a box from a neighbouring
    // strum that plays the same way. The rules live beside their repairs in `chart_rules`, because
    // a second list kept here would drift from that one, and a rule missing here lets a dead note
    // carrying a bend reach validation intact and fail the WHOLE song's import. Counted by rule
    // rather than listed, like every other import conversion: an import converts wholesale, and a
    // position list for a dense score would be hundreds of lines.
    //
    // The builder is an AUTHOR, and an author writes no keyframe that says nothing (the keyframe
    // commit law, `keyframeSaysNothingNew`): a Guitar Pro bend curve's plateau and trailing points
    // repeat its values, and the merges above fold more of them. Shed here, before the load repair
    // below counts what it had to fix, so the report names the score's conversions and not the
    // builder's own tidying.
    for (common::core::ChartNote& note : chart.notes)
    {
        static_cast<void>(common::core::stripSilentKeyframes(note));
    }
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

    song.tempo_map = buildTempoMap(score, song.notes);

    // Section markers live on the master bars shared by every track, so they build once at the
    // song level rather than being duplicated into each track's chart.
    for (std::size_t measure = 0; measure < score.master_bars.size(); ++measure)
    {
        // A song section is a NAMED place in the structure, so an unlabelled section mark states
        // no section here. Bound to one reference so the guard and the read are the same object.
        // "Unlabelled" is measured by the shared section-name trim, not by emptiness: a bar whose
        // label is nothing but spaces states no name either, and importing it would build a section
        // list the section rules refuse and the package could never be read back.
        const std::optional<std::string>& section = score.master_bars[measure].section;
        const std::string section_name =
            section.has_value() ? common::core::trimmedSongSectionName(*section) : std::string{};
        if (!section_name.empty())
        {
            const GridPosition downbeat =
                common::core::measureDownbeat(static_cast<int>(measure) + 1);
            song.sections.push_back(
                common::core::SongSection{
                    .position = downbeat,
                    .name = section_name,
                });
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
            buildChart(track, score.master_bars, song.tempo_map, song.notes, song.let_ring);

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
