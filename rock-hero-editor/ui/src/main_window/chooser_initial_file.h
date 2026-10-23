/*!
\file chooser_initial_file.h
\brief Turns a suggested destination into the file a save dialog opens on.
*/

#pragma once

#include <filesystem>
#include <juce_core/juce_core.h>

namespace rock_hero::editor::ui
{

/*!
\brief Names the file a save dialog opens on for a suggested destination.

A suggestion with a directory opens there; a bare file name, which the controller publishes for a
project that has never been saved, opens on that name in the user's home, never on a path JUCE
would resolve against the working directory; an empty suggestion opens on the home itself.

\param suggested_file Suggested destination: a full path, a bare file name, or empty.
\return The dialog's starting file.
*/
[[nodiscard]] juce::File chooserInitialFile(const std::filesystem::path& suggested_file);

} // namespace rock_hero::editor::ui
