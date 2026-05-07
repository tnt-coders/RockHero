#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <limits>
#include <memory>
#include <optional>
#include <rock_hero/audio/i_thumbnail.h>
#include <rock_hero/audio/i_thumbnail_factory.h>
#include <rock_hero/audio/i_transport.h>
#include <rock_hero/ui/editor_view.h>
#include <stdexcept>
#include <utility>

namespace rock_hero::ui
{

namespace
{

// Returns NaN for empty optionals so Approx checks fail without unchecked optional access.
template <typename Value>
[[nodiscard]] Value optionalValueForApprox(const std::optional<Value>& value)
{
    return value.value_or(std::numeric_limits<Value>::quiet_NaN());
}

// Records editor intents emitted by EditorView child widgets.
class FakeEditorController final : public IEditorController
{
public:
    void onOpenRequested(std::filesystem::path file) override
    {
        last_open_file = std::move(file);
        open_request_count += 1;
    }

    void onImportRequested(std::filesystem::path file) override
    {
        last_import_file = std::move(file);
        import_request_count += 1;
    }

    void onSaveRequested() override
    {
        save_request_count += 1;
    }

    void onSaveAsRequested(std::filesystem::path file) override
    {
        last_save_as_file = std::move(file);
        save_as_request_count += 1;
    }

    void onPublishRequested(std::filesystem::path file) override
    {
        last_publish_file = std::move(file);
        publish_request_count += 1;
    }

    void onSaveAsCancelled() override
    {
        save_as_cancel_count += 1;
    }

    void onCloseRequested() override
    {
        close_request_count += 1;
    }

    void onExitRequested() override
    {
        exit_request_count += 1;
    }

    void onUnsavedChangesDecision(UnsavedChangesDecision decision) override
    {
        last_unsaved_changes_decision = decision;
        unsaved_changes_decision_count += 1;
    }

    void onPlayPausePressed() override
    {
        play_pause_press_count += 1;
    }

    void onStopPressed() override
    {
        stop_press_count += 1;
    }

    void onWaveformClicked(double normalized_x) override
    {
        last_normalized_x = normalized_x;
        waveform_click_count += 1;
    }

    std::optional<std::filesystem::path> last_open_file{};
    std::optional<std::filesystem::path> last_import_file{};
    std::optional<std::filesystem::path> last_save_as_file{};
    std::optional<std::filesystem::path> last_publish_file{};
    std::optional<double> last_normalized_x{};
    std::optional<UnsavedChangesDecision> last_unsaved_changes_decision{};
    int open_request_count{0};
    int import_request_count{0};
    int save_request_count{0};
    int save_as_request_count{0};
    int publish_request_count{0};
    int save_as_cancel_count{0};
    int close_request_count{0};
    int exit_request_count{0};
    int unsaved_changes_decision_count{0};
    int play_pause_press_count{0};
    int stop_press_count{0};
    int waveform_click_count{0};
};

// Fake transport gives the cursor path a position source without exposing Engine.
class FakeTransport final : public audio::ITransport
{
public:
    void play() override
    {}
    void pause() override
    {}
    void stop() override
    {}
    void seek(core::TimePosition position_value) override
    {
        current_position = position_value;
    }

    [[nodiscard]] audio::TransportState state() const noexcept override
    {
        return current_state;
    }

    [[nodiscard]] core::TimePosition position() const noexcept override
    {
        position_read_count += 1;
        return current_position;
    }

    void addListener(Listener& /*listener*/) override
    {}
    void removeListener(Listener& /*listener*/) override
    {}

    audio::TransportState current_state{};
    core::TimePosition current_position{};
    mutable int position_read_count{0};
};

// Records thumbnail source updates installed through the arrangement view owned by EditorView.
class FakeThumbnail final : public audio::IThumbnail
{
public:
    void setSource(const core::AudioAsset& audio_asset) override
    {
        last_source = audio_asset;
        has_source = true;
        set_source_call_count += 1;
    }

    [[nodiscard]] bool hasSource() const override
    {
        return has_source;
    }

    [[nodiscard]] bool isGeneratingProxy() const override
    {
        return false;
    }

    [[nodiscard]] float getProxyProgress() const override
    {
        return 0.0f;
    }

