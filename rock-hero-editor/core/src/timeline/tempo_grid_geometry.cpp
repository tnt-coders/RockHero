#include "timeline/tempo_grid_geometry.h"

#include "timeline/timeline_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <rock_hero/common/core/chart/grid_arithmetic.h>

namespace rock_hero::editor::core
{

namespace
{

// Inverse of timelineXForPosition's interior mapping: turns a drawing-width column back into
// seconds. Used only to bound the grid scan conservatively, so it may be loose; the exact
// per-line column test in visibleTempoGridLines stays the real visibility gate. Callers
// guarantee width_span > 0 so the division is safe.
double secondsAtColumn(common::core::TimeRange visible_timeline, double width_span, int column)
{
    return visible_timeline.start.seconds +
           (static_cast<double>(column) / width_span) * visible_timeline.duration().seconds;
}

// Keeps rendering and snapping on the same grid even when a caller passes a corrupt note value:
// both public entry points degrade to the quarter-note grid rather than diverging or going blank.
[[nodiscard]] common::core::Fraction normalizedGridNoteValue(
    common::core::Fraction note_value) noexcept
{
    return isValidTempoGridNoteValue(note_value) ? note_value : common::core::Fraction{1, 4};
}

// Forward walker over the measure-anchored note-value grid. Within a measure with signature
// num/den_sig, line j sits j * note_numerator * den_sig / note_denominator beats after the
// downbeat (a note value of n/d whole notes is 4n/d quarter notes, and a beat is 4/den_sig
// quarter notes, so the step is n*den_sig/d beats — exact integers over the note denominator).
// The walk restarts at every downbeat, which keeps each measure's start on a grid line even in
// meters whose length is not a multiple of the step, and it never advances past the terminal
// anchor beat. Signature and measure base-index bookkeeping is incremental so the per-line hot
// path stays integer arithmetic.
class MeasureGridWalker
{
public:
    // Positions the walker on measure 1's downbeat; the note value must already be normalized.
    MeasureGridWalker(const common::core::TempoMap& tempo_map, common::core::Fraction note_value)
        : m_tempo_map(tempo_map)
        , m_note(note_value)
        , m_terminal_units(tempo_map.terminalGlobalBeatIndex() * note_value.denominator)
    {
        moveToMeasure(1, 0);
    }

    // Non-copyable: the walker is a scoped scan cursor over one tempo map reference.
    ~MeasureGridWalker() = default;
    MeasureGridWalker(const MeasureGridWalker&) = delete;
    MeasureGridWalker& operator=(const MeasureGridWalker&) = delete;
    MeasureGridWalker(MeasureGridWalker&&) = delete;
    MeasureGridWalker& operator=(MeasureGridWalker&&) = delete;

    // Reports whether the walker still addresses a line at or before the terminal anchor beat.
    [[nodiscard]] bool valid() const noexcept
    {
        return lineUnits() <= m_terminal_units;
    }

    // Exact fractional global-beat position of the current line. Dividing the raw remainder is
    // bit-identical to reducing it through Fraction first, because IEEE division is correctly
    // rounded for the same rational value, and it keeps a gcd off the per-line hot path.
    [[nodiscard]] double beatPosition() const noexcept
    {
        const std::int64_t whole = m_measure_beat_index + m_offset_units / m_note.denominator;
        const std::int64_t remainder = m_offset_units % m_note.denominator;
        return static_cast<double>(whole) +
               static_cast<double>(remainder) / static_cast<double>(m_note.denominator);
    }

    // One-based measure number of the current line.
    [[nodiscard]] int measure() const noexcept
    {
        return m_measure;
    }

    // Exact musical address of the current line. The within-measure offset stays a rational in
    // the note denominator, so odd grids (a 1/13 note value) round-trip into stored positions
    // without any fixed fine-grid approximation.
    [[nodiscard]] common::core::GridPosition gridPosition() const noexcept
    {
        return common::core::GridPosition{
            .measure = m_measure,
            .beat = 1 + static_cast<int>(m_offset_units / m_note.denominator),
            .offset = common::core::Fraction{
                static_cast<int>(m_offset_units % m_note.denominator), m_note.denominator
            },
        };
    }

    // Musical rank of the current line: downbeats outrank whole-beat lines outrank the rest.
    [[nodiscard]] TempoGridLineRank rank() const noexcept
    {
        if (m_offset_units == 0)
        {
            return TempoGridLineRank::Measure;
        }

        return m_offset_units % m_note.denominator == 0 ? TempoGridLineRank::Beat
                                                        : TempoGridLineRank::Subdivision;
    }

    // Moves to the next line, rolling into the next measure when the step leaves this one.
    void advance()
    {
        m_offset_units += m_step_units;
        if (m_offset_units >= m_measure_units)
        {
            moveToMeasure(m_measure + 1, m_measure_beat_index + m_signature.numerator);
        }
    }

