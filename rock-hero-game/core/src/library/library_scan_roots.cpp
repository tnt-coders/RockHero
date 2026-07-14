#include "library/library_scan_roots.h"

#include <set>

namespace rock_hero::game::core
{

std::vector<std::filesystem::path> resolveLibraryScanRoots(
    const std::filesystem::path& app_data_directory,
    std::span<const std::filesystem::path> custom_roots)
{
    std::vector<std::filesystem::path> roots;
    roots.reserve(custom_roots.size() + 1);

    // Dedup by the normalized path so a custom root that repeats the default (or an earlier custom)
    // with redundant `.`/`..` or a different separator is not scanned twice; path comparison treats
    // `/` and `\` as equal, and the normalized path key stays lossless (a narrowed string would drop
    // characters outside the Windows code page). The original path is preserved in the output for
    // the lister. Case differences on Windows are not folded here — plan 10's package identity hash
    // is the durable cross-spelling dedup.
    std::set<std::filesystem::path> seen;
    const auto add = [&roots, &seen](const std::filesystem::path& root) {
        if (seen.insert(root.lexically_normal()).second)
        {
            roots.push_back(root);
        }
    };

    add(app_data_directory / g_default_songs_folder_name);
    for (const std::filesystem::path& custom : custom_roots)
    {
        add(custom);
    }
    return roots;
}

} // namespace rock_hero::game::core
