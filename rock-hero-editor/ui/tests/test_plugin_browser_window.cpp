#include "plugin_browser_window.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>
#include <string>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

using testing::findDescendant;
using testing::findRequiredDescendant;

// Records browser intents emitted by PluginBrowserWindow controls.
class FakePluginBrowserListener final : public PluginBrowserWindow::Listener
{
public:
    // Counts catalog scan requests.
    void onPluginBrowserScanRequested() override
    {
        scan_request_count += 1;
    }

    // Captures the selected browser plugin ID.
    void onPluginBrowserAddRequested(std::string plugin_id) override
    {
        last_added_plugin_id = std::move(plugin_id);
        add_request_count += 1;
    }

    // Counts browser close requests.
    void onPluginBrowserClosed() override
    {
        close_request_count += 1;
    }

    // Counts busy-operation cancellation requests.
    void onPluginBrowserBusyCancelRequested() override
    {
        busy_cancel_request_count += 1;
    }

    std::string last_added_plugin_id{};
    int scan_request_count{0};
    int add_request_count{0};
    int close_request_count{0};
    int busy_cancel_request_count{0};
};

// Builds a browser state with two recognizable plugins.
[[nodiscard]] core::PluginBrowserViewState makeBrowserState()
{
    return core::PluginBrowserViewState{
        .visible = true,
        .scan_enabled = true,
        .add_enabled = true,
        .plugins = {
            core::PluginCandidateViewState{
                .id = "nolly-id",
                .name = "Archetype Nolly X",
                .manufacturer = "Neural DSP",
                .format_name = "VST3",
                .primary_display_type = core::PluginDisplayType::Amp,
                .scanned_display_types =
                    {
                        core::PluginDisplayType::Amp,
                        core::PluginDisplayType::Distortion,
                        core::PluginDisplayType::Dynamics,
                        core::PluginDisplayType::Reverb,
                    },
            },
            core::PluginCandidateViewState{
                .id = "gojira-id",
                .name = "Plate Verb",
                .manufacturer = "Example Audio",
                .format_name = "VST3",
                .primary_display_type = core::PluginDisplayType::Reverb,
                .scanned_display_types = {core::PluginDisplayType::Reverb},
            },
        },
    };
}

} // namespace

// Verifies the browser forwards the selected plugin ID through its listener.
TEST_CASE("PluginBrowserWindow forwards selected add intent", "[ui][plugin-browser]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakePluginBrowserListener listener;
    PluginBrowserWindow window{listener};
    window.setState(makeBrowserState());

    auto& list_box = findRequiredDescendant<juce::ListBox>(window, "plugin_browser_list");
    REQUIRE(list_box.getHeaderComponent() != nullptr);
    CHECK(list_box.getHeaderComponent()->getComponentID() == "plugin_browser_list_header");

    auto& add_button =
        findRequiredDescendant<juce::TextButton>(window, "plugin_browser_add_button");

    list_box.selectRow(1);
    REQUIRE(add_button.onClick);
    add_button.onClick();

    CHECK(listener.add_request_count == 1);
    CHECK(listener.last_added_plugin_id == "gojira-id");
}

// Verifies scan and close commands are routed back to the window listener.
TEST_CASE("PluginBrowserWindow forwards scan and close", "[ui][plugin-browser]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakePluginBrowserListener listener;
    PluginBrowserWindow window{listener};
    window.setState(makeBrowserState());

    auto& scan_button =
        findRequiredDescendant<juce::TextButton>(window, "plugin_browser_rescan_button");
    auto& close_button =
        findRequiredDescendant<juce::TextButton>(window, "plugin_browser_close_button");

    REQUIRE(scan_button.onClick);
    scan_button.onClick();
    REQUIRE(close_button.onClick);
    close_button.onClick();
    window.closeButtonPressed();

    CHECK(listener.scan_request_count == 1);
    CHECK(listener.close_request_count == 2);
}

