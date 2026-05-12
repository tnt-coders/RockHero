#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <limits>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/i_thumbnail.h>
#include <rock_hero/common/audio/i_thumbnail_factory.h>
#include <rock_hero/common/audio/i_transport.h>
#include <rock_hero/editor/ui/editor_view.h>
#include <stdexcept>
#include <utility>

namespace rock_hero::editor::ui
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
class FakeEditorController final : public core::IEditorController
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

    void onUnsavedChangesDecision(core::UnsavedChangesDecision decision) override
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
    std::optional<core::UnsavedChangesDecision> last_unsaved_changes_decision{};
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
class FakeTransport final : public common::audio::ITransport
{
public:
    void play() override
    {}
    void pause() override
    {}
    void stop() override
    {}
    void seek(common::core::TimePosition position_value) override
    {
        current_position = position_value;
    }

    [[nodiscard]] common::audio::TransportState state() const noexcept override
    {
        return current_state;
    }

    [[nodiscard]] common::core::TimePosition position() const noexcept override
    {
        position_read_count += 1;
        return current_position;
    }

    void addListener(Listener& /*listener*/) override
    {}
    void removeListener(Listener& /*listener*/) override
    {}

    common::audio::TransportState current_state{};
    common::core::TimePosition current_position{};
    mutable int position_read_count{0};
};

// Records thumbnail source updates installed through the arrangement view owned by EditorView.
class FakeThumbnail final : public common::audio::IThumbnail
{
public:
    void setSource(const common::core::AudioAsset& audio_asset) override
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
        juce::Graphics& /*g*/, juce::Rectangle<int> /*bounds*/,
        common::core::TimeRange /*visible_range*/, float /*vertical_zoom*/) override
    {
        return true;
    }

    std::optional<common::core::AudioAsset> last_source{};
    int set_source_call_count{0};
    bool has_source{false};
};

