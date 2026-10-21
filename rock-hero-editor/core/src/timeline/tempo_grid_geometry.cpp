#include "timeline/tempo_grid_geometry.h"

#include "timeline/timeline_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <rock_hero/common/core/timeline/timeline_x.h>

namespace rock_hero::editor::core
{

namespace
{

// Keeps rendering and snapping on the same grid even when a caller passes a corrupt note value:
// both public entry points degrade to the default grid rather than diverging or going blank.
[[nodiscard]] common::core::Fraction normalizedGridNoteValue(
    common::core::Fraction note_value) noexcept
{
    return isValidTempoGridNoteValue(note_value) ? note_value : g_default_tempo_grid_note_value;
}

// Forward walker over the measure-anchored note-value grid. Within a measure with signature
// num/den_sig, line j sits j * note_numerator * den_sig / note_denominator beats after the
// downbeat (a note value of n/d whole notes is 4n/d quarter notes, and a beat is 4/den_sig
// quarter notes, so the step is n*den_sig/d beats — exact integers over the note denominator).
// The walk restarts at every downbeat, which keeps each measure's start on a grid line even in
// meters whose length is not a multiple of the step, and it never advances past the terminal
// anchor, which may stand mid-measure. Signature and measure base-index bookkeeping is incremental
// so the per-line hot path stays integer arithmetic.
//
// The walk itself is exact, but every line it REPORTS is rounded onto the chart's tick lattice
// through the one rounding rule (common::core::nearestTick), exactly as common's own lattice
// rounds its lines — so a grid no tick divides (a septuplet's) still names only positions a chart
// may store, and draws and snaps to the same tick an imported septuplet note landed on. A line
// that would round onto the next downbeat is that downbeat, so the measure's last line stops
// before it.
class MeasureGridWalker
{
public:
    // Positions the walker on measure 1's downbeat; the note value must already be normalized.
    MeasureGridWalker(const common::core::TempoMap& tempo_map, common::core::Fraction note_value)
        : m_tempo_map(tempo_map)
        , m_note(note_value)
        , m_terminal_units(terminalUnits(tempo_map, note_value))
    {
        moveToMeasure(1, 0);
    }

    // Non-copyable: the walker is a scoped scan cursor over one tempo map reference.
    ~MeasureGridWalker() = default;
    MeasureGridWalker(const MeasureGridWalker&) = delete;
    MeasureGridWalker& operator=(const MeasureGridWalker&) = delete;
    MeasureGridWalker(MeasureGridWalker&&) = delete;
    MeasureGridWalker& operator=(MeasureGridWalker&&) = delete;

    // Reports whether the walker still addresses a line at or before the terminal anchor.
    [[nodiscard]] bool valid() const noexcept
    {
        return lineUnits() <= m_terminal_units;
    }

    // Fractional global-beat position of the current line, on the tick lattice. Dividing the raw
    // remainder is bit-identical to reducing it through Fraction first, because IEEE division is
    // correctly rounded for the same rational value, and it keeps a gcd off the per-line hot path.
    [[nodiscard]] double beatPosition() const noexcept
    {
        const std::int64_t ticks = lineTicks(m_offset_units);
        const std::int64_t whole = m_measure_beat_index + ticks / m_ticks_per_beat;
        const std::int64_t remainder = ticks % m_ticks_per_beat;
        return static_cast<double>(whole) +
               static_cast<double>(remainder) / static_cast<double>(m_ticks_per_beat);
    }

    // Seconds of the current line under the walked map.
    [[nodiscard]] double lineSeconds() const noexcept
    {
        return m_tempo_map.secondsAtGlobalBeatPosition(beatPosition());
    }

    // One-based measure number of the current line.
    [[nodiscard]] int measure() const noexcept
    {
        return m_measure;
    }

