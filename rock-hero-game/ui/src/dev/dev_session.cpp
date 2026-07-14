#include "dev/dev_session.h"

#include <algorithm>
#include <chrono>
#include <expected>
#include <print>
#include <rock_hero/common/core/highway/highway_projection.h>
#include <rock_hero/common/core/package/rock_song_package.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/core/song/song.h>
#include <string>
#include <system_error>
#include <utility>

namespace rock_hero::game::ui
{

namespace
{

// Source-probe cadence: with the watcher's settle interval on top, an edit reaches the highway
// well inside the phase's one-second budget without touching the filesystem every frame.
constexpr std::chrono::nanoseconds g_probe_interval = std::chrono::milliseconds{250};

// Editing headroom before a "previous section" seek skips back past the section underway.
constexpr double g_previous_section_grace_seconds = 0.75;

// Editor project packages (.rhp) wrap the song content in a song/ subdirectory beside
// project.json; bare song packages carry song.json at the root. Accept both.
[[nodiscard]] std::filesystem::path songContentRoot(const std::filesystem::path& root)
{
    std::error_code probe_error;
    return std::filesystem::exists(root / "song" / "song.json", probe_error) ? root / "song" : root;
}

// Reads the package into a Song, extracting archives through a scratch workspace that is removed
// immediately after the read.
[[nodiscard]] std::expected<common::core::Song, common::core::SongPackageError> readPackage(
    const std::filesystem::path& package_path)
{
    std::error_code probe_error;
    if (std::filesystem::is_directory(package_path, probe_error))
    {
        return common::core::readRockSongPackageDirectory(songContentRoot(package_path));
    }
    const std::filesystem::path workspace =
        std::filesystem::temp_directory_path() /
        ("rock-hero-dev-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(workspace);
    auto extracted = common::core::readRockSongPackage(package_path, workspace);
    if (!extracted.has_value() &&
        std::filesystem::exists(workspace / "song" / "song.json", probe_error))
    {
        extracted = common::core::readRockSongPackageDirectory(workspace / "song");
    }
    std::error_code cleanup_error;
    std::filesystem::remove_all(workspace, cleanup_error);
    return extracted;
}

} // namespace

std::optional<DevSession> DevSession::create(
    std::filesystem::path package_path, const bool lefty, const std::chrono::nanoseconds now)
{
    DevSession session{std::move(package_path), lefty};
    std::optional<common::core::HighwayViewState> state = session.loadViewState();
    if (!state.has_value())
    {
        // Startup diagnostics stay on stderr, where a dev launching from a terminal sees them.
        std::println(stderr, "rock-hero: dev package produced no loadable charted arrangement");
        return std::nullopt;
    }

    session.m_sections = state->sections;
    session.m_loaded_state = std::move(state);
    session.m_clock_anchor = now;
    session.refreshWatchedFiles();
    return session;
}

DevSession::DevSession(std::filesystem::path package_path, const bool lefty)
    : m_package_path{std::move(package_path)}
    , m_lefty{lefty}
{}

std::optional<common::core::HighwayViewState> DevSession::takeLoadedViewState()
{
    return std::exchange(m_loaded_state, std::nullopt);
}

std::optional<common::core::HighwayViewState> DevSession::pollForReload(
    const std::chrono::nanoseconds now)
{
    if (now - m_last_probe_time < g_probe_interval)
    {
        return std::nullopt;
    }
    m_last_probe_time = now;

    if (!m_watcher.update(probeSourceStamp(), now))
    {
        return std::nullopt;
    }
    return reload(now);
}

std::optional<common::core::HighwayViewState> DevSession::reload(const std::chrono::nanoseconds now)
{
    std::optional<common::core::HighwayViewState> state = loadViewState();
    if (!state.has_value())
    {
        // The previous content stays on screen; the next settled edit gets another chance.
        RH_LOG_WARNING("game.dev", "chart hot-reload failed; keeping the previous content");
        return std::nullopt;
    }

    m_sections = state->sections;
    refreshWatchedFiles();
    RH_LOG_INFO(
        "game.dev",
        "chart hot-reload applied notes={} sections={} song_time_s={:.3f}",
        state->notes.size(),
        state->sections.size(),
        clockSnapshotAt(now).position.seconds);
    return state;
}

common::audio::PlaybackClockSnapshot DevSession::clockSnapshotAt(
    const std::chrono::nanoseconds now) const
{
    return common::audio::PlaybackClockSnapshot{
        .position =
            common::core::TimePosition{static_cast<double>((now - m_clock_anchor).count()) / 1.0e9},
        .monotonic_capture_time = now,
        .playback_rate = 1.0,
        .playing = true,
    };
}

void DevSession::seekToSection(const std::size_t section_index, const std::chrono::nanoseconds now)
{
    if (section_index >= m_sections.size())
    {
        return;
    }
    const double target_seconds = m_sections[section_index].seconds;
    m_clock_anchor =
        now - std::chrono::nanoseconds{static_cast<std::int64_t>(target_seconds * 1.0e9)};
    RH_LOG_INFO(
        "game.dev",
        "seek to section index={} type={:?} song_time_s={:.3f}",
        section_index,
        m_sections[section_index].type,
        target_seconds);
}

std::optional<std::size_t> DevSession::sectionAfter(const double seconds) const
{
    for (std::size_t index = 0; index < m_sections.size(); ++index)
    {
        if (m_sections[index].seconds > seconds)
        {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> DevSession::sectionBefore(const double seconds) const
{
    std::optional<std::size_t> best;
    for (std::size_t index = 0; index < m_sections.size(); ++index)
    {
        if (m_sections[index].seconds < seconds - g_previous_section_grace_seconds)
        {
            best = index;
        }
    }
    return best;
}

// Exposes the target seconds so gameplay-session runs seek the engine transport rather than the
// stand-in clock.
std::optional<double> DevSession::sectionStartSeconds(const std::size_t section_index) const
{
    if (section_index >= m_sections.size())
    {
        return std::nullopt;
    }
    return m_sections[section_index].seconds;
}

// Exposes which arrangement the display chose so the gameplay session loads the same one.
const std::string& DevSession::chosenArrangementId() const noexcept
{
    return m_chosen_arrangement_id;
}

std::optional<common::core::HighwayViewState> DevSession::loadViewState()
{
    const std::expected<common::core::Song, common::core::SongPackageError> song =
        readPackage(m_package_path);
    if (!song.has_value())
    {
        RH_LOG_WARNING("game.dev", "dev package load failed: {}", song.error().message);
        return std::nullopt;
    }

    // Prefer a charted guitar part: bass arrangements exercise few of the chord and technique
    // visuals this dev fixture exists to inspect (packages often list bass first).
    const common::core::Arrangement* chosen = nullptr;
    for (const common::core::Arrangement& arrangement : song->arrangements)
    {
        if (!arrangement.chart.has_value())
        {
            continue;
        }
        if (chosen == nullptr || (chosen->part == common::core::Part::Bass &&
                                  arrangement.part != common::core::Part::Bass))
        {
            chosen = &arrangement;
        }
    }
    if (chosen != nullptr)
    {
        // Recorded so the gameplay session loads the same arrangement the display shows.
        m_chosen_arrangement_id = chosen->id;

        // Lowest-pitched string on top is the 3D notation's default (user decision 2026-07-11,
        // recorded in plan 25); the shared projection's invert flag realizes it, and plans 26/27
        // surface the per-player setting later.
        common::core::HighwayViewState state = common::core::makeHighwayViewState(
            *chosen,
            song->tempo_map,
            common::core::HighwayDisplayOptions{.mirrored = m_lefty, .invert_string_order = true});
        RH_LOG_INFO(
            "game.highway",
            "dev package loaded notes={} beats={} sections={} lefty={}",
            state.notes.size(),
            state.beats.size(),
            state.sections.size(),
            m_lefty);
        return state;
    }

    RH_LOG_WARNING("game.dev", "dev package has no charted arrangement");
    return std::nullopt;
}

// Directory packages watch the song document plus every chart sidecar, because those are the
// files an editor touches when the chart changes; archive packages watch the archive itself.
void DevSession::refreshWatchedFiles()
{
    m_watched_files.clear();

    std::error_code probe_error;
    if (!std::filesystem::is_directory(m_package_path, probe_error))
    {
        m_watched_files.push_back(m_package_path);
        return;
    }

    const std::filesystem::path content_root = songContentRoot(m_package_path);
    m_watched_files.push_back(content_root / "song.json");
    const std::filesystem::path charts_directory = content_root / "charts";
    if (std::filesystem::is_directory(charts_directory, probe_error))
    {
        for (const auto& entry : std::filesystem::directory_iterator{charts_directory, probe_error})
        {
            if (entry.is_regular_file(probe_error))
            {
                m_watched_files.push_back(entry.path());
            }
        }
    }
}

std::optional<std::chrono::nanoseconds> DevSession::probeSourceStamp() const
{
    std::chrono::nanoseconds newest{0};
    for (const std::filesystem::path& file : m_watched_files)
    {
        std::error_code probe_error;
        const std::filesystem::file_time_type stamp =
            std::filesystem::last_write_time(file, probe_error);
        if (probe_error)
        {
            // A missing file usually means an atomic replace in flight; skip this probe rather
            // than feed the watcher a misleading stamp.
            return std::nullopt;
        }
        newest = std::max(
            newest, std::chrono::duration_cast<std::chrono::nanoseconds>(stamp.time_since_epoch()));
    }
    return newest;
}

} // namespace rock_hero::game::ui
