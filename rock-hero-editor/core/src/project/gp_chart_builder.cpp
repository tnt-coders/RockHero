#include "project/gp_chart_builder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <compare>
#include <cstddef>
#include <map>
#include <optional>
#include <rock_hero/common/core/chart/chart_rules.h>
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

// The chart's Fraction is a value type without arithmetic; the builder's rationals stay small
// (beat offsets and sustains), so plain cross-multiplication stays inside int range.
[[nodiscard]] constexpr Fraction addFractions(Fraction lhs, Fraction rhs) noexcept
{
    return Fraction{
        lhs.numerator * rhs.denominator + rhs.numerator * lhs.denominator,
        lhs.denominator * rhs.denominator
    };
}

[[nodiscard]] constexpr Fraction subtractFractions(Fraction lhs, Fraction rhs) noexcept
{
    return addFractions(lhs, Fraction{-rhs.numerator, rhs.denominator});
}

[[nodiscard]] constexpr Fraction multiplyFractions(Fraction lhs, Fraction rhs) noexcept
{
    return Fraction{lhs.numerator * rhs.numerator, lhs.denominator * rhs.denominator};
}

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
        const Fraction offset = multiplyFractions(
            percentFraction(std::clamp(point.offset_percent, 0.0, 100.0)), sustain);
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

