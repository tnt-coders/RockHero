/*!
\file preview_resources.h
\brief Loads the 3D preview's deployed shader and texture resources beside the editor executable.
*/

#pragma once

#include <optional>
#include <rock_hero/common/ui/highway/highway_renderer.h>
#include <vector>

namespace juce
{
class File;
} // namespace juce

namespace rock_hero::editor::ui
{

/*!
\brief Reads the shared highway programs' compiled binaries from the editor's resources tree.

The editor's half of the shared renderer's no-filesystem contract: the build deploys the
compiled shaders under resources/shaders/dx11 next to the executable (the same layout the game
uses), and this loads them into the shader-set seam.

\return The filled shader set, or empty when any binary is missing (logged per file).
*/
[[nodiscard]] std::optional<common::ui::HighwayShaderSet> loadPreviewHighwayShaders();

/*!
\brief Reads the shared highway texture assets from the editor's resources tree.

Best-effort at this layer: a missing asset leaves its member empty and the renderer's own
required-asset check decides whether that fails creation.

\return The texture set with every readable asset filled.
*/
[[nodiscard]] common::ui::HighwayTextureSet loadPreviewHighwayTextures();

/*!
\brief EXPERIMENT SCAFFOLDING — lists the staged note-atlas variants beside the shipped atlas.

Variants are files named `notes-variant-*.png` in the deployed textures directory, put there by
`.agents/atlas-variant.ps1 -Stage` so candidate atlases can be cycled in the app without a
rebuild or a script round trip per look. Sorted by filename for a stable cycling order. Deleted
with the atlas sampler once the look is signed.

\return The staged variant files, possibly empty.
*/
[[nodiscard]] std::vector<juce::File> listPreviewNoteAtlasVariants();

/*!
\brief EXPERIMENT SCAFFOLDING — the texture set with the note atlas read from a staged variant.

Loads exactly what loadPreviewHighwayTextures() loads, then substitutes the given staged file's
bytes for the note atlas, so a candidate is judged against otherwise-identical resources.

\param notes_variant The staged variant file to substitute for notes.png.
\return The texture set with the substitution applied.
*/
[[nodiscard]] common::ui::HighwayTextureSet loadPreviewHighwayTextures(
    const juce::File& notes_variant);

} // namespace rock_hero::editor::ui