    // Moves to the previous line, rolling onto the previous measure's last line at downbeats.
    // Returns false without moving when the walker already sits on the very first line.
    [[nodiscard]] bool retreat()
    {
        if (m_offset_units > 0)
        {
            m_offset_units -= m_step_units;
            return true;
        }

        if (m_measure <= 1)
        {
            return false;
        }

        const common::core::TimeSignatureChange previous_signature =
            m_tempo_map.timeSignatureAt(m_measure - 1);
        moveToMeasure(
            m_measure - 1, m_measure_beat_index - std::max(1, previous_signature.numerator));
        m_offset_units = lastLineOffsetUnits();
        return true;
    }

    // Positions the walker on the first line whose seconds are at or after the target. Returns
    // false when every grid line lies before the target; the walker is then past the end and
    // moveToLastLine() recovers the final candidate.
    [[nodiscard]] bool seekFirstLineAtOrAfter(double seconds)
    {
        // Largest measure whose downbeat is not after the target; earlier measures cannot hold
        // the first at-or-after line because every line is at or after its measure's downbeat.
        const int terminal_measure =
            m_tempo_map.beatAtGlobalIndex(m_tempo_map.terminalGlobalBeatIndex()).first;
        int chosen_measure = 1;
        int search_lo = 1;
        int search_hi = terminal_measure;
        while (search_lo <= search_hi)
        {
            const int mid = search_lo + (search_hi - search_lo) / 2;
            if (m_tempo_map.secondsAtBeat(mid, 1) <= seconds)
            {
                chosen_measure = mid;
                search_lo = mid + 1;
            }
            else
            {
                search_hi = mid - 1;
            }
        }

        moveToMeasure(chosen_measure, m_tempo_map.globalBeatIndex(chosen_measure, 1));

        // Smallest line step in this measure whose time reaches the target; every line past this
        // measure's span resolves through advance() below.
        const std::int64_t last_step = lastLineOffsetUnits() / m_step_units;
        std::int64_t chosen_step = last_step + 1;
        std::int64_t step_lo = 0;
        std::int64_t step_hi = last_step;
        while (step_lo <= step_hi)
        {
            const std::int64_t mid = step_lo + (step_hi - step_lo) / 2;
            m_offset_units = mid * m_step_units;
            if (m_tempo_map.secondsAtGlobalBeatPosition(beatPosition()) >= seconds)
            {
                chosen_step = mid;
                step_hi = mid - 1;
            }
            else
            {
                step_lo = mid + 1;
            }
        }

        if (chosen_step > last_step)
        {
            // Every line of this measure is before the target, so the candidate is the next
            // measure's downbeat (when one exists inside the authored range).
            m_offset_units = last_step * m_step_units;
            advance();
        }
        else
        {
            m_offset_units = chosen_step * m_step_units;
        }

        return valid();
    }

    // Positions the walker on the final grid line: the last step that fits at or before the
    // terminal anchor beat inside the terminal measure.
    void moveToLastLine()
    {
        const int terminal_measure =
            m_tempo_map.beatAtGlobalIndex(m_tempo_map.terminalGlobalBeatIndex()).first;
        moveToMeasure(terminal_measure, m_tempo_map.globalBeatIndex(terminal_measure, 1));
        m_offset_units = lastLineOffsetUnits();
    }

private:
    // Rebinds the per-measure geometry for a measure whose downbeat global beat index is known.
    // The signature lookup runs once per measure, not per line, so the scan stays cheap.
    void moveToMeasure(int measure, std::int64_t measure_beat_index)
    {
        m_measure = measure;
        m_measure_beat_index = measure_beat_index;
        m_offset_units = 0;
        m_signature = m_tempo_map.timeSignatureAt(measure);
        // A malformed map's non-positive signature terms clamp to one so the walk always
        // progresses; misplacing that map's lines is acceptable, a zero step hanging the scan is
        // not.
        m_signature.numerator = std::max(1, m_signature.numerator);
        m_signature.denominator = std::max(1, m_signature.denominator);
        m_step_units = static_cast<std::int64_t>(m_note.numerator) * m_signature.denominator;
        m_measure_units = static_cast<std::int64_t>(m_signature.numerator) * m_note.denominator;
    }

    // Current line's exact position in beat units (beats times the note denominator).
    [[nodiscard]] std::int64_t lineUnits() const noexcept
    {
        return m_measure_beat_index * m_note.denominator + m_offset_units;
    }

