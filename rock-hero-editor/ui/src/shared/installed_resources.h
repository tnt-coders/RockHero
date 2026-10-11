/*!
\file installed_resources.h
\brief Where the build deploys the editor's resources tree.
*/

#pragma once

#include <juce_core/juce_core.h>

namespace rock_hero::editor::ui
{

/*!
\brief Returns the resources tree the build deploys beside the editor executable.

The 3D preview reads its compiled shaders from it, and the editor loads its built-in highway themes
from it; the game deploys the same layout beside its own executable.

\return The deployed resources directory.
*/
[[nodiscard]] juce::File installedResourcesRoot();

} // namespace rock_hero::editor::ui