    [[nodiscard]] bool drawChannels(
        juce::Graphics& /*g*/, juce::Rectangle<int> /*bounds*/, core::TimeRange /*visible_range*/,
        float /*vertical_zoom*/) override
    {
        return true;
    }

    std::optional<core::AudioAsset> last_source{};
    int set_source_call_count{0};
    bool has_source{false};
};

// Creates fake thumbnails while recording the owner component passed by EditorView.
class FakeThumbnailFactory final : public audio::IThumbnailFactory
{
public:
    [[nodiscard]] std::unique_ptr<audio::IThumbnail> createThumbnail(
        juce::Component& owner) override
    {
        last_owner = &owner;
        create_call_count += 1;
        auto thumbnail = std::make_unique<FakeThumbnail>();
        last_thumbnail = thumbnail.get();
        return thumbnail;
    }

    juce::Component* last_owner{nullptr};
    FakeThumbnail* last_thumbnail{nullptr};
    int create_call_count{0};
};

// Returns a required child component by id and type, failing the current test if missing.
template <class ComponentType>
[[nodiscard]] ComponentType& findRequiredChild(juce::Component& parent, const juce::String& id)
{
    auto* child = parent.findChildWithID(id);
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

// Returns the play/pause button from the transport-controls child.
[[nodiscard]] juce::DrawableButton& getPlayPauseButton(TransportControls& controls)
{
    auto* button = dynamic_cast<juce::DrawableButton*>(controls.getChildComponent(0));
    if (button == nullptr)
    {
        throw std::runtime_error{"TransportControls play/pause button missing"};
    }
    return *button;
}

// Returns the stop button from the transport-controls child.
[[nodiscard]] juce::DrawableButton& getStopButton(TransportControls& controls)
{
    auto* button = dynamic_cast<juce::DrawableButton*>(controls.getChildComponent(1));
    if (button == nullptr)
    {
        throw std::runtime_error{"TransportControls stop button missing"};
    }
    return *button;
}

// Synthesizes a simple left-button mouse-down event relative to one component.
[[nodiscard]] juce::MouseEvent makeMouseDownEvent(juce::Component& component, float x, float y)
{
    const auto position = juce::Point<float>{x, y};
    const auto event_time = juce::Time::getCurrentTime();

    return {
        juce::Desktop::getInstance().getMainMouseSource(),
        position,
        juce::ModifierKeys::leftButtonModifier,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        &component,
        &component,
        event_time,
        position,
        event_time,
        1,
        false
    };
}

// Builds arrangement view state for editor-view tests that need thumbnail source propagation.
[[nodiscard]] ArrangementViewState makeArrangementState(std::filesystem::path path)
{
    return ArrangementViewState{
        .audio_asset = core::AudioAsset{std::move(path)},
        .audio_duration = core::TimeDuration{4.0},
    };
}

// Returns a plain menu item by id; these are not JUCE command-manager-backed items.
[[nodiscard]] juce::PopupMenu::Item requiredMenuItem(const juce::PopupMenu& menu, int item_id)
{
    juce::PopupMenu::MenuItemIterator iterator{menu, true};
    while (iterator.next())
    {
        const juce::PopupMenu::Item& item = iterator.getItem();
        if (item.itemID == item_id)
        {
            return item;
        }
    }

    throw std::runtime_error{"Missing menu item"};
}

} // namespace

// Verifies the arrangement thumbnail is created and later pointed at pushed audio.
TEST_CASE("EditorView applies arrangement audio to the thumbnail", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;

    EditorView view{controller, transport, thumbnail_factory};

    CHECK(thumbnail_factory.create_call_count == 1);
    REQUIRE(thumbnail_factory.last_owner != nullptr);
    CHECK(thumbnail_factory.last_owner->getComponentID() == "arrangement_view");
    REQUIRE(thumbnail_factory.last_thumbnail != nullptr);
    CHECK(thumbnail_factory.last_thumbnail->set_source_call_count == 0);

    view.setState(
        EditorViewState{
            .open_enabled = true,
            .import_enabled = true,
            .save_enabled = false,
            .save_as_enabled = false,
            .publish_enabled = false,
            .suggested_publish_file = std::filesystem::path{},
            .close_enabled = false,
            .project_loaded = true,
            .save_requires_destination = false,
            .play_pause_enabled = true,
            .stop_enabled = false,
            .play_pause_shows_pause_icon = false,
            .visible_timeline =
                core::TimeRange{
                    .start = core::TimePosition{},
                    .end = core::TimePosition{4.0},
                },
            .arrangement = makeArrangementState(std::filesystem::path{"full_mix.wav"}),
            .last_error = std::nullopt,
            .unsaved_changes_prompt = std::nullopt,
            .save_as_prompt = std::nullopt,
        });

    CHECK(thumbnail_factory.create_call_count == 1);
    CHECK(thumbnail_factory.last_thumbnail->set_source_call_count == 1);
}

// Verifies setState projects transition-shaped state into child widgets without reading position.
TEST_CASE("EditorView setState projects controls without polling position", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    auto& menu_bar = findRequiredChild<juce::MenuBarComponent>(view, "file_menu_bar");
    auto& controls = findRequiredChild<TransportControls>(view, "transport_controls");
    auto& arrangement_view = findRequiredChild<ArrangementView>(view, "arrangement_view");
    constexpr int save_command{3};
    constexpr int close_command{5};
    constexpr int exit_command{6};
    constexpr int publish_command{7};

    view.setState(EditorViewState{});

    CHECK(menu_bar.isVisible());
    const juce::StringArray menu_names = view.getMenuBarNames();
    REQUIRE(menu_names.size() == 1);
    CHECK(menu_names[0] == "File");
    CHECK_FALSE(requiredMenuItem(view.getMenuForIndex(0, "File"), save_command).isEnabled);
    view.menuItemSelected(save_command, 0);
    CHECK(controller.save_request_count == 0);
    CHECK_FALSE(getPlayPauseButton(controls).isEnabled());
    CHECK_FALSE(getStopButton(controls).isEnabled());
    CHECK_FALSE(arrangement_view.isVisible());
    CHECK(transport.position_read_count == 0);

    view.setState(
        EditorViewState{
            .open_enabled = true,
            .import_enabled = true,
            .save_enabled = true,
            .save_as_enabled = true,
            .publish_enabled = true,
            .suggested_publish_file = std::filesystem::path{"song.rock"},
            .close_enabled = true,
            .project_loaded = true,
            .save_requires_destination = false,
            .play_pause_enabled = true,
            .stop_enabled = true,
            .play_pause_shows_pause_icon = true,
            .visible_timeline =
                core::TimeRange{
                    .start = core::TimePosition{},
                    .end = core::TimePosition{8.0},
                },
            .arrangement = makeArrangementState(std::filesystem::path{"mix.wav"}),
            .last_error = std::nullopt,
            .unsaved_changes_prompt = std::nullopt,
            .save_as_prompt = std::nullopt,
        });

    CHECK(requiredMenuItem(view.getMenuForIndex(0, "File"), save_command).isEnabled);
    const auto publish_item = requiredMenuItem(view.getMenuForIndex(0, "File"), publish_command);
    CHECK(publish_item.isEnabled);
    CHECK(publish_item.text == "Publish...");
    CHECK(requiredMenuItem(view.getMenuForIndex(0, "File"), close_command).isEnabled);
    view.menuItemSelected(save_command, 0);
    view.menuItemSelected(close_command, 0);
    view.menuItemSelected(exit_command, 0);
    CHECK(controller.save_request_count == 1);
    CHECK(controller.close_request_count == 1);
    CHECK(controller.exit_request_count == 1);
    CHECK(getPlayPauseButton(controls).isEnabled());
    CHECK(getStopButton(controls).isEnabled());
    CHECK(arrangement_view.isVisible());
    CHECK_FALSE(getPlayPauseButton(controls).getToggleState());
    CHECK(transport.position_read_count == 0);
}

// Verifies the File menu occupies the top application strip instead of an inset control frame.
TEST_CASE("EditorView lays out the File menu flush with the top edge", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 500, 200);

