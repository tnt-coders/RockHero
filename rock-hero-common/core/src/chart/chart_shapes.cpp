#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <map>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// One struck string's contribution to a span's articulation identity: the whole note with its
// position and duration neutralized, so ChartNote equality decides "same chord" and a technique
// field added to the note later can never silently drop out of the comparison. Empty where the
// string takes no part in the posture.
using StringArticulation = std::optional<ChartNote>;

// Reduces a presented note to the identity two strums are compared by. Read from the PRESENTED
// note deliberately: two strums that DRAW identically are one box, so a gesture the presentation
// rules compressed is compared in its compressed form.
[[nodiscard]] StringArticulation articulationOf(const ChartNote& presented)
{
    ChartNote key = presented;
    key.position = GridPosition{};
    key.sustain = Fraction{};
    return key;
}

// The span being held open: the posture it holds, the articulation a following onset must repeat
// to join it, and the beat marks its end is chosen from — how far its members ring (`end_beat`)
// and where its final restrike sits (`last_strum_beat`), which is the floor the closing trim can
// never cut below.
struct OpenSpan
{
    std::size_t posture{0};
    std::vector<StringArticulation> articulation;
    GridPosition position;
    Fraction start_beat{};
    Fraction end_beat{};
    Fraction last_strum_beat{};
};

} // namespace

