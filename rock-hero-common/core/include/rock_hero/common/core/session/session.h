/*!
\file session.h
\brief Editable session state for the current song workspace.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <type_traits>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Editable in-memory session state.

Session owns the Song currently loaded into the editor workflow. It is deliberately headless so
controllers and tests can exercise session behavior without UI, audio devices, or Tracktion. The
current editor presents one arrangement at a time; before a project is opened, the session contains
no arrangements.
*/
class Session
{
public:
    /*! \brief Creates an empty session with no loaded project arrangements. */
    Session();

    /*! \brief Clears the current song and returns the session to the no-project state. */
    void reset();

    /*!
    \brief Returns the loaded song aggregate.
    \return Current song data owned by the session.
    */
    [[nodiscard]] const Song& song() const noexcept;

    /*!
    \brief Returns arrangements in project order.
    \return Ordered arrangements owned by the session.
    */
    [[nodiscard]] const std::vector<Arrangement>& arrangements() const noexcept;

    /*!
    \brief Returns the timeline range the song covers: from its first beat to the later of the
    current arrangement's audio end and the tempo map's terminal anchor.
    \return The song's timeline range, empty without an arrangement.
    */
    [[nodiscard]] TimeRange timeline() const noexcept;

    /*!
    \brief Returns the arrangement currently displayed by the editor.
    \return Current arrangement, or nullptr when no project is loaded.
    */
    [[nodiscard]] const Arrangement* currentArrangement() const noexcept;

    /*!
    \brief Returns mutable access to the current arrangement's tone schedule.

    The narrowest mutation surface the tone editing workflow needs; broader arrangement fields
    stay read-only through this session.

    \return Current arrangement's tone track, or null when no arrangement is loaded.
    */
    [[nodiscard]] ToneTrack* currentToneTrack() noexcept;

    /*!
    \brief Returns mutable access to the current arrangement's tone catalog.

    Parallels currentToneTrack() as the second narrow mutation surface the tone editing workflow
    needs: renaming a tone and creating a tone-change region both edit the catalog, while broader
    arrangement fields stay read-only through this session.

    \return Current arrangement's tone catalog, or null when no arrangement is loaded.
    */
    [[nodiscard]] std::vector<Tone>* currentToneCatalog() noexcept;

    /*!
    \brief Returns mutable access to the current arrangement's plugin-parameter automation.

    The third narrow mutation surface the tone editing workflow needs: automation points are the
    persisted musical truth the automation lanes edit, while broader arrangement fields stay
    read-only through this session.

    \return Current arrangement's automation entries, or null when no arrangement is loaded.
    */
    [[nodiscard]] std::vector<ToneParameterAutomation>* currentToneAutomation() noexcept;

    /*!
    \brief Writes the current arrangement's chart through `write`, then advances
    chartNotesRevision(), and with it chartRevision().

    The fourth narrow mutation surface: chart editing mutates through this scope while broader
    arrangement fields stay read-only through this session. A SCOPE rather than a handed-out
    pointer, so the revisions the projection caches key on advance exactly when the write has
    ended: no read can fall between a revision advancing and the chart it names being written,
    and a cache can never memoize a chart under the revision of its successor. Every write
    advances them, whether or not it changed anything, which costs at most one lazy rebuild.

    \tparam Write Callable taking the chart by reference.
    \param write The write.
    \return For a void write, whether a chart was there to write; otherwise the write's result, or
            nothing when no arrangement or no chart is loaded. The revision advances only when
            the write ran.
    */
    template <typename Write> [[nodiscard]] auto writeChart(const Write& write)
    {
        using Result = std::invoke_result_t<const Write&, Chart&>;
        Chart* const chart = loadedChart();
        if constexpr (std::is_void_v<Result>)
        {
            if (chart == nullptr)
            {
                return false;
            }
            write(*chart);
            ++m_notes_revision;
            return true;
        }
        else
        {
            if (chart == nullptr)
            {
                return std::optional<Result>{};
            }
            std::optional<Result> result{write(*chart)};
            ++m_notes_revision;
            return result;
        }
    }

    /*!
    \brief Replaces the current arrangement's authored fret-hand positions, then advances
    chartRevision() alone (a marker write).

    The eighth narrow mutation surface. Separate from writeChart() because it is the one chart
    write that leaves the notes untouched, and the notes are what chartNotesRevision() keys: a
    position edit must not invalidate the notes layer derived from them, so it cannot go through
    the scope that advances that key.

    \param positions Positions the caller has already validated, strictly ascending.
    \return False, writing nothing, when no arrangement or no chart is loaded.
    */
    [[nodiscard]] bool writeFretHandPositions(std::vector<FretHandPosition> positions);

