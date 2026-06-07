#include "plugin_catalog_workflow.h"

#include <algorithm>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

void sortPluginCatalog(std::vector<common::audio::PluginCandidate>& plugin_candidates)
{
    std::ranges::sort(
        plugin_candidates,
        [](const common::audio::PluginCandidate& lhs, const common::audio::PluginCandidate& rhs) {
            if (lhs.name != rhs.name)
            {
                return lhs.name < rhs.name;
            }
            if (lhs.manufacturer != rhs.manufacturer)
            {
                return lhs.manufacturer < rhs.manufacturer;
            }
            return lhs.id < rhs.id;
        });
}

[[nodiscard]] PluginCandidateViewState makePluginCandidateViewState(
    const common::audio::PluginCandidate& plugin_candidate,
    const PluginDisplayTypeOverrides& display_type_overrides)
{
    PluginDisplayClassification classification = classifyPluginDisplay(
        PluginDisplayMetadata{
            .id = plugin_candidate.id,
            .name = plugin_candidate.name,
            .manufacturer = plugin_candidate.manufacturer,
            .format_name = plugin_candidate.format_name,
            .category = plugin_candidate.category,
        },
        display_type_overrides);
    return PluginCandidateViewState{
        .id = plugin_candidate.id,
        .name = plugin_candidate.name,
        .manufacturer = plugin_candidate.manufacturer,
        .format_name = plugin_candidate.format_name,
        .primary_display_type = classification.primary_type,
        .filter_display_types = std::move(classification.filter_types),
    };
}

[[nodiscard]] std::vector<PluginCandidateViewState> makePluginCandidateViewStates(
    const std::vector<common::audio::PluginCandidate>& plugin_candidates,
    const PluginDisplayTypeOverrides& display_type_overrides)
{
    std::vector<PluginCandidateViewState> states;
    states.reserve(plugin_candidates.size());
    for (const common::audio::PluginCandidate& plugin_candidate : plugin_candidates)
    {
        states.push_back(makePluginCandidateViewState(plugin_candidate, display_type_overrides));
    }
    return states;
}

} // namespace

// Stores display type overrides once so every browser projection uses the same config snapshot.
PluginCatalogWorkflow::PluginCatalogWorkflow(PluginDisplayTypeOverrides display_type_overrides)
    : m_display_type_overrides(std::move(display_type_overrides))
{}

void PluginCatalogWorkflow::open(std::vector<common::audio::PluginCandidate> candidates)
{
    replaceCatalog(std::move(candidates));
    m_visible = true;
}

void PluginCatalogWorkflow::replaceCatalog(std::vector<common::audio::PluginCandidate> candidates)
{
    sortPluginCatalog(candidates);
    m_candidates = std::move(candidates);
}

bool PluginCatalogWorkflow::close() noexcept
{
    if (!m_visible)
    {
        return false;
    }

    m_visible = false;
    return true;
}

void PluginCatalogWorkflow::hide() noexcept
{
    m_visible = false;
}

bool PluginCatalogWorkflow::hasCandidates() const noexcept
{
    return !m_candidates.empty();
}

std::optional<common::audio::PluginCandidate> PluginCatalogWorkflow::candidateForId(
    std::string_view plugin_id) const
{
    const auto found =
        std::ranges::find_if(m_candidates, [plugin_id](const auto& plugin_candidate) {
            return plugin_candidate.id == plugin_id;
        });
    if (found == m_candidates.end())
    {
        return std::nullopt;
    }

    return *found;
}

PluginBrowserViewState PluginCatalogWorkflow::viewState(bool scan_enabled, bool add_enabled) const
{
    return PluginBrowserViewState{
        .visible = m_visible,
        .scan_enabled = scan_enabled,
        .add_enabled = add_enabled,
        .plugins = makePluginCandidateViewStates(m_candidates, m_display_type_overrides),
    };
}

} // namespace rock_hero::editor::core