// The onset walk. Groups are contiguous runs of one grid position, which is the same partition
// presentation and the hold engine use — the stream is sorted by (position, string), so an onset
// group is an adjacency question and never a search.
ChartShapes deriveChartShapes(
    const std::vector<ChartNote>& saved_notes, const std::vector<ChartNote>& presented_notes,
    const TempoMap& tempo_map)
{
    ChartShapes derived;

    // A posture array is indexed by string number, and the model bounds those at
    // g_max_chart_strings — so that constant IS the width, never a quantity read off the input.
    // The derivation runs inside `normalizeChart`, BEFORE `validateChartRules` has refused an
    // out-of-range string, so sizing from the stream (or from the tuning, equally unvalidated
    // there) would let a corrupt document's `"string"` decide an allocation. Every posture from
    // one stream is the same length either way, which is all the dedup below needs to compare
    // held frets alone; a string the tuning does not have simply never fills its slot.
    constexpr auto string_count = static_cast<std::size_t>(g_max_chart_strings);

    // Every onset's exact global beat, once. The whole rule is beat arithmetic — how far members
    // ring, the closing margin, the span's own length — and it stays rational end to end because a
    // span's sustain is a stored-shaped fraction, not a rounded one.
    std::vector<Fraction> onset_beat;
    onset_beat.reserve(saved_notes.size());
    for (const ChartNote& note : saved_notes)
    {
        onset_beat.push_back(beatDistance(tempo_map, GridPosition{}, note.position));
    }
    const auto ring_end_of = [&onset_beat, &saved_notes](const std::size_t index) {
        return onset_beat[index] + saved_notes[index].sustain;
    };

    std::map<std::vector<std::optional<int>>, std::size_t> posture_indices;
    std::optional<OpenSpan> open;

    // The closing onset reduced by the minimum-sustain-distance margin (at the closing onset's
    // measure) — where a span it closes must end.
    const auto margin_limit = [&onset_beat, &saved_notes, &tempo_map](const std::size_t closing) {
        return onset_beat[closing] -
               minimumSustainDistanceBeats(
                   tempo_map.timeSignatureAt(saved_notes[closing].position.measure).denominator);
    };

    // Closes the held span. A span closed by a following event trims to the margin before it
    // (rule 12a — spans keep the same minimum sustain distance as every other element), floored at
    // the last strum so the box always reaches its final restrike. A span that would lose all
    // length (a single strum crowded closer than the margin) falls back to exact adjacency,
    // mirroring the sustain rules' protected-adjacency precedent.
    const auto close_span = [&derived, &open](
                                const std::optional<Fraction> closing_limit,
                                const std::optional<Fraction>
                                    closing_beat) {
        if (!open.has_value())
        {
            return;
        }
        Fraction end = open->end_beat;
        if (closing_limit.has_value() && *closing_limit < end)
        {
            end = std::max(*closing_limit, open->last_strum_beat);
        }
        if (!(open->start_beat < end) && closing_beat.has_value())
        {
            // Exact adjacency: the crowded span ends at the earlier of its own ring and the
            // closing onset — both sit strictly after the span start, so the span keeps positive
            // length even when the closer lands exactly on the ring's end (a dense run of short
            // strums).
            end = std::min(open->end_beat, *closing_beat);
        }
        derived.shapes.push_back(
            ChartShape{
                .position = open->position,
                .sustain = end - open->start_beat,
                .posture = open->posture,
            });
        open.reset();
    };

    // The last note sounded per string, for the ring-through rule: a note whose tail crosses a
    // chord's onset on an un-struck string is still sounding, so its held fret joins the derived
    // posture — and the arrival rule then renders the partly-struck span as an arpeggio. Indexes
    // rather than pointers, because the posture it folds in comes from the presented stream while
    // the ring it tests comes from the stored one.
    std::vector<std::optional<std::size_t>> ringing(string_count);

    std::size_t index = 0;
    while (index < saved_notes.size())
    {
        std::size_t onset_end = index;
        Fraction ring_end{};
        std::vector<StringArticulation> articulation(string_count);
        std::size_t struck = 0;
        while (onset_end < saved_notes.size() &&
               saved_notes[onset_end].position == saved_notes[index].position)
        {
            // Right-hand onsets are invisible to span derivation: they join no posture and extend
            // no ring, so a mixed onset is judged by its fretting-hand members alone.
            if (!rightHandOnset(saved_notes[onset_end].attack))
            {
                if (const auto string_index =
                        static_cast<std::size_t>(saved_notes[onset_end].string - 1);
                    string_index < string_count)
                {
                    articulation[string_index] = articulationOf(presented_notes[onset_end]);
                    ++struck;
                }
                ring_end = std::max(ring_end, ring_end_of(onset_end));
            }
            ++onset_end;
        }

        if (struck == 0)
        {
            // Tap-only onsets are transparent to the GROUPING: they neither form a chord posture
            // nor end a held one. A chord ringing under taps on other strings keeps its span,
            // which the arrival rule then renders as a held arpeggio — the corpus's held-shape-
            // under-tapping case. Transparent to the grouping is not transparent to the ring: a
            // tap is a real onset on its own string, so the same-string clamp has already ended
            // any ring there. A short-ringing chord is unaffected either way: its span still ends
            // at its own ring, before the taps.
        }
        else if (struck >= 2)
        {
            // Ring-through strings join the posture (they never count as struck): the held note's
            // articulation folds in so span merging still compares whole notes.
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                const std::optional<std::size_t>& ring = ringing[string_index];
                if (!articulation[string_index].has_value() && ring.has_value() &&
                    onset_beat[index] < ring_end_of(*ring))
                {
                    articulation[string_index] = articulationOf(presented_notes[*ring]);
                }
            }
            std::vector<std::optional<int>> frets(string_count);
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                // Bound to a local so the optional check and the access are provably the same
                // object (bugprone-unchecked-optional-access cannot track repeated indexing).
                const StringArticulation& slot = articulation[string_index];
                if (slot.has_value())
                {
                    frets[string_index] = slot->fret;
                }
            }
            const auto [entry, inserted] =
                posture_indices.try_emplace(frets, derived.postures.size());
            if (inserted)
            {
                derived.postures.push_back(ChartPosture{.frets = std::move(frets)});
            }
            if (open.has_value() && open->articulation == articulation)
            {
                open->end_beat = std::max(open->end_beat, ring_end);
                open->last_strum_beat = onset_beat[index];
            }
            else
            {
                close_span(margin_limit(index), onset_beat[index]);
                open = OpenSpan{
                    .posture = entry->second,
                    .articulation = std::move(articulation),
                    .position = saved_notes[index].position,
                    .start_beat = onset_beat[index],
                    .end_beat = ring_end,
                    .last_strum_beat = onset_beat[index],
                };
            }
        }
        else
        {
            // Any intervening non-chord onset ends the held posture.
            close_span(margin_limit(index), onset_beat[index]);
        }

        // This onset's non-tap notes become the ring candidates for later onsets (updated after
        // use: a note starting at an onset is struck there, not ringing through it). Taps stay
        // invisible here too — a ringing tap never folds into a later posture.
        for (std::size_t member = index; member < onset_end; ++member)
        {
            if (const auto string_index = static_cast<std::size_t>(saved_notes[member].string - 1);
                string_index < string_count && !rightHandOnset(saved_notes[member].attack))
            {
                ringing[string_index] = member;
            }
        }
        index = onset_end;
    }
    close_span(std::nullopt, std::nullopt);

    return derived;
}