    auto& menu_bar = findRequiredChild<juce::MenuBarComponent>(view, "file_menu_bar");
    CHECK(menu_bar.getBounds() == juce::Rectangle<int>{0, 0, 500, 24});
}

// Verifies editor-wide timeline clicks are forwarded to the controller.
TEST_CASE("EditorView forwards timeline clicks to the controller", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};
    view.setBounds(0, 0, 500, 200);
    view.setState(
        EditorViewState{
            .open_enabled = true,
            .import_enabled = true,
            .save_enabled = true,
            .save_as_enabled = true,
            .publish_enabled = true,
            .suggested_publish_file = std::filesystem::path{"song.rock"},
            .close_enabled = true,
            .project_loaded = true,
            .save_requires_destination = false,
            .play_pause_enabled = true,
            .stop_enabled = false,
            .play_pause_shows_pause_icon = false,
            .visible_timeline =
                core::TimeRange{
                    .start = core::TimePosition{},
                    .end = core::TimePosition{4.0},
                },
            .arrangement = makeArrangementState(std::filesystem::path{"mix.wav"}),
            .last_error = std::nullopt,
            .unsaved_changes_prompt = std::nullopt,
            .save_as_prompt = std::nullopt,
        });

    auto& cursor_overlay = findRequiredChild<juce::Component>(view, "cursor_overlay");
    CHECK(cursor_overlay.isVisible());
    REQUIRE(cursor_overlay.getWidth() > 0);
    const float click_x = static_cast<float>(cursor_overlay.getWidth()) * 0.25f;
    cursor_overlay.mouseDown(makeMouseDownEvent(cursor_overlay, click_x, 20.0f));