// Creates fake thumbnails while recording the owner component passed by EditorView.
class FakeThumbnailFactory final : public common::audio::IThumbnailFactory
{
public:
    [[nodiscard]] std::unique_ptr<common::audio::IThumbnail> createThumbnail(
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

// Recursively searches a component tree because viewport-hosted children are nested.
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

// Returns a required descendant component by id and type, failing the current test if missing.
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

// Returns the play/pause button from the transport-controls child.
[[nodiscard]] juce::DrawableButton& getPlayPauseButton(TransportControls& controls)
{
    auto* button =
        dynamic_cast<juce::DrawableButton*>(controls.findChildWithID("play_pause_button"));
    if (button == nullptr)
    {
        throw std::runtime_error{"TransportControls play/pause button missing"};
    }
    return *button;
}

// Returns the stop button from the transport-controls child.
[[nodiscard]] juce::DrawableButton& getStopButton(TransportControls& controls)
{
    auto* button = dynamic_cast<juce::DrawableButton*>(controls.findChildWithID("stop_button"));
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
[[nodiscard]] core::ArrangementViewState makeArrangementState(
    std::filesystem::path path, double duration_seconds = 4.0)
{
    return core::ArrangementViewState{
        .audio_asset = common::core::AudioAsset{std::move(path)},
        .audio_duration = common::core::TimeDuration{duration_seconds},
    };
}

// Builds a loaded editor state with a full-source timeline for viewport layout tests.
[[nodiscard]] core::EditorViewState makeLoadedEditorState(double duration_seconds)
{
    return core::EditorViewState{
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
            common::core::TimeRange{
                .start = common::core::TimePosition{},
                .end = common::core::TimePosition{duration_seconds},
            },
        .arrangement = makeArrangementState(std::filesystem::path{"mix.wav"}, duration_seconds),
        .last_error = std::nullopt,
        .unsaved_changes_prompt = std::nullopt,
        .save_as_prompt = std::nullopt,
    };
}

// Returns the default timeline-canvas height after reserving the horizontal scrollbar.
[[nodiscard]] int defaultUsableTrackViewportHeight(const juce::Viewport& viewport)
{
    return 720 - viewport.getScrollBarThickness();
}

// Returns the fixed track row height that allows three tracks at the default size.
[[nodiscard]] int defaultTrackHeight(const juce::Viewport& viewport)
{
    return defaultUsableTrackViewportHeight(viewport) / 3;
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
        core::EditorViewState{
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
                common::core::TimeRange{
                    .start = common::core::TimePosition{},
                    .end = common::core::TimePosition{4.0},
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
    auto& track_viewport = findRequiredChild<juce::Component>(view, "track_viewport");
    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredChild<ArrangementView>(view, "arrangement_view");
    auto& cursor_overlay = findRequiredChild<juce::Component>(view, "cursor_overlay");
    constexpr int save_command{3};
    constexpr int close_command{5};
    constexpr int exit_command{6};
    constexpr int publish_command{7};

    view.setState(core::EditorViewState{});

    CHECK(menu_bar.isVisible());
    const juce::StringArray menu_names = view.getMenuBarNames();
    REQUIRE(menu_names.size() == 1);
    CHECK(menu_names[0] == "File");
    CHECK_FALSE(requiredMenuItem(view.getMenuForIndex(0, "File"), save_command).isEnabled);
    view.menuItemSelected(save_command, 0);
    CHECK(controller.save_request_count == 0);
    CHECK_FALSE(getPlayPauseButton(controls).isEnabled());
    CHECK_FALSE(getStopButton(controls).isEnabled());
    CHECK(track_viewport.isVisible());
    CHECK(track_content.isVisible());
    CHECK_FALSE(arrangement_view.isVisible());
    CHECK_FALSE(cursor_overlay.isVisible());
    CHECK(transport.position_read_count == 0);

    view.setState(
        core::EditorViewState{
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
                common::core::TimeRange{
                    .start = common::core::TimePosition{},
                    .end = common::core::TimePosition{8.0},
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
    CHECK(cursor_overlay.isVisible());
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

// Verifies the full-width transport strip sits directly above the track viewport.
TEST_CASE("EditorView lays out toolbar below the menu bar", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 500, 200);

    auto& controls = findRequiredChild<TransportControls>(view, "transport_controls");
    auto& track_viewport = findRequiredChild<juce::Component>(view, "track_viewport");
    auto& viewport = findRequiredChild<juce::Viewport>(view, "track_viewport_scroll");
    auto& arrangement_view = findRequiredChild<ArrangementView>(view, "arrangement_view");
    auto& cursor_overlay = findRequiredChild<juce::Component>(view, "cursor_overlay");
    CHECK(controls.getBounds() == juce::Rectangle<int>{8, 24, 484, 40});
    CHECK(track_viewport.getBounds() == juce::Rectangle<int>{8, 72, 484, 120});
    CHECK(
        arrangement_view.getBounds() ==
        juce::Rectangle<int>{0, 0, 1264, defaultTrackHeight(viewport)});
    CHECK(
        cursor_overlay.getBounds() ==
        juce::Rectangle<int>{0, 0, 1264, defaultUsableTrackViewportHeight(viewport)});
}

// Verifies the default editor size gives the viewport its planned fixed canvas dimensions.
TEST_CASE("EditorView lays out the default track viewport", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 1280, 800);

    auto& track_viewport = findRequiredChild<juce::Component>(view, "track_viewport");
    auto& viewport = findRequiredChild<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredChild<ArrangementView>(view, "arrangement_view");
    auto& cursor_overlay = findRequiredChild<juce::Component>(view, "cursor_overlay");
    CHECK(track_viewport.getBounds() == juce::Rectangle<int>{8, 72, 1264, 720});
    CHECK(track_content.getBounds() == juce::Rectangle<int>{0, 0, 1264, 720});
    CHECK(
        arrangement_view.getBounds() ==
        juce::Rectangle<int>{0, 0, 1264, defaultTrackHeight(viewport)});
    CHECK(cursor_overlay.getBounds() == track_content.getLocalBounds());
}

// Verifies the default zoom maps ten seconds of timeline to the canonical canvas width.
TEST_CASE("EditorView defaults zoom to ten seconds per canvas", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 1280, 800);
    view.setState(makeLoadedEditorState(20.0));

    auto& track_viewport = findRequiredChild<juce::Component>(view, "track_viewport");
    auto& viewport = findRequiredChild<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredChild<ArrangementView>(view, "arrangement_view");
    auto& cursor_overlay = findRequiredChild<juce::Component>(view, "cursor_overlay");
    CHECK(
        track_content.getBounds() ==
        juce::Rectangle<int>{0, 0, 2528, defaultUsableTrackViewportHeight(viewport)});
    CHECK(
        arrangement_view.getBounds() ==
        juce::Rectangle<int>{0, 0, 2528, defaultTrackHeight(viewport)});
    CHECK(cursor_overlay.getBounds() == track_content.getLocalBounds());
    CHECK(viewport.getViewWidth() == track_viewport.getWidth());
    CHECK(viewport.getViewHeight() == track_content.getHeight());
}

// Verifies mouse wheel zoom scales the timeline content instead of seeking transport.
TEST_CASE("EditorView wheel zoom scales track width", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 1280, 800);
    view.setState(makeLoadedEditorState(20.0));

    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    const int default_width = track_content.getWidth();

    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f),
        juce::MouseWheelDetails{
            .deltaX = 0.0f,
            .deltaY = 1.0f,
            .isReversed = false,
            .isSmooth = false,
            .isInertial = false,
        });

    CHECK(track_content.getWidth() > default_width);
    CHECK(controller.waveform_click_count == 0);
}

// Verifies Stop-command state resets the horizontal viewport without treating pause as stop.
TEST_CASE("EditorView stop reset snaps track viewport to start", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 1280, 800);

    auto state = makeLoadedEditorState(20.0);
    state.stop_enabled = true;
    state.play_pause_shows_pause_icon = true;
    transport.current_position = common::core::TimePosition{5.0};
    view.setState(state);

    auto& viewport = findRequiredChild<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    REQUIRE(track_content.getWidth() > viewport.getViewWidth());

    viewport.setViewPosition(400, 0);
    REQUIRE(viewport.getViewPositionX() == 400);

    state.play_pause_shows_pause_icon = false;
    view.setState(state);
    CHECK(viewport.getViewPositionX() == 400);

    state.stop_enabled = false;
    transport.current_position = common::core::TimePosition{};
    view.setState(state);
    CHECK(viewport.getViewPositionX() == 0);
}

// Verifies editor resizing does not scale the fixed waveform track.
TEST_CASE("EditorView keeps waveform track fixed on resize", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 1280, 800);
    view.setState(makeLoadedEditorState(20.0));
    auto& track_viewport = findRequiredChild<juce::Component>(view, "track_viewport");
    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredChild<ArrangementView>(view, "arrangement_view");
    const juce::Rectangle<int> track_bounds = arrangement_view.getBounds();
    const juce::Rectangle<int> content_bounds = track_content.getBounds();

    view.setBounds(0, 0, 1000, 500);

    CHECK(track_viewport.getBounds() == juce::Rectangle<int>{8, 72, 984, 420});
    CHECK(track_content.getBounds() == content_bounds);
    CHECK(arrangement_view.getBounds() == track_bounds);
}

// Verifies larger windows extend cursor height without changing zoom-derived width.
TEST_CASE("EditorView keeps zoomed cursor width on larger viewport", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 1600, 1000);
    view.setState(makeLoadedEditorState(20.0));