    // Last line offset that stays inside this measure and inside the authored beat range.
    [[nodiscard]] std::int64_t lastLineOffsetUnits() const noexcept
    {
        std::int64_t last_offset = ((m_measure_units - 1) / m_step_units) * m_step_units;
        const std::int64_t terminal_offset =
            m_terminal_units - m_measure_beat_index * m_note.denominator;
        if (terminal_offset < last_offset)
        {
            last_offset = (terminal_offset / m_step_units) * m_step_units;
        }

        return last_offset;
    }

    // Tempo map queried for signatures, downbeat indices, and seek-time seconds lookups.
    const common::core::TempoMap& m_tempo_map;

    // Normalized grid note value; its denominator is the exact unit of all offset arithmetic.
    common::core::Fraction m_note;

    // Terminal anchor beat index scaled into note-denominator units; no line may pass it.
    std::int64_t m_terminal_units;

    // Signature active for the current measure.
    common::core::TimeSignatureChange m_signature{};

    // One-based measure the walker currently addresses.
    int m_measure{1};

    // Global beat index of the current measure's downbeat.
    std::int64_t m_measure_beat_index{0};

    // Current line's offset from the downbeat, in note-denominator units of a beat.
    std::int64_t m_offset_units{0};

    // Grid step in note-denominator units of a beat: note numerator times signature denominator.
    std::int64_t m_step_units{1};

