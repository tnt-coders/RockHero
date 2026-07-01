#include "plugin_browser_window.h"

#include "busy_overlay.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_window_width{760};
constexpr int g_window_height{500};
constexpr int g_inset{10};
constexpr int g_gap{8};
constexpr int g_top_row_height{28};
constexpr int g_bottom_row_height{30};
constexpr int g_button_width{92};
constexpr int g_close_button_width{72};
constexpr int g_type_filter_width{150};
constexpr int g_row_height{28};
constexpr int g_list_header_height{24};
constexpr int g_all_types_filter_id{1};
constexpr int g_first_type_filter_id{2};
constexpr auto* g_all_types_label = "All types";
const juce::Colour g_background_color{juce::Colours::darkgrey.darker(0.28f)};
const juce::Colour g_header_color{juce::Colours::darkgrey.darker(0.4f)};
const juce::Colour g_selected_row_color{juce::Colour{0xff2f6f96}};
const juce::Colour g_row_color{juce::Colours::darkgrey.darker(0.16f)};
const juce::Colour g_alternate_row_color{juce::Colours::darkgrey.darker(0.1f)};
const juce::Colour g_column_header_color{juce::Colours::darkgrey.darker(0.34f)};

// Normalizes text for the lightweight browser filter.
[[nodiscard]] std::string lowerText(std::string text)
{
    std::ranges::transform(text, text.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return text;
}

// Builds the count label shown under the list.
[[nodiscard]] juce::String pluginCountText(std::size_t count)
{
    return juce::String{static_cast<int>(count)} + (count == 1 ? " plugin" : " plugins");
}

// Returns the fallback text used when the scanner omits a display field.
[[nodiscard]] juce::String fallbackText(const std::string& value, const char* fallback)
{
    return value.empty() ? juce::String{fallback} : juce::String{value};
}

// Appends one display type once while preserving first-seen order.
void appendUniqueDisplayType(
    std::vector<core::PluginDisplayType>& values, core::PluginDisplayType value)
{
    if (std::ranges::find(values, value) != values.end())
    {
        return;
    }

    values.push_back(value);
}

// Appends every display type that should include this plugin in the browser type filter.
void appendBrowserFilterTypesFor(
    std::vector<core::PluginDisplayType>& values, const core::PluginCandidateViewState& plugin)
{
    for (const core::PluginDisplayType display_type : plugin.scanned_display_types)
    {
        appendUniqueDisplayType(values, display_type);
    }

    if (plugin.primary_display_type != core::PluginDisplayType::Uncategorized ||
        plugin.scanned_display_types.empty())
    {
        appendUniqueDisplayType(values, plugin.primary_display_type);
    }
}

// Reports whether one plugin belongs in the selected browser filter bucket.
[[nodiscard]] bool pluginMatchesBrowserTypeFilter(
    const core::PluginCandidateViewState& plugin, core::PluginDisplayType display_type)
{
    if (std::ranges::find(plugin.scanned_display_types, display_type) !=
        plugin.scanned_display_types.end())
    {
        return true;
    }

    if (plugin.primary_display_type != core::PluginDisplayType::Uncategorized)
    {
        return plugin.primary_display_type == display_type;
    }

    return plugin.scanned_display_types.empty() &&
           display_type == core::PluginDisplayType::Uncategorized;
}

// Computes the browser's painted table columns so the header and rows stay aligned.
[[nodiscard]] std::tuple<int, int, int> pluginBrowserColumnWidths(int available_width)
{
    const int name_width = std::max(160, available_width * 48 / 100);
    const int manufacturer_width = std::max(120, available_width * 34 / 100);
    constexpr int format_width = 58;
    return {name_width, manufacturer_width, format_width};
}

// Header component owned by the ListBox so column labels scroll and resize with the list content.
class PluginBrowserHeader final : public juce::Component
{
public:
    // Gives ListBox a stable header height while letting it resize the width.
    PluginBrowserHeader()
    {
        setComponentID("plugin_browser_list_header");
        setSize(0, g_list_header_height);
    }

    // Paints compact column labels that match paintListBoxItem().
    void paint(juce::Graphics& g) override
    {
        g.fillAll(g_column_header_color);
        auto area = getLocalBounds().reduced(8, 0);
        const auto [name_width, manufacturer_width, format_width] =
            pluginBrowserColumnWidths(area.getWidth());

        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions{12.0f, juce::Font::bold});
        drawHeaderText(g, "Name", area.removeFromLeft(std::min(name_width, area.getWidth())));
        area.removeFromLeft(std::min(g_gap, area.getWidth()));
        drawHeaderText(
            g, "Manufacturer", area.removeFromLeft(std::min(manufacturer_width, area.getWidth())));
        area.removeFromLeft(std::min(g_gap, area.getWidth()));
        drawHeaderText(g, "Format", area.removeFromLeft(std::min(format_width, area.getWidth())));
    }

private:
    // Draws one clipped header cell.
    static void drawHeaderText(
        juce::Graphics& g, const juce::String& text, const juce::Rectangle<int>& area)
    {
        g.drawFittedText(text, area, juce::Justification::centredLeft, 1);
    }
};

