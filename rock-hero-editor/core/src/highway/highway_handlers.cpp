#include "controller/editor_controller_impl.h"
#include "shared/editor_controller_logging.h"

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Applies one change to the stored highway settings and returns the record to show. A change that
// cannot be saved still applies for this session, as the editor's other display preferences do,
// and the failure is logged.
[[nodiscard]] common::core::HighwayPlayerSettings changedHighwaySettings(
    common::core::HighwaySettingsStore& store, common::core::HighwayPlayerSettings shown,
    const std::function<void(common::core::HighwayPlayerSettings&)>& change,
    const std::string_view context)
{
    std::expected<common::core::HighwayPlayerSettings, common::core::HighwaySettingsError> stored =
        store.update(change);
    if (stored.has_value())
    {
        return std::move(*stored);
    }
    logEditorControllerBestEffortFailure(context, stored.error().message);
    change(shown);
    return shown;
}

} // namespace

// The pin is its own setting, so a theme switch leaves a pinned palette where it is and lets an
// unpinned one follow the new theme.
void EditorController::Impl::onHighwayThemeChangeRequested(std::string theme_id)
{
    m_highway_settings = changedHighwaySettings(
        m_highway_settings_store,
        m_highway_settings,
        [&theme_id](common::core::HighwayPlayerSettings& settings) {
            settings.theme_id = theme_id;
        },
        "save highway theme");
    updateView();
}

// A concrete palette pins; an empty id is "Theme's choice" and unpins.
void EditorController::Impl::onStringPaletteChangeRequested(
    std::optional<std::string> pinned_palette_id)
{
    m_highway_settings = changedHighwaySettings(
        m_highway_settings_store,
        m_highway_settings,
        [&pinned_palette_id](common::core::HighwayPlayerSettings& settings) {
            settings.pinned_palette_id = pinned_palette_id;
        },
        "save string palette pin");
    updateView();
}

} // namespace rock_hero::editor::core