    // Musical address of the current line, on the tick lattice: an odd grid (a 1/13 note value)
    // yields the tick nearest each of its lines, which is the finest position a chart stores.
    [[nodiscard]] common::core::GridPosition gridPosition() const noexcept
    {
        const std::int64_t ticks = lineTicks(m_offset_units);
        return common::core::GridPosition{
            .measure = m_measure,
            .beat = 1 + static_cast<int>(ticks / m_ticks_per_beat),
            .offset = common::core::Fraction{
                static_cast<int>(ticks % m_ticks_per_beat), static_cast<int>(m_ticks_per_beat)
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

    // Moves to the next line, rolling into the next measure when the step leaves this one — or
    // lands on a line that rounds onto its downbeat.
    void advance()
    {
        m_offset_units += m_step_units;
        if (m_offset_units >= m_measure_units || roundsOntoNextDownbeat(m_offset_units))
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

        const common::core::TimeSignature previous_signature =
            m_tempo_map.timeSignatureAt(m_measure - 1).meter;
        moveToMeasure(m_measure - 1, m_measure_beat_index - previous_signature.numerator);
        m_offset_units = lastLineOffsetUnits();
        return true;
    }

    // Positions the walker on the first line whose seconds are at or after the target. Returns
    // false when every grid line lies before the target; the walker is then past the end and
    // moveToLastLine() recovers the final candidate.
    [[nodiscard]] bool seekFirstLineAtOrAfter(double seconds)
    {
        // Read straight off the map rather than searched for: the measure the target's beat falls
        // in, then the first step at or past its offset into that measure. A target outside the
        // authored range clamps to its first or terminal beat.
        const double beat_position = m_tempo_map.beatPositionAtSeconds(seconds);
        const int measure =
            m_tempo_map.beatAtGlobalIndex(static_cast<std::int64_t>(std::floor(beat_position)))
                .first;
        moveToMeasure(measure, m_tempo_map.globalBeatIndex(measure, 1));
        const double offset_units = (beat_position - static_cast<double>(m_measure_beat_index)) *
                                    static_cast<double>(m_note.denominator);
        const auto step = std::max<std::int64_t>(
            0,
            static_cast<std::int64_t>(std::ceil(offset_units / static_cast<double>(m_step_units))));
        const std::int64_t last_step = lastLineOffsetUnits() / m_step_units;
        if (step > last_step)
        {
            // Every line of this measure is before the target, so the candidate is the next
            // measure's downbeat (when one exists inside the authored range).
            m_offset_units = last_step * m_step_units;
            advance();
        }
        else
        {
            m_offset_units = step * m_step_units;
        }

        // The seed went through floating time, so a line within rounding of the target may sit
        // one step off; settle it on the exact test the seek promises, in seconds.
        while (valid() && lineSeconds() < seconds)
        {
            advance();
        }
        while (valid() && retreat())
        {
            if (lineSeconds() < seconds)
            {
                advance();
                break;
            }
        }
        return valid();
    }

    // Positions the walker on the final grid line: the last step that fits at or before the
    // terminal anchor inside the terminal measure.
    void moveToLastLine()
    {
        const int terminal_measure = common::core::terminalGridPosition(m_tempo_map).measure;
        moveToMeasure(terminal_measure, m_tempo_map.globalBeatIndex(terminal_measure, 1));
        m_offset_units = lastLineOffsetUnits();
    }

private:
    // The terminal's exact position in note-denominator units, floored. A line's units are whole,
    // so a line is at or before the terminal exactly when its units are at or before this: the
    // floor loses nothing, and a terminal mid-beat needs no rational comparison per line.
    [[nodiscard]] static std::int64_t terminalUnits(
        const common::core::TempoMap& tempo_map, const common::core::Fraction note_value)
    {
        const common::core::GridPosition terminal = common::core::terminalGridPosition(tempo_map);
        const std::int64_t beat_units =
            tempo_map.globalBeatIndex(terminal.measure, terminal.beat) * note_value.denominator;
        // The offset is in [0, 1) of a beat, so its share of the units floors by integer division.
        return beat_units + (static_cast<std::int64_t>(terminal.offset.numerator) *
                             note_value.denominator / terminal.offset.denominator);
    }

    // Rebinds the per-measure geometry for a measure whose downbeat global beat index is known.
    // The signature lookup runs once per measure, not per line, so the scan stays cheap. The
    // reign's meter has both terms clamped to at least one, so the step below is never zero and
    // the walk always progresses, a malformed map's included.
    void moveToMeasure(int measure, std::int64_t measure_beat_index)
    {
        m_measure = measure;
        m_measure_beat_index = measure_beat_index;
        m_offset_units = 0;
        m_signature = m_tempo_map.timeSignatureAt(measure).meter;
        m_step_units = static_cast<std::int64_t>(m_note.numerator) * m_signature.denominator;
        m_measure_units = static_cast<std::int64_t>(m_signature.numerator) * m_note.denominator;
        // A meter whose denominator no tick divides has no lattice; its beat is then its own
        // unit, which keeps that malformed map's walk progressing like the clamps above.
        m_ticks_per_beat = common::core::g_tick_quantum_denominator % m_signature.denominator == 0
                               ? common::core::g_tick_quantum_denominator / m_signature.denominator
                               : 1;
    }

    // Current line's exact position in beat units (beats times the note denominator).
    [[nodiscard]] std::int64_t lineUnits() const noexcept
    {
        return m_measure_beat_index * m_note.denominator + m_offset_units;
    }

    // Ticks from the downbeat to the line at an exact offset, rounded onto the lattice.
    [[nodiscard]] std::int64_t lineTicks(std::int64_t offset_units) const noexcept
    {
        return common::core::nearestTick(offset_units * m_ticks_per_beat, m_note.denominator);
    }

    // Whether the line at an offset inside this measure rounds onto the next measure's downbeat.
    [[nodiscard]] bool roundsOntoNextDownbeat(std::int64_t offset_units) const noexcept
    {
        return lineTicks(offset_units) >= m_signature.numerator * m_ticks_per_beat;
    }

    // Last line offset that stays inside this measure and inside the authored beat range.
    [[nodiscard]] std::int64_t lastLineOffsetUnits() const noexcept
    {
        std::int64_t last_offset = ((m_measure_units - 1) / m_step_units) * m_step_units;
        if (last_offset > 0 && roundsOntoNextDownbeat(last_offset))
        {
            last_offset -= m_step_units;
        }
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

    // The terminal anchor in note-denominator units (terminalUnits); no line may pass it.
    std::int64_t m_terminal_units;

    // Signature active for the current measure.
    common::core::TimeSignature m_signature{};

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

    // Ticks in one beat at the current measure's meter.
    std::int64_t m_ticks_per_beat{1};
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

    // Bounds the scan through the mapping's inverse, loosely: one-pixel margins absorb the rounding
    // in the forward map so the bounds never exclude a line whose rounded column still lands
    // inside the visible span, and the exact per-line column test below stays the real gate.
    const auto drawing_width = static_cast<double>(width);
    const double window_start_seconds = common::core::timelineSecondsForX(
        static_cast<double>(visible_x_begin - 1), visible_timeline, drawing_width);
    const double window_end_seconds = common::core::timelineSecondsForX(
        static_cast<double>(visible_x_end), visible_timeline, drawing_width);

    MeasureGridWalker walker{tempo_map, note_value};
    if (!walker.seekFirstLineAtOrAfter(window_start_seconds))
    {
        return lines;
    }

    // Line positions increase along the measure walk, so a forward cursor resolves each line's
    // time in amortized constant time; it returns the same values as the seek's random-access
    // query.
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

        const int column = timelineColumnForX(*x, width);
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
void seekNearestGridLine(MeasureGridWalker& walker, common::core::TimePosition target)
{
    if (!walker.seekFirstLineAtOrAfter(target.seconds))
    {
        // Every grid line lies before the target, so the last line is the only candidate.
        walker.moveToLastLine();
        return;
    }

    const double after_seconds = walker.lineSeconds();
    if (!walker.retreat())
    {
        return;
    }

    const double before_seconds = walker.lineSeconds();
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
    seekNearestGridLine(walker, target);
    return common::core::TimePosition{walker.lineSeconds()};
}

common::core::GridPosition nearestTempoGridPosition(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value,
    common::core::TimePosition target)
{
    MeasureGridWalker walker{tempo_map, normalizedGridNoteValue(grid_note_value)};
    seekNearestGridLine(walker, target);
    return walker.gridPosition();
}

// Converts either overlay or ruler clicks through the same placement path. The click column first
// becomes a timeline position, so snapping happens in musical time and the resulting seek is the
// exact lattice time instead of a value quantized to the pixel grid.
std::optional<common::core::TimePosition> timelineCursorPlacementTime(
    const common::core::TempoMap& tempo_map, common::core::Fraction placement_quantum,
    common::core::TimeRange visible_timeline, int timeline_width, float timeline_x)
{
    const std::optional<common::core::TimePosition> click_time =
        timelinePositionForX(timeline_x, visible_timeline, timeline_width);
    if (!click_time.has_value())
    {
        return click_time;
    }

    return nearestTempoGridTime(tempo_map, placement_quantum, *click_time);
}

// The editor's only contribution is its note-value validity policy; the lattice walk itself is
// common core's, where the nearest-line snap also lives, so the two can never disagree on which
// lines exist. Never a snap-step-resnap walk here, which is not reversible: a re-snap picks the
// nearest line to wherever the step landed, which skips a measure's last line whenever it sits
// exactly half a step before the next downbeat (a 1/4 grid in 7/8).
common::core::GridPosition adjacentTempoGridPosition(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value,
    const common::core::GridPosition& from, bool later)
{
    return common::core::adjacentGridPosition(
        tempo_map, from, normalizedGridNoteValue(grid_note_value), later);
}

} // namespace rock_hero::editor::core
