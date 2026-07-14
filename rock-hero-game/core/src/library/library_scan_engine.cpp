#include "library/library_scan_engine.h"

#include "library/library_entry_projection.h"

#include <optional>
#include <string>
#include <utility>

namespace rock_hero::game::core
{

LibraryScanEngine::LibraryScanEngine(
    ILibraryDirectoryLister& lister, ILibraryPackageDescriber& describer,
    IAlbumArtGenerator& album_art_generator)
    : m_lister(lister)
    , m_describer(describer)
    , m_album_art_generator(album_art_generator)
{}

void LibraryScanEngine::begin(
    LibraryIndex prior_index, const std::span<const std::filesystem::path> scan_roots)
{
    // List every root; a bad root returns an empty list, so the scan spans the readable ones.
    std::vector<PackageFileFacts> current_files;
    for (const std::filesystem::path& scan_root : scan_roots)
    {
        std::vector<PackageFileFacts> root_files = m_lister.listPackages(scan_root);
        current_files.insert(
            current_files.end(),
            std::make_move_iterator(root_files.begin()),
            std::make_move_iterator(root_files.end()));
    }

    m_facts_by_path.clear();
    for (const PackageFileFacts& facts : current_files)
    {
        m_facts_by_path[facts.path] = facts;
    }

    m_actions = planLibraryScan(prior_index, current_files);

    // Keep the prior entries so Reuse actions restore them without re-describing.
    m_prior_by_path.clear();
    for (LibraryEntry& entry : prior_index.entries)
    {
        // Copy the key before moving the value: MSVC evaluates the subscript and the assigned
        // value right-to-left, so keying on entry.package_path inline would read it after the move.
        std::filesystem::path key = entry.package_path;
        m_prior_by_path[std::move(key)] = std::move(entry);
    }

    m_next_action = 0;
    m_working_index.entries.clear();
    m_working_index.entries.reserve(m_actions.size());
    m_current_package.clear();
    m_phase = m_actions.empty() ? LibraryScanPhase::Complete : LibraryScanPhase::Scanning;
}

LibraryScanStep LibraryScanEngine::step(const common::core::CancellationToken& token)
{
    // Idle, Complete, and Cancelled are all safe no-ops; only Scanning has an action to apply.
    if (m_phase != LibraryScanPhase::Scanning)
    {
        return LibraryScanStep{
            .phase = m_phase, .progress = progress(), .commit_checkpoint = false
        };
    }

    // Cancellation is polled at the between-package checkpoint: the in-flight package (if any) has
    // already been appended by the previous step, so the partial index is durable here.
    if (token.isCancelled())
    {
        m_phase = LibraryScanPhase::Cancelled;
        return LibraryScanStep{.phase = m_phase, .progress = progress(), .commit_checkpoint = true};
    }

    const LibraryScanAction& action = m_actions[m_next_action];
    m_current_package = action.package_path;
    switch (action.kind)
    {
        case LibraryScanActionKind::Add:
        case LibraryScanActionKind::Rescan:
            describeAndAppend(action.package_path);
            break;
        case LibraryScanActionKind::Reuse:
            if (const auto prior = m_prior_by_path.find(action.package_path);
                prior != m_prior_by_path.end())
            {
                m_working_index.entries.push_back(prior->second);
            }
            break;
        case LibraryScanActionKind::Remove:
            // The removed package is simply not carried into the freshly built working index.
            break;
    }

    ++m_next_action;
    if (m_next_action == m_actions.size())
    {
        m_phase = LibraryScanPhase::Complete;
    }
    return LibraryScanStep{.phase = m_phase, .progress = progress(), .commit_checkpoint = true};
}

void LibraryScanEngine::describeAndAppend(const std::filesystem::path& package_path)
{
    // Guaranteed present: Add/Rescan actions only arise for paths the lister reported this scan.
    const PackageFileFacts& facts = m_facts_by_path.at(package_path);
    const auto description = m_describer.describe(package_path);

    std::string image_file_name;
    std::optional<std::string> album_art_warning;
    if (description.has_value())
    {
        if (auto album_art = m_album_art_generator.generate(package_path); album_art.has_value())
        {
            image_file_name = std::move(album_art->image_file_name);
        }
        else
        {
            album_art_warning = std::move(album_art.error().message);
        }
    }

    LibraryEntry entry = makeLibraryEntry(facts, description, image_file_name);
    if (album_art_warning.has_value())
    {
        entry.warnings.push_back("album art: " + *album_art_warning);
    }
    m_working_index.entries.push_back(std::move(entry));
}

bool LibraryScanEngine::done() const noexcept
{
    return m_phase == LibraryScanPhase::Cancelled || m_phase == LibraryScanPhase::Complete;
}

LibraryScanPhase LibraryScanEngine::phase() const noexcept
{
    return m_phase;
}

LibraryScanProgress LibraryScanEngine::progress() const noexcept
{
    return LibraryScanProgress{
        .completed_actions = m_next_action,
        .total_actions = m_actions.size(),
        .current_package = m_current_package,
    };
}

const LibraryIndex& LibraryScanEngine::index() const noexcept
{
    return m_working_index;
}

} // namespace rock_hero::game::core