    auto& track_viewport = findRequiredChild<juce::Component>(view, "track_viewport");
    auto& viewport = findRequiredChild<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredChild<ArrangementView>(view, "arrangement_view");
    auto& cursor_overlay = findRequiredChild<juce::Component>(view, "cursor_overlay");
    CHECK(track_viewport.getBounds() == juce::Rectangle<int>{8, 72, 1584, 920});
    CHECK(
        track_content.getBounds() ==
        juce::Rectangle<int>{0, 0, 2528, 920 - viewport.getScrollBarThickness()});
    CHECK(
        arrangement_view.getBounds() ==
        juce::Rectangle<int>{0, 0, 2528, defaultTrackHeight(viewport)});
    CHECK(cursor_overlay.getBounds() == track_content.getLocalBounds());
}

// Verifies editor-wide timeline clicks are forwarded to the controller.
TEST_CASE("EditorView forwards timeline clicks to the controller", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};
    view.setBounds(0, 0, 1600, 1000);
    view.setState(
        core::EditorViewState{
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
                common::core::TimeRange{
                    .start = common::core::TimePosition{},
                    .end = common::core::TimePosition{4.0},
                },
            .arrangement = makeArrangementState(std::filesystem::path{"mix.wav"}),
            .last_error = std::nullopt,
            .unsaved_changes_prompt = std::nullopt,
            .save_as_prompt = std::nullopt,
        });

    auto& cursor_overlay = findRequiredChild<juce::Component>(view, "cursor_overlay");
    auto& arrangement_view = findRequiredChild<ArrangementView>(view, "arrangement_view");
    CHECK(cursor_overlay.isVisible());
    REQUIRE(cursor_overlay.getWidth() > 0);
    const float click_x = std::floor(static_cast<float>(cursor_overlay.getWidth()) * 0.25f) + 0.5f;
    const auto click_y = static_cast<float>(cursor_overlay.getHeight() - 20);
    REQUIRE(click_y > static_cast<float>(arrangement_view.getBottom()));
    REQUIRE(click_y < static_cast<float>(cursor_overlay.getHeight()));
    cursor_overlay.mouseDown(makeMouseDownEvent(cursor_overlay, click_x, click_y));

    CHECK(controller.waveform_click_count == 1);
    const auto last_normalized_x = controller.last_normalized_x;
    REQUIRE(last_normalized_x.has_value());
    const double expected_normalized_x =
        static_cast<double>(click_x) / static_cast<double>(cursor_overlay.getWidth());
    CHECK(optionalValueForApprox(last_normalized_x) == Catch::Approx(expected_normalized_x));
}