// Builds cached lowercase search text for one plugin row.
[[nodiscard]] std::string pluginFilterText(const core::PluginCandidateViewState& plugin)
{
    std::string text;
    text.reserve(plugin.name.size() + plugin.manufacturer.size() + plugin.format_name.size() + 2);
    text.append(plugin.name);
    text.push_back(' ');
    text.append(plugin.manufacturer);
    text.push_back(' ');
    text.append(plugin.format_name);
    return lowerText(std::move(text));
}

} // namespace

// Dense browser content used by the top-level plugin browser window.
class PluginBrowserWindow::Content final : public juce::Component, private juce::ListBoxModel
{
public:
    // Creates the search, list, and command controls.
    explicit Content(PluginBrowserWindow::Listener& listener)
        : m_listener(listener)
        , m_list_box("plugin_browser_list", this)
    {
        setComponentID("plugin_browser_content");
        m_filter_editor.setComponentID("plugin_browser_filter");
        m_filter_editor.setTextToShowWhenEmpty("Search plugins", juce::Colours::lightgrey);
        m_filter_editor.onTextChange = [this] {
            rebuildFilteredIndicesPreservingSelection();
            updateControls();
        };

        m_type_filter_combo.setComponentID("plugin_browser_type_filter");
        m_type_filter_combo.addItem(g_all_types_label, g_all_types_filter_id);
        m_type_filter_combo.setSelectedId(g_all_types_filter_id, juce::dontSendNotification);
        m_type_filter_combo.setTextWhenNothingSelected(g_all_types_label);
        m_type_filter_combo.setTextWhenNoChoicesAvailable(g_all_types_label);
        m_type_filter_combo.onChange = [this] {
            updateSelectedTypeFilter();
            rebuildFilteredIndicesPreservingSelection();
            updateControls();
        };

        m_rescan_button.setComponentID("plugin_browser_rescan_button");
        m_rescan_button.setButtonText("Rescan");
        m_rescan_button.onClick = [this] { m_listener.onPluginBrowserScanRequested(); };

        m_close_button.setComponentID("plugin_browser_close_button");
        m_close_button.setButtonText("Close");
        m_close_button.onClick = [this] { m_listener.onPluginBrowserClosed(); };

        m_add_button.setComponentID("plugin_browser_add_button");
        m_add_button.setButtonText("Add");
        m_add_button.onClick = [this] { addSelectedPlugin(); };

        m_count_label.setComponentID("plugin_browser_count_label");
        m_count_label.setJustificationType(juce::Justification::centredLeft);
        m_count_label.setColour(juce::Label::textColourId, juce::Colours::white);

        m_list_box.setComponentID("plugin_browser_list");
        m_list_box.setRowHeight(g_row_height);
        m_list_box.setMultipleSelectionEnabled(false);
        m_list_box.setClickingTogglesRowSelection(false);
        m_list_box.setColour(juce::ListBox::backgroundColourId, g_background_color);
        m_list_box.setHeaderComponent(std::make_unique<PluginBrowserHeader>());

        addAndMakeVisible(m_filter_editor);
        addAndMakeVisible(m_type_filter_combo);
        addAndMakeVisible(m_rescan_button);
        addAndMakeVisible(m_close_button);
        addAndMakeVisible(m_list_box);
        addAndMakeVisible(m_count_label);
        addAndMakeVisible(m_add_button);
        m_busy_overlay.setComponentID("plugin_browser_busy_overlay");
        m_busy_overlay.setCancelCallback(
            [this] { m_listener.onPluginBrowserBusyCancelRequested(); });
        addChildComponent(m_busy_overlay);
        setState(core::PluginBrowserViewState{});
    }

