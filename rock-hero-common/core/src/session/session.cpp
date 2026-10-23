#include "session/session.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Reports whether backend audio loading prepared the arrangement for a playable session.
[[nodiscard]] bool hasLoadedAudio(const Arrangement& arrangement) noexcept
{
    return !arrangement.audio_asset.path.empty() && arrangement.audio_duration.seconds > 0.0;
}

// Reports whether every arrangement has passed backend audio validation before session commit.
[[nodiscard]] bool hasLoadedAudio(const Song& song)
{
    return std::ranges::all_of(song.arrangements, [](const Arrangement& arrangement) {
        return hasLoadedAudio(arrangement);
    });
}

} // namespace

// Starts empty until a real project file has been opened.
Session::Session()
{
    reset();
}

// Restores the no-project editor session after the current project is closed.
void Session::reset()
{
    m_song = Song{};
    m_current_arrangement_index = 0;
}

// Exposes the full loaded song aggregate for read-only editor and test observation.
const Song& Session::song() const noexcept
{
    return m_song;
}

// Exposes ordered arrangements without letting callers mutate the vector shape directly.
const std::vector<Arrangement>& Session::arrangements() const noexcept
{
    return m_song.arrangements;
}

// Derived on every read, so no edit can leave it stale. It begins at the song's first beat and runs
// to whichever ends later: the current arrangement's audio, which a positive asset offset pushes
// later (silence fills the gap before the audio starts), or the tempo map's terminal anchor, the
// end of everything the grid addresses, so the whole chart is reachable on screen.
TimeRange Session::timeline() const noexcept
{
    const Arrangement* const arrangement = currentArrangement();
    if (arrangement == nullptr)
    {
        return TimeRange{};
    }
    const std::vector<BeatAnchor>& anchors = m_song.tempo_map.anchors();
    const double terminal_seconds = anchors.empty() ? 0.0 : anchors.back().seconds();
    return TimeRange{
        .start = TimePosition{},
        .end =
            TimePosition{std::max(arrangement->audioTimelineRange().end.seconds, terminal_seconds)},
    };
}

// Returns the arrangement shown by the current single-arrangement editor surface.
const Arrangement* Session::currentArrangement() const noexcept
{
    if (m_current_arrangement_index >= m_song.arrangements.size())
    {
        return nullptr;
    }

    return &m_song.arrangements[m_current_arrangement_index];
}

// Commits a fully prepared song once every arrangement has backend-loaded audio.
bool Session::loadSong(Song song, std::size_t selected_arrangement)
{
    if (song.arrangements.empty() || selected_arrangement >= song.arrangements.size())
    {
        return false;
    }

    if (!hasLoadedAudio(song))
    {
        return false;
    }

    m_song = std::move(song);
    ++m_notes_revision;
    m_current_arrangement_index = selected_arrangement;
    return true;
}

// Returns mutable access to the current arrangement's tone schedule.
ToneTrack* Session::currentToneTrack() noexcept
{
    if (m_current_arrangement_index >= m_song.arrangements.size())
    {
        return nullptr;
    }

    return &m_song.arrangements[m_current_arrangement_index].tone_track;
}

// Returns mutable access to the current arrangement's tone catalog.
std::vector<Tone>* Session::currentToneCatalog() noexcept
{
    if (m_current_arrangement_index >= m_song.arrangements.size())
    {
        return nullptr;
    }

    return &m_song.arrangements[m_current_arrangement_index].tones;
}

// Returns mutable access to the current arrangement's plugin-parameter automation.
std::vector<ToneParameterAutomation>* Session::currentToneAutomation() noexcept
{
    if (m_current_arrangement_index >= m_song.arrangements.size())
    {
        return nullptr;
    }

    return &m_song.arrangements[m_current_arrangement_index].tone_automation;
}

// The chart writeChart writes; the revision is writeChart's to advance, once the write has ended.
Chart* Session::loadedChart() noexcept
{
    if (m_current_arrangement_index >= m_song.arrangements.size())
    {
        return nullptr;
    }

    std::optional<Chart>& chart = m_song.arrangements[m_current_arrangement_index].chart;
    if (!chart.has_value())
    {
        return nullptr;
    }

    return &*chart;
}

// Song-level rather than arrangement-scoped, so it needs no loaded-arrangement guard: every
// arrangement shares one list.
void Session::setSongSections(std::vector<SongSection> sections)
{
    m_song.sections = std::move(sections);
    ++m_markers_revision;
}

// No projection reads the metadata, so no revision advances.
void Session::setSongMetadata(SongMetadata metadata)
{
    m_song.metadata = std::move(metadata);
}

// The tempo map places every note, so it is a notes write.
void Session::setTempoMap(TempoMap tempo_map)
{
    m_song.tempo_map = std::move(tempo_map);
    ++m_notes_revision;
}

// Rewrites every arrangement's chart and the tempo map together, so it is one notes write.
bool Session::setSongTimeline(SongTimeline timeline)
{
    if (!writeSongTimeline(m_song, std::move(timeline)))
    {
        return false;
    }
    ++m_notes_revision;
    return true;
}

// The positions alone, a marker write: the notes key stays, so the notes layer derived from them
// survives.
bool Session::writeFretHandPositions(std::vector<FretHandPosition> positions)
{
    Chart* const chart = loadedChart();
    if (chart == nullptr)
    {
        return false;
    }
    chart->fret_hand_positions = std::move(positions);
    ++m_markers_revision;
    return true;
}

// Every projection-input write advances one of the two counters, so their sum keys projections.
std::uint64_t Session::chartRevision() const noexcept
{
    return m_notes_revision + m_markers_revision;
}

// Returns the monotonic write count that keys the notes layer.
std::uint64_t Session::chartNotesRevision() const noexcept
{
    return m_notes_revision;
}

} // namespace rock_hero::common::core
