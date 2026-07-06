#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>

namespace rock_hero::editor::ui
{

// Pins the GlyphArrangement-based width measurement that EditorView uses to size the menu-bar
// action; a wider label must report a larger preferred width than a narrower one.
TEST_CASE("MenuBarButton preferred width grows with label text", "[ui][menu-bar-button]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    constexpr int menu_strip_height{24};

    MenuBarButton button;
    button.setText("[audio device closed]");
    const int narrow_width = button.preferredWidthForHeight(menu_strip_height);

    button.setText("[48kHz 24bit: 8/8ch 128spls ~5.1/8.5ms ASIO]");
    const int wide_width = button.preferredWidthForHeight(menu_strip_height);

    CHECK(narrow_width > 0);
    CHECK(wide_width > narrow_width);
}

// Verifies the File menu and audio-device action share the top strip without overlap.
TEST_CASE("EditorView lays out menu strip actions without overlap", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 500, 200);

    auto& menu_bar = findRequiredDescendant<juce::MenuBarComponent>(view, "file_menu_bar");
    auto& audio_button = findRequiredDescendant<MenuBarButton>(view, "audio_device_button");
    CHECK(menu_bar.getBounds() == juce::Rectangle<int>{0, 0, 320, 24});
    CHECK(audio_button.getBounds() == juce::Rectangle<int>{320, 0, 180, 24});

    core::EditorViewState state;
    state.audio_device_status_text = "[48kHz 24bit: 8/8ch 128spls ~5.1/8.5ms ASIO]";
    view.setState(state);

    CHECK(audio_button.getRight() == 500);
    CHECK(menu_bar.getRight() == audio_button.getX());
    CHECK(audio_button.getWidth() > 260);
}

// Verifies the full-width transport strip sits directly above the track viewport.
TEST_CASE("EditorView lays out toolbar below the menu bar", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 500, 200);

    auto& controls = findRequiredDescendant<TransportControls>(view, "transport_controls");
    auto& arrangement_caption = findRequiredDescendant<juce::Label>(view, "arrangement_caption");
    auto& arrangement_selector =
        findRequiredDescendant<juce::ComboBox>(view, "arrangement_selector");
    auto& grid_selector = findRequiredDescendant<juce::Component>(view, "grid_spacing_selector");
    auto& track_viewport = findRequiredDescendant<juce::Component>(view, "track_viewport");
    auto& timeline_ruler = findRequiredDescendant<juce::Component>(view, "timeline_ruler");
    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    auto& cursor_overlay = findRequiredDescendant<juce::Component>(view, "cursor_overlay");
    auto& signal_chain_panel = findRequiredDescendant<SignalChainPanel>(view, "signal_chain_panel");
    // The arrangement caption and dropdown pin to the strip's left edge with the grid selector
    // beside them. The playback block (buttons + position readout) wants its whole width
    // centered on the window, but at this window width the selectors clamp the block to start
    // right of them.
    CHECK(arrangement_caption.getBounds() == juce::Rectangle<int>{8, 28, 100, 32});
    CHECK(arrangement_selector.getBounds() == juce::Rectangle<int>{112, 28, 124, 32});
    CHECK(grid_selector.getBounds() == juce::Rectangle<int>{248, 28, 132, 32});
    CHECK(controls.getBounds() == juce::Rectangle<int>{380, 28, 96, 32});
    CHECK(track_viewport.getBounds() == juce::Rectangle<int>{8, 72, 484, 80});
    CHECK(timeline_ruler.getBounds() == juce::Rectangle<int>{0, 0, 484, 53});
    CHECK(viewport.getBounds() == juce::Rectangle<int>{0, 53, 484, 27});
    CHECK(signal_chain_panel.getBounds() == juce::Rectangle<int>{8, 160, 484, 32});
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
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1280, 800);

    auto& track_viewport = findRequiredDescendant<juce::Component>(view, "track_viewport");
    auto& timeline_ruler = findRequiredDescendant<juce::Component>(view, "timeline_ruler");
    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    auto& cursor_overlay = findRequiredDescendant<juce::Component>(view, "cursor_overlay");
    auto& signal_chain_panel = findRequiredDescendant<SignalChainPanel>(view, "signal_chain_panel");
    CHECK(track_viewport.getBounds() == juce::Rectangle<int>{8, 72, 1264, 472});
    CHECK(timeline_ruler.getBounds() == juce::Rectangle<int>{0, 0, 1264, 53});
    CHECK(viewport.getBounds() == juce::Rectangle<int>{0, 53, 1264, 419});
    CHECK(signal_chain_panel.getBounds() == juce::Rectangle<int>{8, 552, 1264, 240});
    CHECK(
        track_content.getBounds() ==
        juce::Rectangle<int>{0, 0, 1264, defaultUsableTrackViewportHeight(viewport)});
    CHECK(
        arrangement_view.getBounds() ==
        juce::Rectangle<int>{0, 0, 1264, defaultTrackHeight(viewport)});
    CHECK(cursor_overlay.getBounds() == track_content.getLocalBounds());
}

