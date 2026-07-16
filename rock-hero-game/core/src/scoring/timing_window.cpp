#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/game/core/scoring/scoring_ruleset.h>
#include <rock_hero/game/core/scoring/timing_window.h>

namespace rock_hero::game::core
{

namespace
{

// Real milliseconds span speed_factor-times as many song-time milliseconds: at half speed the
// song advances 0.5 s per real second, so a 100 ms real interval covers 50 ms of song time.
[[nodiscard]] double songSecondsFromRealMs(double real_ms, double speed_factor) noexcept
{
    return real_ms / 1000.0 * speed_factor;
}

} // namespace

// Inclusive on both bounds so a window edge is a hit, matching the ruleset's ± reading.
bool HitWindow::contains(double song_seconds) const noexcept
{
    return song_seconds >= open_seconds && song_seconds <= close_seconds;
}

// The half-width is a real-time constant converted into song time, so the window feels the same
// to the player at every playback speed instead of widening as the song slows down.
HitWindow makeOnsetWindow(
    const common::core::TempoMap& tempo_map, common::core::GridPosition position,
    const ScoringRuleset& ruleset, double speed_factor) noexcept
{
    const double expected_seconds =
        tempo_map.secondsAtNote(position.measure, position.beat, position.offset);
    const double half_width_seconds =
        songSecondsFromRealMs(ruleset.onset_window_half_width_ms, speed_factor);
    return HitWindow{
        .open_seconds = expected_seconds - half_width_seconds,
        .close_seconds = expected_seconds + half_width_seconds,
    };
}

// Plan 13's effective-offset contract applied at consumption: t_play = t_in - audio_offset. The
// offset is measured in real time, so it converts through the speed factor like the window does.
double playedSongTime(
    double observed_song_seconds, double audio_offset_ms, double speed_factor) noexcept
{
    return observed_song_seconds - songSecondsFromRealMs(audio_offset_ms, speed_factor);
}

// The song-time delta divided by the speed factor recovers the player's real-time error, which
// is what the verdict records (signed, negative = early) for the early/late tendency display.
double timingDeltaMs(
    double expected_song_seconds, double played_song_seconds, double speed_factor) noexcept
{
    return (played_song_seconds - expected_song_seconds) * 1000.0 / speed_factor;
}

} // namespace rock_hero::game::core