// Verifies the focusable editor root maps keyboard play/pause to the transport intent.
TEST_CASE("EditorView forwards space key to the controller", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    CHECK(view.getWantsKeyboardFocus());
    CHECK(view.keyPressed(juce::KeyPress{juce::KeyPress::spaceKey}));
    CHECK(controller.play_pause_press_count == 1);
}

// Verifies cursor geometry uses a pushed visible range plus a separately read position.
TEST_CASE("EditorView cursor geometry maps position through visible range", "[ui][editor-view]")
{
    const auto midpoint_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{5.0},
        common::core::TimeRange{
            .start = common::core::TimePosition{}, .end = common::core::TimePosition{10.0}
        },
        201);
    REQUIRE(midpoint_cursor.has_value());
    CHECK(optionalValueForApprox(midpoint_cursor) == Catch::Approx(100.0f));

    const auto offset_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{12.0},
        common::core::TimeRange{
            .start = common::core::TimePosition{10.0}, .end = common::core::TimePosition{14.0}
        },
        101);
    REQUIRE(offset_cursor.has_value());
    CHECK(optionalValueForApprox(offset_cursor) == Catch::Approx(50.0f));

    const auto fractional_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{1.25},
        common::core::TimeRange{
            .start = common::core::TimePosition{}, .end = common::core::TimePosition{4.0}
        },
        101);
    REQUIRE(fractional_cursor.has_value());
    CHECK(optionalValueForApprox(fractional_cursor) == Catch::Approx(31.25f));

    const auto before_start_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{-1.0},
        common::core::TimeRange{
            .start = common::core::TimePosition{}, .end = common::core::TimePosition{4.0}
        },
        101);
    REQUIRE(before_start_cursor.has_value());
    CHECK(optionalValueForApprox(before_start_cursor) == Catch::Approx(0.0f));

    const auto after_end_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{9.0},
        common::core::TimeRange{
            .start = common::core::TimePosition{}, .end = common::core::TimePosition{4.0}
        },
        101);
    REQUIRE(after_end_cursor.has_value());
    CHECK(optionalValueForApprox(after_end_cursor) == Catch::Approx(100.0f));

    CHECK_FALSE(cursorXForTimelinePosition(
                    common::core::TimePosition{1.0},
                    common::core::TimeRange{
                        .start = common::core::TimePosition{}, .end = common::core::TimePosition{}
                    },
                    101)
                    .has_value());
}

} // namespace rock_hero::editor::ui