// Collects the timed note events of one track across bars and voices, skipping grace beats.
[[nodiscard]] std::vector<NoteEvent> collectEvents(
    const GpTrack& track, const MeasureGrid& grid, std::vector<std::string>& notes)
{
    std::vector<NoteEvent> events;
    int overfull = 0;
    for (std::size_t bar_index = 0; bar_index < track.bars.size(); ++bar_index)
    {
        const auto measure_index = std::min(bar_index, grid.beats_per_measure.size() - 1);
        const int beats_in_bar = grid.beats_per_measure[measure_index];
        const int denominator = grid.denominator[measure_index];

        for (const std::vector<GpBeat>& voice : track.bars[bar_index].voices)
        {
            Fraction position_beats{};
            for (const GpBeat& beat : voice)
            {
                if (beat.grace)
                {
                    continue;
                }

                const Fraction duration_beats =
                    multiplyFractions(beat.duration_whole, Fraction{denominator});
                const Fraction onset = position_beats;
                position_beats = addFractions(position_beats, duration_beats);
                if (beat.notes.empty())
                {
                    continue;
                }
                if (onset >= Fraction{beats_in_bar})
                {
                    ++overfull;
                    continue;
                }

                // Split the in-bar beat position into the whole beat and its sub-beat offset.
                const int whole_beats = onset.numerator / onset.denominator;
                const Fraction offset = subtractFractions(onset, Fraction{whole_beats});
                for (const GpNote& source : beat.notes)
                {
                    NoteEvent event;
                    event.measure = static_cast<int>(bar_index) + 1;
                    event.beat = whole_beats + 1;
                    event.offset = offset;
                    event.duration_beats = duration_beats;
                    event.global_beat = addFractions(
                        Fraction{grid.first_global_beat[measure_index] + whole_beats}, offset);
                    event.source = source;
                    event.tremolo = beat.tremolo;
                    events.push_back(std::move(event));
                }
            }
        }
    }
    if (overfull > 0)
    {
        notes.push_back(std::to_string(overfull) + " beats overflowed their bar and were dropped");
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
};

// Reports whether the note carries a technique that lives on its sustain tail. These notes keep
// their tails through the sub-beat drop rule: removing the tail would remove the technique.
[[nodiscard]] bool hasSustainTechnique(const ChartNote& note)
{
    return !note.bend.empty() || !note.slides.empty() || note.vibrato || note.tremolo;
}

// Normalizes imported sustains for chart readability (import policy, user rules 2026-07-21).
// The maintained plain-English spec is "GP chart normalization policy" in
// docs/developer/the-project-lifecycle.md — tweak behavior there first, then re-align this code.
//
// 1. A tail is trimmed to end at least the minimum-note-distance margin — 1/16 of a whole note,
//    the same settled margin the editor's duration verb clamps to — before the next onset on ANY
//    string. Same-onset chord members never bind each other, and GP import emits no shape spans,
//    so the editor rule's span-sibling exemption has nothing to exempt here. Trimming never
//    clips a bend or slide payload: the tail floors at the last payload offset, so a slide still
//    reaches its target note (exact adjacency stays legal per 40-Q2-B).
// 2. After trimming, a note with no sustain-carried technique whose tail is shorter than one
//    beat loses the tail entirely: Guitar Pro gives every note its full notated duration, and a
//    sub-beat effect-free tail reads as noise in a chart rather than as a deliberate sustain.
//
// Both rules are import normalization only — the editor never rewrites spacing the user authored.
void normalizeImportedSustains(
    std::vector<BuiltNote>& built, const MeasureGrid& grid, std::vector<std::string>& notes)
{
    int trimmed = 0;
    int dropped = 0;
    for (std::size_t index = 0; index < built.size(); ++index)
    {
        ChartNote& note = built[index].note;
        if (note.sustain.numerator > 0)
        {
            std::size_t next = index + 1;
            while (next < built.size() && built[next].global_beat == built[index].global_beat)
            {
                ++next;
            }
            if (next < built.size())
            {
                const auto measure_index = static_cast<std::size_t>(note.position.measure - 1);
                const Fraction margin{grid.denominator[measure_index], 16};
                const Fraction limit = subtractFractions(
                    subtractFractions(built[next].global_beat, built[index].global_beat), margin);
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
                    if (target < note.sustain)
                    {
                        note.sustain = target;
                        ++trimmed;
                    }
                }
            }
        }
        if (note.sustain.numerator > 0 && !hasSustainTechnique(note) && note.sustain < Fraction{1})
        {
            note.sustain = Fraction{};
            ++dropped;
        }
    }
    if (trimmed > 0)
    {
        notes.push_back(
            std::to_string(trimmed) + " sustains were trimmed to the minimum note distance");
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
// renders as a chord box over repeated strums. ANY articulation difference is a new chord (user
// rule 2026-07-21): span continuity compares each string's whole note with only its position and
// duration neutralized, so attack (hammer/pull/tap/slap/pop), muting, harmonics, vibrato,
// tremolo, accent, bends, and slides — and any technique added to ChartNote later — all split
// the span, while strum durations never do. The template table stays deduplicated by frets
// alone (the hand posture is identical; techniques render on the notes). Every derived span
// starts at a multi-note onset, so the projection's
// arrival rule always reads it as a chord box, never an arpeggio bracket; no arpeggio spans are
// derived (broken-chord grouping needs the corpus-informed pass). Derived templates are unnamed
// and unfingered. The maintained plain-English spec is "GP chart normalization policy" in
// docs/developer/the-project-lifecycle.md.
void deriveChordShapes(const std::vector<BuiltNote>& built, Chart& chart)
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
    };
    std::optional<OpenSpan> open;
    const auto close_span = [&chart, &open]() {
        if (open.has_value())
        {
            chart.shapes.push_back(
                ChartShape{
                    .position = open->position,
                    .sustain = subtractFractions(open->end_beat, open->start_beat),
                    .chord = open->chord,
                });
            open.reset();
        }
    };

    std::size_t index = 0;
    while (index < built.size())
    {
        std::size_t onset_end = index;
        Fraction notated_end = built[index].end_global_beat;
        std::vector<StringArticulation> articulation(string_count);
        std::size_t struck = 0;
        while (onset_end < built.size() && built[onset_end].global_beat == built[index].global_beat)
        {
            const ChartNote& note = built[onset_end].note;
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
            ++onset_end;
        }
        if (struck >= 2)
        {
            std::vector<std::optional<int>> posture(string_count);
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                if (articulation[string_index].has_value())
                {
                    posture[string_index] = articulation[string_index]->fret;
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
            }
            else
            {
                close_span();
                open = OpenSpan{
                    .chord = entry->second,
                    .articulation = std::move(articulation),
                    .position = built[index].note.position,
                    .start_beat = built[index].global_beat,
                    .end_beat = notated_end,
                };
            }
        }
        else
        {
            // Any intervening non-chord onset ends the held posture.
            close_span();
        }
        index = onset_end;
    }
    close_span();
}

