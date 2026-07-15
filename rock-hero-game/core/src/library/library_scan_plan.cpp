#include "library/library_scan_plan.h"

#include <algorithm>
#include <map>

namespace rock_hero::game::core
{

std::vector<LibraryScanAction> planLibraryScan(
    const LibraryIndex& previous_index, const std::span<const PackageFileFacts> current_files)
{
    // Ordered maps make the plan deterministic regardless of input order; last-wins collapses
    // duplicate fact paths a careless lister might produce.
    //
    // The keys compare by path::operator< (normalizes separators, does NOT case-fold), while the
    // Windows filesystem is case-insensitive. The lister and the stored index must therefore agree
    // on one canonical spelling per package; if the same file is ever presented under different
    // casing across runs, this planner sees two keys and degrades a Reuse into Remove + Add (a
    // benign re-describe, never a crash). docs/plans/roadmap/10-format-versioning-and-chart-identity.md's
    // package identity hash is the designed real fix for stable identity across path spellings.
    std::map<std::filesystem::path, const PackageFileFacts*> facts_by_path;
    for (const PackageFileFacts& facts : current_files)
    {
        facts_by_path[facts.path] = &facts;
    }

    std::map<std::filesystem::path, const LibraryEntry*> entries_by_path;
    for (const LibraryEntry& entry : previous_index.entries)
    {
        entries_by_path[entry.package_path] = &entry;
    }

    std::vector<LibraryScanAction> actions;
    actions.reserve(facts_by_path.size() + entries_by_path.size());

    for (const auto& [path, facts] : facts_by_path)
    {
        const auto known = entries_by_path.find(path);
        if (known == entries_by_path.end())
        {
            actions.push_back({LibraryScanActionKind::Add, path});
            continue;
        }

        const bool unchanged =
            known->second->file_size_bytes == facts->file_size_bytes &&
            known->second->modification_time_milliseconds == facts->modification_time_milliseconds;
        actions.push_back(
            {unchanged ? LibraryScanActionKind::Reuse : LibraryScanActionKind::Rescan, path});
    }

    for (const auto& [path, entry] : entries_by_path)
    {
        if (!facts_by_path.contains(path))
        {
            actions.push_back({LibraryScanActionKind::Remove, path});
        }
    }

    // Each path appears exactly once, so a plain path sort fully determines the order.
    std::ranges::sort(
        actions, {}, [](const LibraryScanAction& action) -> const std::filesystem::path& {
            return action.package_path;
        });
    return actions;
}

} // namespace rock_hero::game::core