// Verifies presentation-side text filtering ignores paths and hidden plugin types.
TEST_CASE("PluginBrowserWindow filters visible plugins", "[ui][plugin-browser]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakePluginBrowserListener listener;
    PluginBrowserWindow window{listener};
    window.setState(makeBrowserState());

    auto& filter = findRequiredDescendant<juce::TextEditor>(window, "plugin_browser_filter");
    auto& count_label = findRequiredDescendant<juce::Label>(window, "plugin_browser_count_label");

    CHECK(count_label.getText() == "2 plugins");

    filter.setText("gojira.vst3", false);
    REQUIRE(filter.onTextChange);
    filter.onTextChange();

    CHECK(count_label.getText() == "0 plugins");

    filter.setText("reverb", false);
    filter.onTextChange();

    CHECK(count_label.getText() == "0 plugins");

    filter.setText("plate", false);
    filter.onTextChange();

    CHECK(count_label.getText() == "1 plugin");
}

// Verifies the type menu exposes normalized scanner categories and filters rows.
TEST_CASE("PluginBrowserWindow filters by scanner type", "[ui][plugin-browser]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakePluginBrowserListener listener;
    PluginBrowserWindow window{listener};
    window.setState(makeBrowserState());

    auto& type_filter =
        findRequiredDescendant<juce::ComboBox>(window, "plugin_browser_type_filter");
    auto& count_label = findRequiredDescendant<juce::Label>(window, "plugin_browser_count_label");

    CHECK(type_filter.getNumItems() == 5);
    CHECK(type_filter.getItemText(0) == "All types");
    CHECK(type_filter.getItemText(1) == "Amp");
    CHECK(type_filter.getItemText(2) == "Distortion");
    CHECK(type_filter.getItemText(3) == "Dynamics");
    CHECK(type_filter.getItemText(4) == "Reverb");
    CHECK(count_label.getText() == "2 plugins");

    type_filter.setSelectedId(4, juce::dontSendNotification);
    REQUIRE(type_filter.onChange);
    type_filter.onChange();

    CHECK(count_label.getText() == "1 plugin");

    type_filter.setSelectedId(5, juce::dontSendNotification);
    type_filter.onChange();

    CHECK(count_label.getText() == "2 plugins");

    type_filter.setSelectedId(1, juce::dontSendNotification);
    type_filter.onChange();

    CHECK(count_label.getText() == "2 plugins");
}

// Verifies editor-wide busy state overlays the plugin browser content while scanning.
TEST_CASE("PluginBrowserWindow shows busy overlay", "[ui][plugin-browser]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakePluginBrowserListener listener;
    PluginBrowserWindow window{listener};
    window.setState(makeBrowserState());

    auto& overlay = findRequiredDescendant<juce::Component>(window, "plugin_browser_busy_overlay");
    CHECK_FALSE(overlay.isVisible());
    CHECK(findDescendant(window, "busy_progress_bar") == nullptr);

    window.setBusyState(
        std::optional{core::BusyViewState{
            .operation = core::BusyOperation::ScanningPlugins,
            .message = "Scanning plugin (1/2)...\nAmp.vst3",
            .indicator = core::BusyIndicator::DeterminateProgress,
            .progress = std::optional<double>{0.5},
            .cancel_enabled = true,
        }});

    CHECK(overlay.isVisible());
    CHECK(findRequiredDescendant<juce::Component>(window, "busy_progress_bar").isVisible());
    auto& cancel_button = findRequiredDescendant<juce::TextButton>(window, "busy_cancel_button");
    CHECK(cancel_button.isVisible());
    REQUIRE(cancel_button.onClick);
    cancel_button.onClick();
    CHECK(listener.busy_cancel_request_count == 1);

    window.setBusyState(std::nullopt);

    CHECK_FALSE(overlay.isVisible());
}

} // namespace rock_hero::editor::ui