// Generates the fret-hand position track with a minimal-shift window walk — the deliberately
// simple starting algorithm (user decision 2026-07-21): the hand covers a [fret, fret+width-1]
// window (width four unless one onset spans wider), open strings never constrain it, and when an
// onset's fretted notes fall outside the window the anchor moves the shortest distance that
// covers them. The maintained plain-English spec is "GP chart normalization policy" in
// docs/developer/the-project-lifecycle.md — tweak behavior there first, then re-align this code.
// Known limits (no lookahead, no phrase segmentation, mid-sustain slide targets uncovered until
// the next onset) and the corpus-derived generator that should eventually replace this are
// recorded in docs/plans/todo/fhp-corpus-derived-generation.md.
[[nodiscard]] std::vector<common::core::FretHandPosition> generateFretHandPositions(
    const std::vector<ChartNote>& chart_notes)
{
    std::vector<common::core::FretHandPosition> positions;
    int anchor = 0;
    int width = 4;
    std::size_t index = 0;
    while (index < chart_notes.size())
    {
        std::size_t onset_end = index;
        int min_fret = 0;
        int max_fret = 0;
        while (onset_end < chart_notes.size() &&
               chart_notes[onset_end].position == chart_notes[index].position)
        {
            const int fret = chart_notes[onset_end].fret;
            if (fret > 0)
            {
                min_fret = min_fret == 0 ? fret : std::min(min_fret, fret);
                max_fret = std::max(max_fret, fret);
            }
            ++onset_end;
        }
        if (min_fret > 0 &&
            (positions.empty() || min_fret < anchor || max_fret > anchor + width - 1))
        {
            const int next_width = std::max(4, max_fret - min_fret + 1);
            // The window may sit anywhere in [max-width+1, min]; the first placement anchors at
            // the lowest fretted note, later ones shift minimally from the current anchor.
            const int lowest_anchor = std::max(1, max_fret - next_width + 1);
            const int next_anchor =
                positions.empty() ? min_fret : std::clamp(anchor, lowest_anchor, min_fret);
            positions.push_back(
                common::core::FretHandPosition{
                    .position = chart_notes[index].position,
                    .fret = next_anchor,
                    .width = next_width,
                });
            anchor = next_anchor;
            width = next_width;
        }
        index = onset_end;
    }
    return positions;
}

