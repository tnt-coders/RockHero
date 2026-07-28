#include "library/filesystem_directory_lister.h"

#include <juce_core/juce_core.h>
#include <optional>
#include <rock_hero/common/core/shared/juce_path.h>

namespace rock_hero::game::core
{

std::optional<std::vector<PackageFileFacts>> FilesystemDirectoryLister::listPackages(
    const std::filesystem::path& scan_root)
{
    const juce::File root = common::core::juceFileFromPath(scan_root);
    if (!root.isDirectory())
    {
        // Absent or unreadable (offline share, permission denied): nullopt lets the scan engine
        // keep this root's prior entries instead of treating it as a newly empty directory.
        return std::nullopt;
    }

    std::vector<PackageFileFacts> facts;
    // Recurse so songs organized into subfolders are still found; juce::File enumeration reports
    // the ms-since-epoch modification time and byte size the planner diffs against.
    const juce::Array<juce::File> packages =
        root.findChildFiles(juce::File::findFiles, true, "*.rock");
    facts.reserve(static_cast<std::size_t>(packages.size()));
    for (const juce::File& package : packages)
    {
        facts.push_back(
            PackageFileFacts{
                .path = common::core::pathFromJuceFile(package),
                .file_size_bytes = package.getSize(),
                .modification_time_milliseconds =
                    package.getLastModificationTime().toMilliseconds(),
            });
    }
    return facts;
}

} // namespace rock_hero::game::core
