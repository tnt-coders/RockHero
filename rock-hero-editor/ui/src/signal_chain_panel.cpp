#include "signal_chain_panel.h"

#include <algorithm>
#include <string>

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
const juce::Colour g_panel_background{juce::Colours::darkgrey.darker(0.24f)};
const juce::Colour g_panel_header_background{juce::Colours::darkgrey.darker(0.34f)};
const juce::Colour g_panel_border{juce::Colours::black.withAlpha(0.45f)};
const juce::Colour g_plugin_row_background{juce::Colours::darkgrey.darker(0.12f)};
const juce::Colour g_plugin_row_border{juce::Colours::black.withAlpha(0.35f)};

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

    g.setFont(juce::FontOptions{14.0f});
    for (const core::PluginViewState& plugin : m_state.plugins)
    {
        if (area.getHeight() < g_plugin_row_height)
        {
            break;
        }

        auto row = area.removeFromTop(g_plugin_row_height);
        area.removeFromTop(std::min(g_plugin_row_gap, area.getHeight()));
        g.setColour(g_plugin_row_background);
        g.fillRect(row);
        g.setColour(g_plugin_row_border);
        g.drawRect(row);
        g.setColour(juce::Colours::white);
        g.drawFittedText(
            pluginLabel(plugin), row.reduced(8, 0), juce::Justification::centredLeft, 1);
    }
}

// Keeps the add button in the header area while leaving the body for chain rendering.
void SignalChainPanel::resized()
{
    auto area = getLocalBounds().reduced(g_panel_inset);
    auto header = area.removeFromTop(g_header_height);
    m_add_plugin_button.setBounds(
        header.removeFromRight(g_add_button_width)
            .withSizeKeepingCentre(
                g_add_button_width, std::min(g_add_button_height, header.getHeight())));
}

} // namespace rock_hero::editor::ui