    // Measure length in note-denominator units of a beat.
    std::int64_t m_measure_units{1};
};

} // namespace

// Pure presentation math kept in editor-core so the timeline grid stays unit-testable without JUCE.
std::vector<TempoGridLine> visibleTempoGridLines(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value,
    common::core::TimeRange visible_timeline, int width, int visible_x_begin, int visible_x_end)
{
    std::vector<TempoGridLine> lines;
    if (width <= 0 || visible_timeline.duration().seconds <= 0.0 ||
        visible_x_begin >= visible_x_end)
    {
        return lines;
    }

    const common::core::Fraction note_value = normalizedGridNoteValue(grid_note_value);

    // timelineXForPosition spreads the visible range across [0, width - 1], so a zero span means a
    // single-pixel canvas where the inverse is undefined. There, skip the bounding search and scan
    // every line; the exact column test below still keeps the output correct.
    const auto width_span = static_cast<double>(width - 1);
    const bool can_bound_scan = width_span > 0.0;

    // One-pixel margins absorb the rounding in the forward map so the bounds never exclude a line
    // whose rounded column still lands inside the visible span.
    const double window_start_seconds =
        can_bound_scan ? secondsAtColumn(visible_timeline, width_span, visible_x_begin - 1)
                       : std::numeric_limits<double>::lowest();
    const double window_end_seconds =
        can_bound_scan ? secondsAtColumn(visible_timeline, width_span, visible_x_end)
                       : std::numeric_limits<double>::max();

    MeasureGridWalker walker{tempo_map, note_value};
    if (!walker.seekFirstLineAtOrAfter(window_start_seconds))
    {
        return lines;
    }

    // Line positions increase along the measure walk, so a forward cursor resolves each line's
    // time in amortized constant time; it returns the same values as the seek's binary search.
    common::core::TempoMap::ForwardBeatTimeCursor beat_time_cursor{tempo_map};
    for (; walker.valid(); walker.advance())
    {
        const double seconds = beat_time_cursor.secondsAt(walker.beatPosition());
        if (seconds > window_end_seconds)
        {
            break;
        }

        const auto x = timelineXForPosition(
            common::core::TimePosition{seconds},
            visible_timeline,
            width,
            TimelinePositionClamping::RejectOutsideVisibleRange);
        if (!x.has_value())
        {
            continue;
        }

        const int column = static_cast<int>(std::round(*x));
        if (column < visible_x_begin || column >= visible_x_end)
        {
            continue;
        }

        const int measure = walker.measure();
        const TempoGridLineRank rank = walker.rank();
        if (!lines.empty() && lines.back().x == column)
        {
            // Several lines round onto one column when zoomed far out; keep the strongest rank so
            // measure and beat boundaries still read at low zoom, and keep that rank's measure so
            // ruler labels match the promoted color.
            if (rank > lines.back().rank)
            {
                lines.back().measure = measure;
                lines.back().rank = rank;
            }
            continue;
        }

        lines.push_back(TempoGridLine{.x = column, .measure = measure, .rank = rank});
    }

    return lines;
}

namespace
{

// Positions the walker on the grid line nearest the target by comparing the two bracketing
// lines. Distances are measured in seconds, not pixels, so the snap is exact and independent of
// zoom; the earlier line wins exact halfway targets so repeated clicks snap stably instead of
// jumping forward.
void seekNearestGridLine(
    MeasureGridWalker& walker, const common::core::TempoMap& tempo_map,
    common::core::TimePosition target)
{
    if (!walker.seekFirstLineAtOrAfter(target.seconds))
    {
        // Every grid line lies before the target, so the last line is the only candidate.
        walker.moveToLastLine();
        return;
    }

    const double after_seconds = tempo_map.secondsAtGlobalBeatPosition(walker.beatPosition());
    if (!walker.retreat())
    {
        return;
    }

    const double before_seconds = tempo_map.secondsAtGlobalBeatPosition(walker.beatPosition());
    if (std::abs(before_seconds - target.seconds) > std::abs(after_seconds - target.seconds))
    {
        walker.advance();
    }
}

} // namespace

common::core::TimePosition nearestTempoGridTime(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value,
    common::core::TimePosition target)
{
    MeasureGridWalker walker{tempo_map, normalizedGridNoteValue(grid_note_value)};
    seekNearestGridLine(walker, tempo_map, target);
    return common::core::TimePosition{tempo_map.secondsAtGlobalBeatPosition(walker.beatPosition())};
}

common::core::GridPosition nearestTempoGridPosition(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value,
    common::core::TimePosition target)
{
    MeasureGridWalker walker{tempo_map, normalizedGridNoteValue(grid_note_value)};
    seekNearestGridLine(walker, tempo_map, target);
    return walker.gridPosition();
}

// Converts either overlay or ruler clicks through the same placement path. The click column first
// becomes a timeline position, so snapping happens in musical time and the resulting seek is the
// exact grid-line time instead of a value quantized to the pixel grid.
std::optional<common::core::TimePosition> timelineCursorPlacementTime(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value,
    common::core::TimeRange visible_timeline, int timeline_width, float timeline_x,
    TimelineCursorPlacementMode mode)
{
    const std::optional<common::core::TimePosition> click_time =
        timelinePositionForX(timeline_x, visible_timeline, timeline_width);
    if (!click_time.has_value() || mode == TimelineCursorPlacementMode::Free)
    {
        return click_time;
    }

    return nearestTempoGridTime(tempo_map, grid_note_value, *click_time);
}

common::core::Fraction gridStepBeats(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value, int measure)
{
    const common::core::Fraction note_value = normalizedGridNoteValue(grid_note_value);
    const common::core::TimeSignatureChange signature = tempo_map.timeSignatureAt(measure);
    return common::core::Fraction{
        note_value.numerator * signature.denominator, note_value.denominator
    };
}

common::core::GridPosition adjacentTempoGridPosition(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value,
    const common::core::GridPosition& from, bool later)
{
    const common::core::Fraction note_value = normalizedGridNoteValue(grid_note_value);
    const common::core::GridPosition snapped =
        common::core::snapGridPosition(tempo_map, from, note_value);
    // An off-grid start whose nearest line lies in the step direction stops there first: a
    // step must never jump past the adjacent line.
    if (later ? from < snapped : snapped < from)
    {
        return snapped;
    }
    // From the lattice (or off-grid with the nearest line behind): one grid step, re-snapped;
    // the second push keeps the walk progressing across measure-anchored grid restarts, where
    // the re-snap can otherwise bounce back onto the starting line.
    const common::core::Fraction unsigned_step = gridStepBeats(tempo_map, note_value, from.measure);
    const common::core::Fraction step{
        (later ? 1 : -1) * unsigned_step.numerator, unsigned_step.denominator
    };
    common::core::GridPosition stepped = common::core::snapGridPosition(
        tempo_map, common::core::advanceGridPosition(tempo_map, snapped, step), note_value);
    if (stepped == snapped)
    {
        stepped = common::core::snapGridPosition(
            tempo_map, common::core::advanceGridPosition(tempo_map, stepped, step), note_value);
    }
    return stepped;
}

double secondsAtGridPosition(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition& position)
{
    return tempo_map.secondsAtNote(position.measure, position.beat, position.offset);
}

common::core::GridPosition fineGridPositionForBeat(
    const common::core::TempoMap& tempo_map, double global_beat)
{
    // Quantize the fractional beat to the 1/960 fine grid so the stored position stays an exact
    // rational instead of a raw double.
    double whole_beats = 0.0;
    const double beat_fraction = std::modf(std::max(0.0, global_beat), &whole_beats);
    auto beat_index = static_cast<std::int64_t>(whole_beats);
    int fine_steps =
        static_cast<int>(std::lround(beat_fraction * static_cast<double>(g_fine_grid_denominator)));
    if (fine_steps == g_fine_grid_denominator)
    {
        beat_index += 1;
        fine_steps = 0;
    }
    const auto [measure, beat] = tempo_map.beatAtGlobalIndex(beat_index);
    return common::core::GridPosition{
        .measure = measure,
        .beat = beat,
        .offset = common::core::Fraction{fine_steps, g_fine_grid_denominator},
    };
}

} // namespace rock_hero::editor::core