std::vector<bool> chartShapeArrivals(
    const std::vector<ChartNote>& presented_notes, const std::vector<ChartShape>& shapes,
    const std::vector<ChartPosture>& postures, const TempoMap& tempo_map)
{
    std::vector<bool> arpeggio;
    arpeggio.reserve(shapes.size());
    // Both streams ascend, so one note cursor serves every shape. It carries the one thing the rule
    // needs from the past — the most recent note on each string — which is what turns the whole
    // classification into a single forward pass. Answering it per shape instead meant walking BACK
    // through the note stream from each span, all the way to the first note whenever a posture
    // string had none, and both projections do this for every shape on every chart revision.
    constexpr std::size_t no_note = std::numeric_limits<std::size_t>::max();
    std::array<std::size_t, static_cast<std::size_t>(g_max_chart_strings) + 1> last_per_string{};
    last_per_string.fill(no_note);
    std::size_t next_note = 0;
    for (const ChartShape& shape : shapes)
    {
        while (next_note < presented_notes.size() &&
               presented_notes[next_note].position < shape.position)
        {
            const int string = presented_notes[next_note].string;
            if (string >= 1 && string <= g_max_chart_strings)
            {
                last_per_string.at(static_cast<std::size_t>(string)) = next_note;
            }
            ++next_note;
        }
        // The cursor now sits on the first note AT the span start, and the notes sharing that onset
        // are the contiguous run from there.
        std::size_t after_start = next_note;
        while (after_start < presented_notes.size() &&
               presented_notes[after_start].position == shape.position)
        {
            ++after_start;
        }
        if (after_start - next_note < 2)
        {
            // A single onset at the span start is a sequential arrival, whatever else is ringing.
            arpeggio.push_back(true);
            continue;
        }

        // A held chord played under a right-hand onset reads as a held arpeggio, not a strummed
        // box: the fretting hand holds the shape while the other hand sounds above it — taps and
        // pick slides alike. Any such note sounding within the span flips the box.
        const GridPosition span_end = advanceGridPosition(tempo_map, shape.position, shape.sustain);
        bool held_under_right_hand = false;
        for (std::size_t scan = next_note;
             scan < presented_notes.size() && presented_notes[scan].position < span_end;
             ++scan)
        {
            held_under_right_hand =
                held_under_right_hand || rightHandOnset(presented_notes[scan].attack);
        }
        if (held_under_right_hand || shape.posture >= postures.size())
        {
            arpeggio.push_back(held_under_right_hand);
            continue;
        }

        // A posture string still ringing at the start without an onset there was not re-struck —
        // the strum picks around the held note, so the span cannot be one full strum. Only that
        // string's most recent earlier note can still be ringing, which the cursor already knows.
        // "Ringing" is the PRESENTED tail: a dead string makes no sound to pick around, and
        // presentation is where a dead note's tail goes (E25).
        const ChartPosture& posture = postures[shape.posture];
        bool rings_unstruck = false;
        for (std::size_t index = 0; index < posture.frets.size(); ++index)
        {
            // Bound to a local so the optional check and the access are provably the same object.
            const std::optional<int>& fret = posture.frets[index];
            const int string = static_cast<int>(index) + 1;
            if (!fret.has_value() || string > g_max_chart_strings)
            {
                continue;
            }
            bool struck = false;
            for (std::size_t scan = next_note; scan < after_start; ++scan)
            {
                struck = struck || presented_notes[scan].string == string;
            }
            const std::size_t earlier = last_per_string.at(static_cast<std::size_t>(string));
            rings_unstruck =
                rings_unstruck ||
                (!struck && earlier != no_note &&
                 shape.position < sustainEndPosition(tempo_map, presented_notes[earlier]));
        }
        arpeggio.push_back(rings_unstruck);
    }
    return arpeggio;
}

} // namespace rock_hero::common::core
