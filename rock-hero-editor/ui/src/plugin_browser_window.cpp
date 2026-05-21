#include "plugin_browser_window.h"

#include <algorithm>
#include <cctype>
#include <string>
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
constexpr int g_row_height{28};
const juce::Colour g_background_colour{juce::Colours::darkgrey.darker(0.28f)};
const juce::Colour g_header_colour{juce::Colours::darkgrey.darker(0.4f)};
const juce::Colour g_selected_row_colour{juce::Colour{0xff2f6f96}};
const juce::Colour g_row_colour{juce::Colours::darkgrey.darker(0.16f)};
const juce::Colour g_alternate_row_colour{juce::Colours::darkgrey.darker(0.1f)};

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
            rebuildFilteredIndices();
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
        m_list_box.setColour(juce::ListBox::backgroundColourId, g_background_colour);

        addAndMakeVisible(m_filter_editor);
        addAndMakeVisible(m_rescan_button);
        addAndMakeVisible(m_close_button);
        addAndMakeVisible(m_list_box);
        addAndMakeVisible(m_count_label);
        addAndMakeVisible(m_add_button);
        setState(core::PluginBrowserViewState{});
    }

    // Applies controller-derived catalog state and preserves selection by plugin ID.
    void setState(const core::PluginBrowserViewState& state)
    {
        const std::string previous_selection = selectedPluginId();
        m_state = state;
        rebuildFilteredIndices();
        selectPluginId(previous_selection);
        updateControls();
        repaint();
    }

    // Draws the plain browser background.
    void paint(juce::Graphics& g) override
    {
        g.fillAll(g_background_colour);
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
        m_filter_editor.setBounds(top_row);

        area.removeFromTop(g_gap);
        auto bottom_row = area.removeFromBottom(g_bottom_row_height);
        area.removeFromBottom(g_gap);
        m_add_button.setBounds(bottom_row.removeFromRight(g_button_width));
        bottom_row.removeFromRight(g_gap);
        m_count_label.setBounds(bottom_row);
        m_list_box.setBounds(area);
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
        const core::PluginCandidateState* const plugin = pluginAtRow(row_number);
        if (plugin == nullptr)
        {
            return;
        }

        g.setColour(
            row_is_selected ? g_selected_row_colour
                            : (row_number % 2 == 0 ? g_row_colour : g_alternate_row_colour));
        g.fillRect(0, 0, width, height);

        auto area = juce::Rectangle<int>{0, 0, width, height}.reduced(8, 0);
        const int name_width = std::max(120, area.getWidth() * 32 / 100);
        const int manufacturer_width = std::max(110, area.getWidth() * 24 / 100);
        const int format_width = 58;

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

        area.removeFromLeft(std::min(g_gap, area.getWidth()));
        g.setColour(juce::Colours::silver);
        g.drawFittedText(
            juce::String{plugin->file_path.string()}, area, juce::Justification::centredLeft, 1);
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
    [[nodiscard]] const core::PluginCandidateState* pluginAtRow(int row) const
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
        const core::PluginCandidateState* const plugin = pluginAtRow(m_list_box.getSelectedRow());
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

    // Rebuilds the filtered row map from the search field.
    void rebuildFilteredIndices()
    {
        m_filtered_indices.clear();
        const std::string filter = lowerText(m_filter_editor.getText().toStdString());
        for (std::size_t index = 0; index < m_state.plugins.size(); ++index)
        {
            if (pluginMatchesFilter(m_state.plugins[index], filter))
            {
                m_filtered_indices.push_back(index);
            }
        }

        m_list_box.updateContent();
    }

    // Returns whether one plugin matches the current lowercase filter.
    [[nodiscard]] bool pluginMatchesFilter(
        const core::PluginCandidateState& plugin, const std::string& filter) const
    {
        if (filter.empty())
        {
            return true;
        }

        const std::string haystack = lowerText(
            plugin.name + " " + plugin.manufacturer + " " + plugin.format_name + " " +
            plugin.file_path.string());
        return haystack.find(filter) != std::string::npos;
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

    // Plugin indices that pass the current filter.
    std::vector<std::size_t> m_filtered_indices;
};

// Creates a native top-level browser window with owned content.
PluginBrowserWindow::PluginBrowserWindow(Listener& listener)
    : juce::DocumentWindow(
          "Add Plugin", g_header_colour,
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

// Forwards native close-button clicks to the controller-owned state machine.
void PluginBrowserWindow::closeButtonPressed()
{
    m_listener.onPluginBrowserClosed();
}

} // namespace rock_hero::editor::ui
