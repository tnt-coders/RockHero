#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/editor/ui/plugin_browser_window.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

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

    std::string last_added_plugin_id{};
    int scan_request_count{0};
    int add_request_count{0};
    int close_request_count{0};
};

// Searches a component tree for a child component by ID.
[[nodiscard]] juce::Component* findChildRecursive(juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id)
    {
        return &parent;
    }

    for (int index = 0; index < parent.getNumChildComponents(); ++index)
    {
        auto* const child = parent.getChildComponent(index);
        if (child == nullptr)
        {
            continue;
        }

        if (auto* const matched_child = findChildRecursive(*child, id); matched_child != nullptr)
        {
            return matched_child;
        }
    }

    return nullptr;
}

// Returns a required descendant component by id and type.
template <class ComponentType>
[[nodiscard]] ComponentType& findRequiredChild(juce::Component& parent, const juce::String& id)
{
    auto* child = findChildRecursive(parent, id);
    if (child == nullptr)
    {
        throw std::runtime_error{"Missing child component: " + id.toStdString()};
    }

    auto* typed_child = dynamic_cast<ComponentType*>(child);
    if (typed_child == nullptr)
    {
        throw std::runtime_error{"Child component has unexpected type: " + id.toStdString()};
    }

    return *typed_child;
}

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
                .file_path = std::filesystem::path{"Nolly.vst3"},
            },
            core::PluginCandidateViewState{
                .id = "gojira-id",
                .name = "Archetype Gojira X",
                .manufacturer = "Neural DSP",
                .format_name = "VST3",
                .file_path = std::filesystem::path{"Gojira.vst3"},
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

    auto& list_box = findRequiredChild<juce::ListBox>(window, "plugin_browser_list");
    auto& add_button = findRequiredChild<juce::TextButton>(window, "plugin_browser_add_button");

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

    auto& scan_button = findRequiredChild<juce::TextButton>(window, "plugin_browser_rescan_button");
    auto& close_button = findRequiredChild<juce::TextButton>(window, "plugin_browser_close_button");

    REQUIRE(scan_button.onClick);
    scan_button.onClick();
    REQUIRE(close_button.onClick);
    close_button.onClick();
    window.closeButtonPressed();

    CHECK(listener.scan_request_count == 1);
    CHECK(listener.close_request_count == 2);
}

// Verifies presentation-side filtering narrows the visible plugin count.
TEST_CASE("PluginBrowserWindow filters visible plugins", "[ui][plugin-browser]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakePluginBrowserListener listener;
    PluginBrowserWindow window{listener};
    window.setState(makeBrowserState());

    auto& filter = findRequiredChild<juce::TextEditor>(window, "plugin_browser_filter");
    auto& count_label = findRequiredChild<juce::Label>(window, "plugin_browser_count_label");

    CHECK(count_label.getText() == "2 plugins");

    filter.setText("gojira.vst3", false);
    REQUIRE(filter.onTextChange);
    filter.onTextChange();

    CHECK(count_label.getText() == "1 plugin");

    auto updated_state = makeBrowserState();
    updated_state.plugins[1].file_path = std::filesystem::path{"Different.vst3"};
    window.setState(updated_state);

    CHECK(count_label.getText() == "0 plugins");
}

} // namespace rock_hero::editor::ui