    // Applies controller-derived catalog state and preserves selection by plugin ID.
    void setState(const core::PluginBrowserViewState& state)
    {
        const std::string previous_selection = selectedPluginId();
        m_state = state;
        rebuildTypeFilterOptions();
        rebuildFilterTextCache();
        rebuildFilteredIndices();
        selectPluginId(previous_selection);
        updateControls();
        repaint();
    }

    // Applies editor-wide busy state over the browser content.
    void setBusyState(const std::optional<core::BusyViewState>& busy)
    {
        m_busy_overlay.setBusyState(busy);
    }

    // Draws the plain browser background.
    void paint(juce::Graphics& g) override
    {
        g.fillAll(g_background_color);
    }

    // Keeps controls in a compact file-browser style layout.
    void resized() override
    {
        auto area = getLocalBounds().reduced(g_inset);
        auto top_row = area.removeFromTop(g_top_row_height);
        m_close_button.setBounds(top_row.removeFromRight(g_close_button_width));
        top_row.removeFromRight(g_gap);
        m_rescan_button.setBounds(top_row.removeFromRight(g_button_width));
        top_row.removeFromRight(g_gap);
        m_type_filter_combo.setBounds(top_row.removeFromRight(g_type_filter_width));
        top_row.removeFromRight(g_gap);
        m_filter_editor.setBounds(top_row);

        area.removeFromTop(g_gap);
        auto bottom_row = area.removeFromBottom(g_bottom_row_height);
        area.removeFromBottom(g_gap);
        m_add_button.setBounds(bottom_row.removeFromRight(g_button_width));
        bottom_row.removeFromRight(g_gap);
        m_count_label.setBounds(bottom_row);
        m_list_box.setBounds(area);
        m_busy_overlay.setBounds(getLocalBounds());
    }

private:
    // ListBoxModel implementation: reports filtered row count.
    int getNumRows() override
    {
        return static_cast<int>(m_filtered_indices.size());
    }

    // ListBoxModel implementation: draws one plugin row with stable columns.
    void paintListBoxItem(
        int row_number, juce::Graphics& g, int width, int height, bool row_is_selected) override
    {
        const core::PluginCandidateViewState* const plugin = pluginAtRow(row_number);
        if (plugin == nullptr)
        {
            return;
        }

        g.setColour(
            row_is_selected ? g_selected_row_color
                            : (row_number % 2 == 0 ? g_row_color : g_alternate_row_color));
        g.fillRect(0, 0, width, height);

        auto area = juce::Rectangle<int>{0, 0, width, height}.reduced(8, 0);
        const auto [name_width, manufacturer_width, format_width] =
            pluginBrowserColumnWidths(area.getWidth());

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions{14.0f});
        g.drawFittedText(
            fallbackText(plugin->name, "Unnamed Plugin"),
            area.removeFromLeft(std::min(name_width, area.getWidth())),
            juce::Justification::centredLeft,
            1);

        area.removeFromLeft(std::min(g_gap, area.getWidth()));
        g.setColour(juce::Colours::lightgrey);
        g.drawFittedText(
            fallbackText(plugin->manufacturer, "Unknown"),
            area.removeFromLeft(std::min(manufacturer_width, area.getWidth())),
            juce::Justification::centredLeft,
            1);

