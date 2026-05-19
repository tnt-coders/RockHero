#include "signal_chain_panel.h"

#include <algorithm>
#include <string>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_panel_inset{8};
constexpr int g_add_button_width{112};
constexpr int g_add_button_height{28};
constexpr int g_header_height{34};
constexpr int g_plugin_row_height{24};
constexpr int g_plugin_row_gap{4};
constexpr int g_remove_button_width{72};
constexpr int g_row_button_gap{8};
const juce::Colour g_panel_background{juce::Colours::darkgrey.darker(0.24f)};
const juce::Colour g_panel_header_background{juce::Colours::darkgrey.darker(0.34f)};
const juce::Colour g_panel_border{juce::Colours::black.withAlpha(0.45f)};
const juce::Colour g_plugin_row_background{juce::Colours::darkgrey.darker(0.12f)};
const juce::Colour g_plugin_row_hover_background{juce::Colour{0xff263a4c}};
const juce::Colour g_plugin_row_border{juce::Colours::black.withAlpha(0.35f)};
const juce::Colour g_plugin_row_hover_border{juce::Colours::lightskyblue.withAlpha(0.95f)};
const juce::Colour g_plugin_row_hover_accent{juce::Colours::lightskyblue};

// Builds the compact slot label shown in the linear plugin chain.
[[nodiscard]] juce::String pluginLabel(const core::PluginViewState& plugin)
{
    juce::String label{std::to_string(plugin.chain_index + 1)};
    label += ". ";
    label += plugin.name.empty() ? juce::String{"Unnamed Plugin"} : juce::String{plugin.name};

    if (!plugin.manufacturer.empty())
    {
        label += " - ";
        label += juce::String{plugin.manufacturer};
    }

    if (!plugin.format_name.empty())
    {
        label += " (";
        label += juce::String{plugin.format_name};
        label += ")";
    }

    return label;
}

} // namespace

// Presents one plugin-chain row and emits a remove intent for its stored instance ID.
class SignalChainPanel::PluginRowView final : public juce::Component
{
public:
    // Creates the row with a stable plugin snapshot and the parent panel listener.
    PluginRowView(core::PluginViewState plugin, Listener& listener)
        : m_listener(listener)
        , m_plugin(std::move(plugin))
    {
        setComponentID(juce::String{"plugin_row_"} + juce::String{m_plugin.instance_id});
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        m_remove_button.setComponentID(
            juce::String{"remove_plugin_button_"} + juce::String{m_plugin.instance_id});
        m_remove_button.setButtonText("Remove");
        m_remove_button.setTooltip("Remove plugin");
        m_remove_button.onClick = [this] {
            m_listener.onRemovePluginPressed(m_plugin.instance_id);
        };
        addAndMakeVisible(m_remove_button);
    }

    // Applies controller-derived remove availability to the row button.
    void setRemoveEnabled(bool enabled)
    {
        m_remove_button.setEnabled(enabled);
    }

    // Draws the highlight box around the clickable label area only; the Remove button sits
    // visually beside it so the two interactive zones never overlap.
    void paint(juce::Graphics& g) override
    {
        auto highlight_area = getLocalBounds();
        highlight_area.removeFromRight(g_remove_button_width + g_row_button_gap);

        g.setColour(m_is_hovered ? g_plugin_row_hover_background : g_plugin_row_background);
        g.fillRect(highlight_area);
        if (m_is_hovered)
        {
            g.setColour(g_plugin_row_hover_accent);
            g.fillRect(highlight_area.withWidth(4));
        }

        g.setColour(m_is_hovered ? g_plugin_row_hover_border : g_plugin_row_border);
        g.drawRect(highlight_area, m_is_hovered ? 2 : 1);

        const auto label_area = highlight_area.reduced(8, 0);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions{14.0f});
        g.drawFittedText(pluginLabel(m_plugin), label_area, juce::Justification::centredLeft, 1);
    }

    // Keeps the remove button fixed on the right side of the row.
    void resized() override
    {
        m_remove_button.setBounds(
            getLocalBounds()
                .reduced(4, 2)
                .removeFromRight(g_remove_button_width)
                .withSizeKeepingCentre(g_remove_button_width, getHeight() - 4));
    }

    // Treats a row click as an editor-window request while ignoring drag releases.
    void mouseUp(const juce::MouseEvent& event) override
    {
        if (event.mouseWasClicked())
        {
            m_listener.onOpenPluginPressed(m_plugin.instance_id);
        }
    }

    // Shows that the row itself has a click action independent of the remove button.
    void mouseEnter(const juce::MouseEvent& /*event*/) override
    {
        m_is_hovered = true;
        repaint();
    }

    // Clears the row affordance when the pointer leaves the plugin row.
    void mouseExit(const juce::MouseEvent& /*event*/) override
    {
        m_is_hovered = false;
        repaint();
    }

