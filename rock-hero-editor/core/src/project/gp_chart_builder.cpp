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
using common::core::ChartSection;
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
// import rather than guessing. Tracked in docs/todo/gp-track-part-mapping.md.
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

// Builds one track's chart: tie merging, technique mapping, bends, and slide resolution.
[[nodiscard]] Chart buildChart(
    const GpTrack& track, const MeasureGrid& grid, const std::vector<GpMasterBar>& master_bars,
    std::vector<std::string>& notes)
{
    Chart chart;
    for (const int midi : track.tuning_midi)
    {
        chart.tuning.strings.push_back(midiNoteName(midi));
    }
    chart.tuning.capo = track.capo;

    for (std::size_t measure = 0; measure < master_bars.size(); ++measure)
    {
        if (!master_bars[measure].section.empty())
        {
            chart.sections.push_back(
                ChartSection{
                    .position = GridPosition{.measure = static_cast<int>(measure) + 1, .beat = 1},
                    .type = master_bars[measure].section,
                });
        }
    }

    const std::vector<NoteEvent> events = collectEvents(track, grid, notes);

    // Built notes plus their onset on the global beat axis (needed for tie and slide spans).
    struct BuiltNote
    {
        ChartNote note;
        Fraction global_beat{};
        Fraction end_global_beat{};
        int gp_string{0};
        int slide_flags{0};
    };
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
    // exists. Shift and legato slides glide into that next note; slide-outs trail off unpitched.
    for (std::size_t index = 0; index < built.size(); ++index)
    {
        BuiltNote& entry = built[index];
        if (entry.slide_flags == 0)
        {
            continue;
        }
        ChartNote& note = entry.note;
        const Fraction minimum_window{1, 8};

        if ((entry.slide_flags & (1 | 2)) != 0)
        {
            const BuiltNote* next = nullptr;
            for (std::size_t follower = index + 1; follower < built.size(); ++follower)
            {
                if (built[follower].gp_string == entry.gp_string)
                {
                    next = &built[follower];
                    break;
                }
            }
            if (next != nullptr)
            {
                const Fraction gap = subtractFractions(next->global_beat, entry.global_beat);
                // The glide runs across the note's own sustain; a zero-sustain source note gets
                // the smallest window that still reads as a slide, never the whole gap.
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
            }
            else
            {
                entry.slide_flags |= 4;
            }
        }

        if ((entry.slide_flags & (4 | 8)) != 0 && note.slides.empty())
        {
            const bool upward = (entry.slide_flags & 8) != 0;
            const int target = upward ? std::min(note.fret + 4, common::core::g_max_fret)
                                      : std::max(note.fret - 4, 0);
            Fraction window = note.sustain;
            if (window.numerator <= 0)
            {
                window = minimum_window;
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
        arrangement.chart = buildChart(track, grid, score.master_bars, song.notes);

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
    // can spot and correct a misfiled track. Tracked in docs/todo/gp-track-part-mapping.md.
    song.notes.push_back("assigned parts by track order and name (verify): " + part_guesses);

    return song;
}

} // namespace rock_hero::editor::core