// Verifies Stop-command state resets the horizontal viewport without treating pause as stop.
TEST_CASE("EditorView stop reset snaps track viewport to start", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1280, 800);

    auto state = makeLoadedEditorState(20.0);
    state.transport.stop_enabled = true;
    state.transport.play_pause_shows_pause_icon = true;
    transport.current_position = common::core::TimePosition{5.0};
    view.setState(state);

    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    REQUIRE(track_content.getWidth() > viewport.getViewWidth());

    viewport.setViewPosition(400, 0);
    REQUIRE(viewport.getViewPositionX() == 400);

    state.transport.play_pause_shows_pause_icon = false;
    view.setState(state);
    CHECK(viewport.getViewPositionX() == 400);

    state.transport.stop_enabled = false;
    transport.current_position = common::core::TimePosition{};
    view.setState(state);
    CHECK(viewport.getViewPositionX() == 0);
}

// Verifies editor resizing does not scale the fixed waveform track.
TEST_CASE("EditorView keeps waveform track fixed on resize", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1280, 800);
    view.setState(makeLoadedEditorState(20.0));
    auto& track_viewport = findRequiredDescendant<juce::Component>(view, "track_viewport");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    const juce::Rectangle<int> track_bounds = arrangement_view.getBounds();
    const juce::Rectangle<int> content_bounds = track_content.getBounds();

    view.setBounds(0, 0, 1000, 500);

    CHECK(track_viewport.getBounds() == juce::Rectangle<int>{8, 72, 984, 252});
    CHECK(track_content.getBounds() == content_bounds);
    CHECK(arrangement_view.getBounds() == track_bounds);
}

// Verifies larger windows extend cursor height without changing zoom-derived width.
TEST_CASE("EditorView keeps zoomed cursor width on larger viewport", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    view.setBounds(0, 0, 1600, 1000);
    view.setState(makeLoadedEditorState(20.0));

    auto& track_viewport = findRequiredDescendant<juce::Component>(view, "track_viewport");
    auto& viewport = findRequiredDescendant<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredDescendant<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredDescendant<ArrangementView>(view, "arrangement_view");
    auto& cursor_overlay = findRequiredDescendant<juce::Component>(view, "cursor_overlay");
    auto& signal_chain_panel = findRequiredDescendant<SignalChainPanel>(view, "signal_chain_panel");
    CHECK(track_viewport.getBounds() == juce::Rectangle<int>{8, 72, 1584, 652});
    CHECK(signal_chain_panel.getBounds() == juce::Rectangle<int>{8, 732, 1584, 260});
    CHECK(
        track_content.getBounds() ==
        juce::Rectangle<int>{0, 0, 2528, defaultUsableTrackViewportHeight(viewport)});
    CHECK(
        arrangement_view.getBounds() ==
        juce::Rectangle<int>{0, 0, 2528, defaultTrackHeight(viewport)});
    CHECK(cursor_overlay.getBounds() == track_content.getLocalBounds());
}

} // namespace rock_hero::editor::ui