    CHECK(controller.waveform_click_count == 1);
    const auto last_normalized_x = controller.last_normalized_x;
    REQUIRE(last_normalized_x.has_value());
    CHECK(optionalValueForApprox(last_normalized_x) == Catch::Approx(0.25));
}

// Verifies keyboard play/pause uses the same editor intent as the transport button.
TEST_CASE("EditorView forwards space key to the controller", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    CHECK(view.keyPressed(juce::KeyPress{juce::KeyPress::spaceKey}));
    CHECK(controller.play_pause_press_count == 1);
}

// Verifies cursor geometry uses a pushed visible range plus a separately read position.
TEST_CASE("EditorView cursor geometry maps position through visible range", "[ui][editor-view]")
{
    const auto midpoint_cursor = cursorXForTimelinePosition(
        core::TimePosition{5.0},
        core::TimeRange{.start = core::TimePosition{}, .end = core::TimePosition{10.0}},
        201);
    REQUIRE(midpoint_cursor.has_value());
    CHECK(optionalValueForApprox(midpoint_cursor) == Catch::Approx(100.0f));

    const auto offset_cursor = cursorXForTimelinePosition(
        core::TimePosition{12.0},
        core::TimeRange{.start = core::TimePosition{10.0}, .end = core::TimePosition{14.0}},
        101);
    REQUIRE(offset_cursor.has_value());
    CHECK(optionalValueForApprox(offset_cursor) == Catch::Approx(50.0f));

    const auto fractional_cursor = cursorXForTimelinePosition(
        core::TimePosition{1.25},
        core::TimeRange{.start = core::TimePosition{}, .end = core::TimePosition{4.0}},
        101);
    REQUIRE(fractional_cursor.has_value());
    CHECK(optionalValueForApprox(fractional_cursor) == Catch::Approx(31.25f));

    const auto before_start_cursor = cursorXForTimelinePosition(
        core::TimePosition{-1.0},
        core::TimeRange{.start = core::TimePosition{}, .end = core::TimePosition{4.0}},
        101);
    REQUIRE(before_start_cursor.has_value());
    CHECK(optionalValueForApprox(before_start_cursor) == Catch::Approx(0.0f));

    const auto after_end_cursor = cursorXForTimelinePosition(
        core::TimePosition{9.0},
        core::TimeRange{.start = core::TimePosition{}, .end = core::TimePosition{4.0}},
        101);
    REQUIRE(after_end_cursor.has_value());
    CHECK(optionalValueForApprox(after_end_cursor) == Catch::Approx(100.0f));

    CHECK_FALSE(cursorXForTimelinePosition(
                    core::TimePosition{1.0},
                    core::TimeRange{.start = core::TimePosition{}, .end = core::TimePosition{}},
                    101)
                    .has_value());
}

} // namespace rock_hero::ui
