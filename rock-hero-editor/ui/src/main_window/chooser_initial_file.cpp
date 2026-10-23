#include "main_window/chooser_initial_file.h"

#include <rock_hero/common/core/shared/juce_path.h>

namespace rock_hero::editor::ui
{

// The home is the one starting directory a suggestion without its own can fall back to.
juce::File chooserInitialFile(const std::filesystem::path& suggested_file)
{
    const juce::File home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    if (suggested_file.empty())
    {
        return home;
    }
    if (!suggested_file.has_parent_path())
    {
        return home.getChildFile(common::core::juceStringFromPath(suggested_file));
    }
    return common::core::juceFileFromPath(suggested_file);
}

} // namespace rock_hero::editor::ui