    /*!
    \brief Replaces the song's structure sections, then advances chartRevision() alone (a marker
    write).

    The fifth narrow mutation surface, and the first song-level one: sections describe the song's
    structure rather than one arrangement's tab, so section authoring reaches them here. Sections
    feed the highway projection's labels and camera zones, so a section change IS a projection
    change: it advances the same revision a chart write does. The notes layer reads no section, so
    chartNotesRevision() stays where it was.

    \param sections Section markers the caller has already validated, in ascending position order.
    */
    void setSongSections(std::vector<SongSection> sections);

    /*!
    \brief Replaces the song's descriptive metadata, advancing no revision.

    The ninth narrow mutation surface, song-level like the sections. Nothing projected reads the
    metadata (no chart, highway or tone projection draws a title or a year), so no derived layer
    goes stale and no revision advances; the song document writer is its only reader.

    \param metadata Metadata the caller has already normalized and validated.
    */
    void setSongMetadata(SongMetadata metadata);

    /*!
    \brief Replaces the song's tempo map, then advances chartNotesRevision(), and with it
    chartRevision().

    The sixth narrow mutation surface, song-level like the sections. The tempo map places every
    chart note in time, so a tempo change IS a change to every chart projection and to the notes
    layer: it advances the revisions a chart write does.

    \param tempo_map Tempo map the caller has already validated.
    */
    void setTempoMap(TempoMap tempo_map);

    /*!
    \brief Replaces the song's whole timeline at once, then advances chartNotesRevision(), and with
    it chartRevision(), once.

    The seventh narrow mutation surface, and the only one that reaches a NON-current arrangement: a
    meter edit re-addresses every arrangement, and its undo restores them all, so the map, the
    sections and every arrangement's chart, tone track and tone automation are written together
    (\ref writeSongTimeline). Nothing else in the song changes, so the loaded arrangement, its audio
    and the session timeline derived from that audio stay exactly as they were.

    \param timeline Timeline the caller has already validated, read from this session's song.
    \return False, writing nothing, when its arrangements are not this song's, by id and in order.
    */
    [[nodiscard]] bool setSongTimeline(SongTimeline timeline);

    /*!
    \brief Returns the chart revision counter that keys chart-projection caches: the sum of
    chartNotesRevision() and the marker writes.

    Every write that can change a projection advances exactly one of the two counters it sums, so
    the sum advances by one on every writeChart() or writeFretHandPositions() write, every
    replacement of the tempo map, the song sections or the whole timeline, and every accepted
    loadSong(), and it never repeats a value. View-state caches pair it with the arrangement id so
    chart, tempo and section edits invalidate memoized projections without any explicit
    notification path that could be forgotten; the load advance keeps a re-opened project whose
    persisted arrangement ids match from reusing the edited projection.

    \return Monotonic count of chart and position writes, tempo-map, section and timeline
            replacements and accepted song loads.
    */
    [[nodiscard]] std::uint64_t chartRevision() const noexcept;

    /*!
    \brief Returns the revision counter of the current chart's NOTES, the key of the notes layer
    derived from them (\ref chartResolutions).

    Advances on every writeChart() write, every tempo-map and whole-timeline replacement, and every
    accepted loadSong(): everything that can change the notes, the shape holds, the capo or the
    tempo map they are placed by. The marker writes — a section replacement and
    writeFretHandPositions(), neither of which the notes layer reads — advance chartRevision()
    alone, so a cache keyed on this survives them. Paired with the arrangement id, like
    chartRevision().

    \return Monotonic count of the writes and replacements above.
    */
    [[nodiscard]] std::uint64_t chartNotesRevision() const noexcept;

    /*!
    \brief Replaces the current session song.

    The selected arrangement index is stored as session state, not as persistent arrangement
    identity. The selected arrangement must exist and every arrangement must have playable audio.

    \param song Song aggregate to make current.
    \param selected_arrangement Arrangement index displayed by the editor.
    \return True when the song was accepted.
    */
    bool loadSong(Song song, std::size_t selected_arrangement);

private:
    // The current arrangement's chart, or null when no arrangement or no chart is loaded: what
    // writeChart writes, and the only mutable path to it.
    [[nodiscard]] Chart* loadedChart() noexcept;

    // Song aggregate currently loaded into the editor session.
    Song m_song;

    // Index of the arrangement currently shown by the single-arrangement editor surface.
    std::size_t m_current_arrangement_index{0};

    // Every write that changes a projection advances exactly one of these two, so their sum,
    // chartRevision(), advances on each. Both deliberately survive loadSong/reset so neither the
    // sum nor the notes key can repeat a value a cache might still hold.

    // Monotonic count of the writes that can change the notes layer, and of song loads.
    std::uint64_t m_notes_revision{0};

    // Monotonic count of the marker writes the notes layer never reads: positions and sections.
    std::uint64_t m_markers_revision{0};
};

} // namespace rock_hero::common::core
