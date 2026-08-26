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

// One hold marker resolved against the span it fell inside. The marker itself names no span, so
// this is the whole of the relationship: which string it claims, the beat it claims it at (which
// the span's own end then judges), and the fret it has resolved to — its own where it carries one,
// otherwise the one a later in-span note on that string supplies, and empty while nothing has.
struct SilentClaim
{
    std::size_t string_index{0};
    Fraction beat{};
    std::optional<int> fret{};
};

// The span being held open: the articulation a following onset must repeat to join it, the silent
// claims authored inside it so far, and the beat marks its end is chosen from — how far its members
// ring (`end_beat`) and where its final restrike sits (`last_strum_beat`), which is the floor the
// closing trim can never cut below. The posture is NOT here: a claim's fret can arrive from a note
// later in the span, so the fret vector is only complete once the span is (see `close_span`).
struct OpenSpan
{
    std::vector<StringArticulation> articulation;
    std::vector<SilentClaim> claims;
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
    const std::vector<ChartHoldMarker>& hold_markers, const TempoMap& tempo_map)
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

    // Closes the held span, and keys its posture. A span closed by a following event trims to the
    // margin before it (rule 12a — spans keep the same minimum sustain distance as every other
    // element), floored at the last strum so the box always reaches its final restrike. A span that
    // would lose all length (a single strum crowded closer than the margin) falls back to exact
    // adjacency, mirroring the sustain rules' protected-adjacency precedent.
    //
    // The posture is built HERE rather than at each onset because a fret-absent hold marker takes
    // its fret from a note later in the same span: the vector is only complete once the span is.
    // Keying it once per span rather than once per strum is what that buys back.
    const auto close_span = [&derived, &posture_indices, &open](
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
        std::vector<std::optional<int>> frets(open->articulation.size());
        for (std::size_t string_index = 0; string_index < open->articulation.size(); ++string_index)
        {
            // Bound to a local so the optional check and the access are provably the same
            // object (bugprone-unchecked-optional-access cannot track repeated indexing).
            const StringArticulation& slot = open->articulation[string_index];
            if (slot.has_value())
            {
                frets[string_index] = slot->fret;
            }
        }
        // What the notes could not say. A claim that resolved to no fret is inert, and so is one
        // authored past the span's own end — that gap is after the shape stopped sounding, and the
        // bracket the fret would print under never reaches it. A string the sound already states is
        // left alone: the marker adds nothing there, so it makes no silent member either.
        bool silent_member = false;
        for (const SilentClaim& claim : open->claims)
        {
            std::optional<int>& fret = frets[claim.string_index];
            if (!claim.fret.has_value() || !(claim.beat < end) || fret.has_value())
            {
                continue;
            }
            fret = claim.fret;
            silent_member = true;
        }
        const auto [entry, inserted] = posture_indices.try_emplace(frets, derived.postures.size());
        if (inserted)
        {
            derived.postures.push_back(ChartPosture{.frets = std::move(frets)});
        }
        derived.shapes.push_back(
            ChartShape{
                .position = open->position,
                .sustain = end - open->start_beat,
                .posture = entry->second,
                .silent_member = silent_member,
            });
        open.reset();
    };

    // Attaches one authored marker to whatever span is open at its position. A marker outside every
    // span is not refused anywhere — it simply attaches to nothing and draws nothing, the same
    // degrade an unjustified connection claim takes. Strings are bounded here for the same reason
    // the note walk bounds them: this runs before validation has refused an impossible one.
    std::size_t marker_index = 0;
    const auto attach_markers_before = [&open, &hold_markers, &marker_index, &tempo_map](
                                           const GridPosition& limit, const bool inclusive) {
        while (marker_index < hold_markers.size() &&
               (inclusive ? !(limit < hold_markers[marker_index].position)
                          : hold_markers[marker_index].position < limit))
        {
            const ChartHoldMarker& marker = hold_markers[marker_index];
            ++marker_index;
            if (!open.has_value() || marker.string < 1 || marker.string > g_max_chart_strings)
            {
                continue;
            }
            open->claims.push_back(
                SilentClaim{
                    .string_index = static_cast<std::size_t>(marker.string - 1),
                    .beat = beatDistance(tempo_map, GridPosition{}, marker.position),
                    .fret = marker.fret,
                });
        }
    };

    // The last note sounded per string, for the ring-through rule: a note whose tail crosses a
    // chord's onset on an un-struck string is still sounding, so its held fret joins the derived
    // posture — and the arrival rule then renders the partly-struck span as an arpeggio. Indexes
    // rather than pointers, because the posture it folds in comes from the presented stream while
    // the ring it tests comes from the stored one.
    std::vector<std::optional<std::size_t>> ringing(string_count);

    // SIDE RULING (ii): a lone re-pick of a string the open span already holds does not leave the
    // shape. Any single-string onset used to close the span, which killed a held chord at the exact
    // moment a broken figure re-picked one of its own members — and every fact needed to know
    // better was already in the stream. Nothing here is authored: it is the derivation reading what
    // it had. Three conditions, and the third is what keeps it honest.
    const auto lone_repick_continues = [&ringing, &onset_beat, &presented_notes](
                                           const OpenSpan& span,
                                           const std::vector<StringArticulation>& articulation,
                                           const Fraction now) {
        // A `struck == 1` onset fills exactly one slot, which this finds without the walk having to
        // carry it out of the group loop.
        std::size_t struck_index = articulation.size();
        for (std::size_t string_index = 0; string_index < articulation.size(); ++string_index)
        {
            if (articulation[string_index].has_value())
            {
                struck_index = string_index;
            }
        }
        if (struck_index == articulation.size())
        {
            return false;
        }
        // 1. The string is a member of the open shape. A member the SOUND states must be re-picked
        //    identically — rule 11 splits a chord on any articulation change, and a lone re-pick is
        //    that same question asked of one string. A member the chart HOLDS silently has no
        //    articulation to match, because nothing ever sounded it, so the authored claim is the
        //    whole test — and this is where a marker and this ruling compose into the reported
        //    case.
        //
        //    The claim IS the whole test, which means all of it: a claim carrying a fret states
        //    where the finger is, so a re-pick at a different stop is a different hand and splits,
        //    exactly as the sound branch splits on a changed articulation. Passing it would let an
        //    authored stop outlive the note that contradicts it — the bracket printing the claim's
        //    fret at the span start while the note inside it sounds another. A fret-ABSENT claim
        //    states only WHEN the hand arrived, and the re-pick is what supplies the what, so there
        //    the string is the whole test.
        const StringArticulation& held = span.articulation[struck_index];
        const StringArticulation& repick = articulation[struck_index];
        bool member = false;
        if (held.has_value() && repick.has_value())
        {
            member = *held == *repick;
        }
        else if (!held.has_value() && repick.has_value())
        {
            const int repick_fret = repick->fret;
            for (const SilentClaim& claim : span.claims)
            {
                // Bound to a local so the optional check and the access are provably the same
                // object (bugprone-unchecked-optional-access cannot track a member per iteration).
                const std::optional<int>& claim_fret = claim.fret;
                member = member || (claim.string_index == struck_index &&
                                    (!claim_fret.has_value() || *claim_fret == repick_fret));
            }
        }
        if (!member)
        {
            return false;
        }
        // 2. Some OTHER member is still sounding, so the hand demonstrably has not left the shape.
        //    Without this a lone re-pick would resurrect a span across any amount of silence.
        // 3. That witness is the PRESENTED ring, for the arrival rule's own reason: a tail no
        //    surface draws is not a string the shape is heard to be holding.
        bool another_rings = false;
        for (std::size_t string_index = 0; string_index < span.articulation.size(); ++string_index)
        {
            const std::optional<std::size_t>& ring = ringing[string_index];
            if (string_index == struck_index || !span.articulation[string_index].has_value() ||
                !ring.has_value())
            {
                continue;
            }
            another_rings =
                another_rings || now < onset_beat[*ring] + presented_notes[*ring].sustain;
        }
        return another_rings;
    };

    std::size_t index = 0;
    while (index < saved_notes.size())
    {
        // Markers in the gap before this onset belong to the span that was open across it, so they
        // attach before the branch below decides that span's fate. Markers AT this onset attach
        // after it, to whatever span the onset leaves open.
        attach_markers_before(saved_notes[index].position, /*inclusive=*/false);

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
            if (open.has_value() && open->articulation == articulation)
            {
                open->end_beat = std::max(open->end_beat, ring_end);
                open->last_strum_beat = onset_beat[index];
            }
            else
            {
                close_span(margin_limit(index), onset_beat[index]);
                open = OpenSpan{
                    .articulation = std::move(articulation),
                    .claims = {},
                    .position = saved_notes[index].position,
                    .start_beat = onset_beat[index],
                    .end_beat = ring_end,
                    .last_strum_beat = onset_beat[index],
                };
            }
        }
        else if (open.has_value() && lone_repick_continues(*open, articulation, onset_beat[index]))
        {
            // Side ruling (ii): the shape survives one of its own members being re-picked, and the
            // span grows to cover the re-pick — the last-strum floor included, so the closing trim
            // can never cut back past it.
            open->end_beat = std::max(open->end_beat, ring_end);
            open->last_strum_beat = onset_beat[index];
        }
        else
        {
            // Any other intervening non-chord onset ends the held posture.
            close_span(margin_limit(index), onset_beat[index]);
        }

        // A fret-absent claim takes its fret from the first note that sounds on its string LATER in
        // the same span. Asked here, after the branch, so it reads the span this onset actually
        // left open: an onset that closed a span is outside it and states nothing about it, and a
        // span that just opened carries no claims yet (this onset's markers attach below).
        if (open.has_value())
        {
            for (std::size_t member = index; member < onset_end; ++member)
            {
                const ChartNote& note = saved_notes[member];
                if (rightHandOnset(note.attack) || note.string < 1 ||
                    note.string > g_max_chart_strings)
                {
                    continue;
                }
                const auto string_index = static_cast<std::size_t>(note.string - 1);
                for (SilentClaim& claim : open->claims)
                {
                    if (!claim.fret.has_value() && claim.string_index == string_index &&
                        claim.beat < onset_beat[index])
                    {
                        claim.fret = presented_notes[member].fret;
                    }
                }
            }
        }
        attach_markers_before(saved_notes[index].position, /*inclusive=*/true);

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
    // Markers past the last onset can still fall inside the span that is ringing out under them;
    // the close's own end check is what decides, exactly as for every other marker.
    while (marker_index < hold_markers.size())
    {
        attach_markers_before(hold_markers[marker_index].position, /*inclusive=*/true);
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
        if (shape.silent_member)
        {
            // The hand holds a member it never sounds here, which only the bracket can state: a
            // chord box prints the notes' own heads and has nowhere to put a fret nothing struck.
            // Carried by the derivation rather than re-derived, because nothing in the note stream
            // can tell a silently-held string from an absent one — that is the whole reason a
            // ChartHoldMarker is authored at all.
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
