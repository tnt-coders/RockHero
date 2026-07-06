#include "shared/editor_theme.h"

namespace rock_hero::editor::ui
{

// One shared default-built theme; swapping this instance (and repainting) is the future
// user-theme hook.
const EditorTheme& editorTheme() noexcept
{
    static const EditorTheme g_theme{};
    return g_theme;
}

} // namespace rock_hero::editor::ui
