#include "shared/installed_resources.h"

namespace rock_hero::editor::ui
{

// The deploy rule copies the tree next to the executable in the build and the install alike.
juce::File installedResourcesRoot()
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getChildFile("resources");
}

} // namespace rock_hero::editor::ui
