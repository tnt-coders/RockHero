#include "library/library_entry_projection.h"

namespace rock_hero::game::core
{

LibraryEntry makeLibraryEntry(
    const PackageFileFacts& facts,
    const std::expected<common::core::PackageDescription, common::core::SongPackageError>&
        description,
    const std::string& image_file_name)
{
    LibraryEntry entry;
    entry.package_path = facts.path;
    entry.file_size_bytes = facts.file_size_bytes;
    entry.modification_time_milliseconds = facts.modification_time_milliseconds;
    entry.album_art_file_name = image_file_name;

    // A hard read failure still yields an entry: it keeps its identity facts so the next scan can
    // detect a fix, and it carries the typed diagnostic as a warning instead of vanishing.
    if (!description.has_value())
    {
        entry.warnings.push_back("package could not be read: " + description.error().message);
        return entry;
    }

    const common::core::PackageDescription& package = *description;
    entry.metadata = package.metadata;
    entry.warnings = package.warnings;
    entry.arrangements.reserve(package.arrangements.size());
    for (const common::core::ArrangementDescription& arrangement : package.arrangements)
    {
        entry.arrangements.push_back(
            LibraryArrangementSummary{
                .id = arrangement.id,
                .part = arrangement.part,
                .tuning = arrangement.tuning,
                // Intensity stays the "Unknown" bucket until plan 11 ships a difficulty calculator.
                .intensity = std::nullopt,
            });
    }
    return entry;
}

} // namespace rock_hero::game::core