// Builds one track's chart: tie merging, technique mapping, bends, slide resolution, sustain
// normalization, and fret-hand position generation.
[[nodiscard]] Chart buildChart(
    const GpTrack& track, const MeasureGrid& grid, std::vector<std::string>& notes)
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
    int slide_in_count = 0;

    for (const NoteEvent& event : events)
    {
        const GpNote& source = event.source;
        const Fraction event_end = addFractions(event.global_beat, event.duration_beats);

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
                    origin.note.sustain =
                        subtractFractions(origin.end_global_beat, origin.global_beat);
                }
                origin.note.vibrato = origin.note.vibrato || source.vibrato;
                origin.note.tremolo = origin.note.tremolo || event.tremolo;
                if (source.bend.has_value())
                {
                    const Fraction base = subtractFractions(event.global_beat, origin.global_beat);
                    for (BendPoint point :
                         buildBendPoints(*source.bend, event.duration_beats, notes))
                    {
                        point.offset = addFractions(point.offset, base);
                        if (origin.note.bend.empty() ||
                            point.offset > origin.note.bend.back().offset)
                        {
                            origin.note.bend.push_back(point);
                        }
                    }
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

        ChartNote& note = entry.note;
        note.position =
            GridPosition{.measure = event.measure, .beat = event.beat, .offset = event.offset};
        note.string = source.string + 1;
        note.fret = source.fret;
        note.sustain = event.duration_beats;
        note.vibrato = source.vibrato;
        note.tremolo = event.tremolo;
        note.accent = source.accent;

        if (source.tapped)
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
    // exists. A shift slide (flag 1) glides into a re-picked target that keeps its own onset and
    // head. A legato slide (flag 2) is a continuation of the same note (user rule 2026-07-21):
    // the target is not re-picked, so it folds into the origin as a pitched waypoint at the
    // junction — the sustain extends through the target's notated end, its sustain-carried
    // techniques fold in, and its own onward slide continues the chain until a shift, a
    // trail-off, or the chain's end stops it. Slide-outs trail off unpitched.
    std::vector<bool> merged_away(built.size(), false);
    for (std::size_t index = 0; index < built.size(); ++index)
    {
        BuiltNote& entry = built[index];
        if (merged_away[index] || entry.slide_flags == 0)
        {
            continue;
        }
        ChartNote& note = entry.note;
        const Fraction minimum_window{1, 8};

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
                // No landing note exists; the glide degrades to an unpitched trail-off.
                flags |= 4;
                break;
            }
            const Fraction gap = subtractFractions(next->global_beat, entry.global_beat);
            if ((flags & 2) != 0 && (flags & 1) == 0)
            {
                // Legato: the landing continues this note. Waypoint at the junction, sustain
                // through the target's notated end, techniques folded, chain continued.
                note.slides.push_back(SlideWaypoint{.offset = gap, .fret = next->note.fret});
                if (entry.end_global_beat < next->end_global_beat)
                {
                    entry.end_global_beat = next->end_global_beat;
                }
                note.sustain = subtractFractions(entry.end_global_beat, entry.global_beat);
                note.vibrato = note.vibrato || next->note.vibrato;
                note.tremolo = note.tremolo || next->note.tremolo;
                for (BendPoint point : next->note.bend)
                {
                    point.offset = addFractions(point.offset, gap);
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

            // Shift: the glide runs across the note's own sustain into the re-picked target; a
            // zero-sustain source note gets the smallest window that still reads as a slide,
            // never the whole gap.
            Fraction window = note.sustain.numerator > 0 ? note.sustain : minimum_window;
            if (gap.numerator > 0 && gap < window)
            {
                window = gap;
            }
            if (note.sustain < window)
            {
                note.sustain = window;
            }
            note.slides.push_back(SlideWaypoint{.offset = window, .fret = next->note.fret});
            flags = 0;
            break;
        }

        if ((flags & (4 | 8)) != 0)
        {
            const bool upward = (flags & 8) != 0;
            const int target = upward ? std::min(glide_fret + 4, common::core::g_max_fret)
                                      : std::max(glide_fret - 4, 0);
            // The trail-off ends at the sustain end, strictly after any chain waypoint so the
            // payload stays ascending.
            Fraction window = note.sustain;
            if (!note.slides.empty() && window <= note.slides.back().offset)
            {
                window = addFractions(note.slides.back().offset, minimum_window);
            }
            if (window.numerator <= 0)
            {
                window = minimum_window;
            }
            if (note.sustain < window)
            {
                note.sustain = window;
            }
            note.slides.push_back(
                SlideWaypoint{.offset = window, .fret = target, .unpitched = true});
        }

        if ((entry.slide_flags & (16 | 32)) != 0)
        {
            ++slide_in_count;
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
    if (slide_in_count > 0)
    {
        notes.push_back(
            std::to_string(slide_in_count) + " slide-ins have no chart equivalent and were kept "
                                             "as plain notes");
    }

    // Runs after slide resolution so slide-extended tails carry their payloads into the trim's
    // payload floor.
    normalizeImportedSustains(built, grid, notes);

    // Shapes read the notated (pre-trim) note ends, so this runs on the built entries before
    // their notes move into the chart.
    deriveChordShapes(built, chart);
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

    chart.fret_hand_positions = generateFretHandPositions(chart.notes);
    if (!chart.fret_hand_positions.empty())
    {
        notes.push_back(
            "generated " + std::to_string(chart.fret_hand_positions.size()) +
            " fret-hand positions (simple window walk; verify)");
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
    // song level rather than being duplicated into each track's chart.
    for (std::size_t measure = 0; measure < score.master_bars.size(); ++measure)
    {
        if (!score.master_bars[measure].section.empty())
        {
            song.sections.push_back(
                common::core::SongSection{
                    .position = GridPosition{.measure = static_cast<int>(measure) + 1, .beat = 1},
                    .name = score.master_bars[measure].section,
                });
        }
    }

    int whammy_beats = 0;
    int grace_beats = 0;
    for (const GpTrack& track : score.tracks)
    {
        for (const GpBar& bar : track.bars)
        {
            for (const std::vector<GpBeat>& voice : bar.voices)
            {
                for (const GpBeat& beat : voice)
                {
                    whammy_beats += beat.whammy ? 1 : 0;
                    grace_beats += beat.grace ? 1 : 0;
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
    if (grace_beats > 0)
    {
        song.notes.push_back(std::to_string(grace_beats) + " grace-note beats were dropped");
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
        arrangement.chart = buildChart(track, grid, song.notes);

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
