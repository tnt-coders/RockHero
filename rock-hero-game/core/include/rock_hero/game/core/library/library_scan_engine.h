/*!
\file library_scan_engine.h
\brief Headless, step-driven engine that scans song packages into the library index.

The engine owns the deterministic scan pipeline (list -> plan -> describe and generate album art for each package)
behind three injected ports, so it is fully testable with fakes and never touches the real
filesystem in tests. It is persistence-free by design: it exposes the working index and signals a
commit checkpoint after each package, and a thin game/app runner performs the actual save through
the Phase-2 free functions on its worker thread. This mirrors the "record a signal, the shell
performs the effect" split of DiagnosticsController rather than mutating the filesystem inside the
orchestration (docs/design/architectural-principles.md "Separate State From Side Effects").
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <rock_hero/common/core/shared/cancellation_token.h>
#include <rock_hero/game/core/library/i_album_art_generator.h>
#include <rock_hero/game/core/library/i_library_directory_lister.h>
#include <rock_hero/game/core/library/i_library_package_describer.h>
#include <rock_hero/game/core/library/library_index.h>
#include <rock_hero/game/core/library/library_scan_plan.h>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::game::core
{

/*! \brief Observable phase of a library scan. */
enum class LibraryScanPhase : std::uint8_t
{
    /*! \brief No scan has begun; begin() is the only meaningful operation. */
    Idle,

    /*! \brief A scan is in progress; step() advances it one package at a time. */
    Scanning,

    /*! \brief The scan stopped early on a cancellation request; the working index is partial. */
    Cancelled,

    /*! \brief Every planned action has been applied; the working index is final. */
    Complete,
};

/*! \brief How far a library scan has progressed. */
struct LibraryScanProgress
{
    /*! \brief Planned actions applied so far. */
    std::size_t completed_actions{0};

    /*! \brief Total planned actions for the whole scan. */
    std::size_t total_actions{0};

    /*! \brief Package the most recent step processed; empty before the first step. */
    std::filesystem::path current_package;
};

/*! \brief Outcome of advancing a scan by one step. */
struct LibraryScanStep
{
    /*! \brief Scan phase after this step. */
    LibraryScanPhase phase{LibraryScanPhase::Idle};

    /*! \brief Progress after this step. */
    LibraryScanProgress progress;

    /*!
    \brief True when the working index reached a durable point the runner may persist now.

    Signalled after every applied action so that an interruption loses at most the in-flight
    package. The runner owns how often it actually writes (it may throttle), but persisting on
    every checkpoint is what delivers the "at most one entry lost" guarantee.
    */
    bool commit_checkpoint{false};
};

/*!
\brief Drives a whole library scan synchronously, one package per step().

Holds references to the three scan ports and owns the scan's working state; game/app constructs it
with real adapters and pumps step() on a dedicated thread, marshalling progress back to the message
thread (docs/design/architectural-principles.md "Keep Threading at the Boundary"). The engine never
persists: after each step the caller reads index() and, on a commit checkpoint, saves it with the
Phase-2 saveLibraryIndex free function.
*/
class LibraryScanEngine
{
public:
    /*!
    \brief Creates an idle engine over the three scan ports.
    \param lister Enumerates package files under the scan roots.
    \param describer Peeks each package's description.
    \param album_art_generator Produces cached album-art images (the null default until plan 43).
    */
    LibraryScanEngine(
        ILibraryDirectoryLister& lister, ILibraryPackageDescriber& describer,
        IAlbumArtGenerator& album_art_generator);

    LibraryScanEngine(const LibraryScanEngine&) = delete;
    LibraryScanEngine(LibraryScanEngine&&) = delete;
    LibraryScanEngine& operator=(const LibraryScanEngine&) = delete;
    LibraryScanEngine& operator=(LibraryScanEngine&&) = delete;

    /*! \brief Destroys the scan engine. */
    ~LibraryScanEngine() = default;

    /*!
    \brief Plans a fresh scan of the given roots against the previously cached index.

    Lists every root through the lister, diffs the facts against \p prior_index with the pure
    planner, and seeds an empty working index that step() grows in plan order. Legal in any phase;
    it restarts the engine's scan state. A plan with no actions completes immediately.

    \param prior_index The last cached index, consumed for its reusable entries and change detection.
    \param scan_roots Directories to scan for packages.
    */
    void begin(LibraryIndex prior_index, std::span<const std::filesystem::path> scan_roots);

    /*!
    \brief Applies the next planned action, or stops promptly when cancellation is requested.

    Checks \p token before doing work: a requested cancellation moves the engine to Cancelled and
    offers a final commit checkpoint so the partial index can be persisted. Otherwise it applies one
    action — describing and generating album art for an added or rescanned package, reusing a cached entry, or
    dropping a removed one — and advances progress. No-ops once the scan is done.

    \param token Cooperative cancellation handle polled at the between-package checkpoint.
    \return The phase, progress, and commit signal after this step.
    */
    [[nodiscard]] LibraryScanStep step(const common::core::CancellationToken& token);

    /*!
    \brief Reports whether the scan has finished.
    \return True once the scan has completed or been cancelled.
    */
    [[nodiscard]] bool done() const noexcept;

    /*!
    \brief The current scan phase.
    \return The phase after the most recent step.
    */
    [[nodiscard]] LibraryScanPhase phase() const noexcept;

    /*!
    \brief Progress after the most recent step.
    \return The completed and total action counts and the current package.
    */
    [[nodiscard]] LibraryScanProgress progress() const noexcept;

    /*!
    \brief The working index built so far; final once the scan completes.

    Valid and loadable at every step: partial mid-scan, complete at the end. This is the value the
    runner persists on a commit checkpoint.

    \return The current working index.
    */
    [[nodiscard]] const LibraryIndex& index() const noexcept;

private:
    // Applies one Add/Rescan by describing and generating album art for the package, then
    // appending its entry.
    void describeAndAppend(const std::filesystem::path& package_path);

    // Scan ports; the engine drives them but never owns their implementations.
    ILibraryDirectoryLister& m_lister;
    ILibraryPackageDescriber& m_describer;
    IAlbumArtGenerator& m_album_art_generator;

    // Current scan phase; every transition happens on the pumping thread.
    LibraryScanPhase m_phase{LibraryScanPhase::Idle};

    // Planned actions in deterministic path order, and the next one to apply.
    std::vector<LibraryScanAction> m_actions;
    std::size_t m_next_action{0};

    // File facts for the current on-disk packages, keyed by path, for Add/Rescan entry building.
    std::map<std::filesystem::path, PackageFileFacts> m_facts_by_path;

    // Prior entries kept for Reuse actions, keyed by path.
    std::map<std::filesystem::path, LibraryEntry> m_prior_by_path;

    // The index grown in plan order; partial mid-scan, final at completion.
    LibraryIndex m_working_index;

    // Package processed by the most recent step, surfaced through progress().
    std::filesystem::path m_current_package;
};

} // namespace rock_hero::game::core