private:
    // Listener that receives this row's remove intent.
    Listener& m_listener;

    // Stable plugin snapshot represented by this row.
    core::PluginViewState m_plugin;

    // Button that emits a remove intent for this row's plugin instance.
    juce::TextButton m_remove_button;

    // True while the pointer is over the row, used only for the clickable-row affordance.
    bool m_is_hovered{false};
};

// Creates the panel controls and routes the add command through the owner.
SignalChainPanel::SignalChainPanel(Listener& listener)
    : m_listener(listener)
{
    setComponentID("signal_chain_panel");
    m_add_plugin_button.setComponentID("add_plugin_button");
    m_add_plugin_button.setButtonText("Add Plugin");
    m_add_plugin_button.onClick = [this] { m_listener.onAddPluginPressed(); };
    addAndMakeVisible(m_add_plugin_button);
    setState(core::SignalChainViewState{});
}

// Uses default destruction for JUCE child controls owned by value.
SignalChainPanel::~SignalChainPanel() = default;

// Stores the render state and updates controls whose enabledness is derived outside the view.
void SignalChainPanel::setState(const core::SignalChainViewState& state)
{
    m_state = state;
    m_add_plugin_button.setEnabled(m_state.add_plugin_enabled);
    rebuildPluginRows();
    resized();
    repaint();
}

// Draws a compact plugin-chain panel without introducing plugin-host policy into the widget.
void SignalChainPanel::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(g_panel_background);
    g.setColour(g_panel_border);
    g.drawRect(bounds);

    auto area = bounds.reduced(g_panel_inset);
    auto header = area.removeFromTop(g_header_height);
    header.removeFromRight(g_add_button_width + g_panel_inset);

    g.setColour(g_panel_header_background);
    g.fillRect(header);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions{16.0f, juce::Font::bold});
    g.drawFittedText("Signal Chain", header.reduced(8, 0), juce::Justification::centredLeft, 1);

    area.removeFromTop(g_panel_inset);
    if (m_state.plugins.empty())
    {
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions{14.0f});
        g.drawFittedText("No plugins loaded", area, juce::Justification::centredLeft, 1);
        return;
    }
}

// Keeps the add button in the header area and lays out visible plugin rows in the body.
void SignalChainPanel::resized()
{
    auto area = getLocalBounds().reduced(g_panel_inset);
    auto header = area.removeFromTop(g_header_height);
    m_add_plugin_button.setBounds(
        header.removeFromRight(g_add_button_width)
            .withSizeKeepingCentre(
                g_add_button_width, std::min(g_add_button_height, header.getHeight())));

    area.removeFromTop(g_panel_inset);
    for (const std::unique_ptr<PluginRowView>& row : m_plugin_rows)
    {
        if (row == nullptr)
        {
            continue;
        }

        if (area.getHeight() < g_plugin_row_height)
        {
            row->setVisible(false);
            continue;
        }

        row->setVisible(true);
        row->setBounds(area.removeFromTop(g_plugin_row_height));
        area.removeFromTop(std::min(g_plugin_row_gap, area.getHeight()));
    }
}

// Recreates child rows from the latest controller state so each button carries a stable ID.
void SignalChainPanel::rebuildPluginRows()
{
    for (const std::unique_ptr<PluginRowView>& row : m_plugin_rows)
    {
        if (row != nullptr)
        {
            removeChildComponent(row.get());
        }
    }

    m_plugin_rows.clear();
    m_plugin_rows.reserve(m_state.plugins.size());
    for (const core::PluginViewState& plugin : m_state.plugins)
    {
        auto row = std::make_unique<PluginRowView>(plugin, m_listener);
        row->setRemoveEnabled(m_state.remove_plugins_enabled);
        addAndMakeVisible(*row);
        m_plugin_rows.push_back(std::move(row));
    }
}

} // namespace rock_hero::editor::ui