        area.removeFromLeft(std::min(g_gap, area.getWidth()));
        g.drawFittedText(
            fallbackText(plugin->format_name, "Plugin"),
            area.removeFromLeft(std::min(format_width, area.getWidth())),
            juce::Justification::centredLeft,
            1);
    }

    // ListBoxModel implementation: refreshes Add availability when selection changes.
    void selectedRowsChanged(int /*last_row_selected*/) override
    {
        updateControls();
    }

    // ListBoxModel implementation: double-clicking adds the row's plugin.
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent& /*event*/) override
    {
        if (const auto* const plugin = pluginAtRow(row); plugin != nullptr)
        {
            m_listener.onPluginBrowserAddRequested(plugin->id);
        }
    }

    // Returns the plugin represented by a filtered row, or null for invalid rows.
    [[nodiscard]] const core::PluginCandidateViewState* pluginAtRow(int row) const
    {
        if (row < 0 || static_cast<std::size_t>(row) >= m_filtered_indices.size())
        {
            return nullptr;
        }

        const std::size_t plugin_index = m_filtered_indices[static_cast<std::size_t>(row)];
        if (plugin_index >= m_state.plugins.size())
        {
            return nullptr;
        }

        return &m_state.plugins[plugin_index];
    }

    // Returns the currently selected plugin ID, or empty if the selection is invalid.
    [[nodiscard]] std::string selectedPluginId() const
    {
        const core::PluginCandidateViewState* const plugin =
            pluginAtRow(m_list_box.getSelectedRow());
        return plugin != nullptr ? plugin->id : std::string{};
    }

    // Selects a plugin by ID after filtering or state replacement.
    void selectPluginId(const std::string& plugin_id)
    {
        if (plugin_id.empty())
        {
            m_list_box.deselectAllRows();
            return;
        }

        for (std::size_t row = 0; row < m_filtered_indices.size(); ++row)
        {
            const std::size_t plugin_index = m_filtered_indices[row];
            if (plugin_index < m_state.plugins.size() &&
                m_state.plugins[plugin_index].id == plugin_id)
            {
                m_list_box.selectRow(static_cast<int>(row));
                return;
            }
        }

        m_list_box.deselectAllRows();
    }

    // Rebuilds the lowercase search text that stays parallel to m_state.plugins.
    void rebuildFilterTextCache()
    {
        m_filter_texts.clear();
        m_filter_texts.reserve(m_state.plugins.size());
        for (const core::PluginCandidateViewState& plugin : m_state.plugins)
        {
            m_filter_texts.push_back(pluginFilterText(plugin));
        }
    }

    // Rebuilds the type filter menu from core-derived display types while preserving selection.
    void rebuildTypeFilterOptions()
    {
        const std::optional<core::PluginDisplayType> previous_filter = m_selected_type_filter;
        m_type_filter_values.clear();
        for (const core::PluginCandidateViewState& plugin : m_state.plugins)
        {
            appendBrowserFilterTypesFor(m_type_filter_values, plugin);
        }

        std::ranges::sort(
            m_type_filter_values, [](core::PluginDisplayType lhs, core::PluginDisplayType rhs) {
                return core::pluginDisplayTypeLabel(lhs) < core::pluginDisplayTypeLabel(rhs);
            });

        m_type_filter_combo.clear(juce::dontSendNotification);
        m_type_filter_combo.addItem(g_all_types_label, g_all_types_filter_id);
        for (std::size_t index = 0; index < m_type_filter_values.size(); ++index)
        {
            m_type_filter_combo.addItem(
                juce::String{core::pluginDisplayTypeLabel(m_type_filter_values[index])},
                g_first_type_filter_id + static_cast<int>(index));
        }

        const auto selected_type = previous_filter.has_value()
                                       ? std::ranges::find(m_type_filter_values, *previous_filter)
                                       : m_type_filter_values.end();
        m_selected_type_filter =
            selected_type != m_type_filter_values.end() ? previous_filter : std::nullopt;
        m_type_filter_combo.setSelectedId(selectedTypeFilterId(), juce::dontSendNotification);
    }

    // Rebuilds the filtered row map from the search field and selected type.
    void rebuildFilteredIndices()
    {
        m_filtered_indices.clear();
        const std::string filter = lowerText(m_filter_editor.getText().toStdString());
        for (std::size_t index = 0; index < m_state.plugins.size(); ++index)
        {
            if (pluginMatchesTypeFilter(index) && pluginMatchesTextFilter(index, filter))
            {
                m_filtered_indices.push_back(index);
            }
        }

        m_list_box.updateContent();
    }

    // Rebuilds the filtered row map while keeping the same plugin selected when still visible.
    void rebuildFilteredIndicesPreservingSelection()
    {
        const std::string previous_selection = selectedPluginId();
        rebuildFilteredIndices();
        selectPluginId(previous_selection);
    }

    // Synchronizes the stored type filter with the selected combo-box item.
    void updateSelectedTypeFilter()
    {
        const int selected_id = m_type_filter_combo.getSelectedId();
        if (selected_id < g_first_type_filter_id)
        {
            m_selected_type_filter = std::nullopt;
            return;
        }

        const std::size_t type_index =
            static_cast<std::size_t>(selected_id - g_first_type_filter_id);
        m_selected_type_filter = type_index < m_type_filter_values.size()
                                     ? std::optional{m_type_filter_values[type_index]}
                                     : std::nullopt;
    }

    // Returns the combo-box item ID for the stored type filter.
    [[nodiscard]] int selectedTypeFilterId() const
    {
        if (!m_selected_type_filter.has_value())
        {
            return g_all_types_filter_id;
        }

        const auto selected_type = std::ranges::find(m_type_filter_values, *m_selected_type_filter);
        if (selected_type == m_type_filter_values.end())
        {
            return g_all_types_filter_id;
        }

        const auto type_index = static_cast<int>(
            static_cast<std::size_t>(selected_type - m_type_filter_values.begin()));
        return g_first_type_filter_id + type_index;
    }

    // Returns whether one plugin matches the selected type filter.
    [[nodiscard]] bool pluginMatchesTypeFilter(std::size_t plugin_index) const
    {
        if (!m_selected_type_filter.has_value())
        {
            return true;
        }

        if (plugin_index >= m_state.plugins.size())
        {
            return false;
        }

        return pluginMatchesBrowserTypeFilter(
            m_state.plugins[plugin_index], *m_selected_type_filter);
    }

    // Returns whether one plugin matches the current lowercase text filter.
    [[nodiscard]] bool pluginMatchesTextFilter(
        std::size_t plugin_index, const std::string& filter) const
    {
        if (filter.empty())
        {
            return true;
        }

        return plugin_index < m_filter_texts.size() &&
               m_filter_texts[plugin_index].find(filter) != std::string::npos;
    }

    // Adds the currently selected plugin if both state and selection allow it.
    void addSelectedPlugin()
    {
        if (!m_state.add_enabled)
        {
            return;
        }

        const std::string plugin_id = selectedPluginId();
        if (!plugin_id.empty())
        {
            m_listener.onPluginBrowserAddRequested(plugin_id);
        }
    }

    // Applies enabledness and count text derived from state plus current selection.
    void updateControls()
    {
        m_rescan_button.setEnabled(m_state.scan_enabled);
        m_type_filter_combo.setEnabled(!m_state.plugins.empty());
        m_add_button.setEnabled(m_state.add_enabled && !selectedPluginId().empty());
        m_count_label.setText(
            pluginCountText(m_filtered_indices.size()), juce::dontSendNotification);
    }

    // Listener that receives the content's browser intents.
    PluginBrowserWindow::Listener& m_listener;

    // Last controller-derived browser state.
    core::PluginBrowserViewState m_state{};

    // Search field used only for presentation-side filtering.
    juce::TextEditor m_filter_editor;

    // Menu that filters visible plugins by core-derived display type.
    juce::ComboBox m_type_filter_combo;

    // Button that requests a catalog rescan.
    juce::TextButton m_rescan_button;

    // Button that requests closing the browser window.
    juce::TextButton m_close_button;

    // List displaying filtered plugins.
    juce::ListBox m_list_box;

    // Label showing the current filtered count.
    juce::Label m_count_label;

    // Button that adds the selected plugin.
    juce::TextButton m_add_button;

    // Overlay that blocks plugin browser interactions while editor-wide busy work is active.
    BusyOverlay m_busy_overlay;

    // Plugin indices that pass the current filter.
    std::vector<std::size_t> m_filtered_indices;

    // Lowercase search text parallel to m_state.plugins, rebuilt when state changes.
    std::vector<std::string> m_filter_texts;

    // Display types currently available in the combo box, excluding the all-types item.
    std::vector<core::PluginDisplayType> m_type_filter_values;

    // Empty means the all-types item is selected.
    std::optional<core::PluginDisplayType> m_selected_type_filter;
};

// Creates a native top-level browser window with owned content.
PluginBrowserWindow::PluginBrowserWindow(Listener& listener)
    : juce::DocumentWindow(
          "Add Plugin", g_header_color,
          juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
    , m_listener(listener)
    , m_content(std::make_unique<Content>(listener))
{
    setComponentID("plugin_browser_window");
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setContentNonOwned(m_content.get(), false);
    setResizeLimits(520, 340, 1400, 900);
    centreWithSize(g_window_width, g_window_height);
}

PluginBrowserWindow::~PluginBrowserWindow()
{
    clearContentComponent();
}

// Applies state to the owned content component.
void PluginBrowserWindow::setState(const core::PluginBrowserViewState& state)
{
    m_content->setState(state);
}

// Applies editor-wide busy state to the browser content overlay.
void PluginBrowserWindow::setBusyState(const std::optional<core::BusyViewState>& busy)
{
    m_content->setBusyState(busy);
}

// Forwards native close-button clicks to the controller-owned state machine.
void PluginBrowserWindow::closeButtonPressed()
{
    m_listener.onPluginBrowserClosed();
}

} // namespace rock_hero::editor::ui
