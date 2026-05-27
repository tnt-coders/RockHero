#include "editor_view.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/i_audio.h>
#include <rock_hero/common/audio/i_thumbnail.h>
#include <rock_hero/common/audio/i_thumbnail_factory.h>
#include <rock_hero/common/audio/i_transport.h>
#include <rock_hero/editor/ui/editor.h>
#include <stdexcept>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

// Records transport listeners and state so the controller can subscribe during construction.
class FakeTransport final : public common::audio::ITransport
{
public:
    // Simulates starting playback for the composed controller.
    void play() override
    {
        current_state.playing = true;
    }

    // Simulates pausing playback for the composed controller.
    void pause() override
    {
        current_state.playing = false;
    }

    // Simulates stop by clearing playback and resetting the cursor position.
    void stop() override
    {
        current_state.playing = false;
        current_position = common::core::TimePosition{};
    }

    // Records the latest seek target requested by the composed controller.
    void seek(common::core::TimePosition position_value) override
    {
        current_position = position_value;
    }

    // Returns the manually controlled coarse state.
    [[nodiscard]] common::audio::TransportState state() const noexcept override
    {
        return current_state;
    }

    // Returns the manually controlled current cursor position.
    [[nodiscard]] common::core::TimePosition position() const noexcept override
    {
        return current_position;
    }

    // Registers a non-owning listener pointer during controller construction.
    void addListener(Listener& listener) override
    {
        listeners.push_back(&listener);
    }

    // Removes the listener pointer registered by the controller.
    void removeListener(Listener& listener) override
    {
        std::erase(listeners, &listener);
    }

    // Coarse transport state returned by state().
    common::audio::TransportState current_state{};

    // Current cursor position returned by position().
    common::core::TimePosition current_position{};

    // Non-owning listeners registered by the composed controller.
    std::vector<Listener*> listeners{};
};

// Minimal audio port fake used by Editor construction and initial state projection.
class FakeAudio final : public common::audio::IAudio
{
public:
    // Accepts preparation and fills arrangement durations for controller loading paths.
    bool prepareSong(common::core::Song& song) override
    {
        ++prepare_song_call_count;
        for (common::core::Arrangement& arrangement : song.arrangements)
        {
            arrangement.audio_duration = common::core::TimeDuration{8.0};
        }
        return true;
    }

    // Records the active arrangement selected by the controller.
    bool setActiveArrangement(const common::core::Arrangement& arrangement) override
    {
        last_active_audio_asset = arrangement.audio_asset;
        ++set_active_arrangement_call_count;
        return true;
    }

    // Records backend clearing when the controller closes a project.
    void clearActiveArrangement() override
    {
        ++clear_active_arrangement_call_count;
    }

    // Last active arrangement selected by the controller.
    std::optional<common::core::AudioAsset> last_active_audio_asset{};

    // Number of song-preparation calls received.
    int prepare_song_call_count{0};

    // Number of active-arrangement replacement calls received.
    int set_active_arrangement_call_count{0};

    // Number of active-arrangement clear calls received.
    int clear_active_arrangement_call_count{0};
};

// Records thumbnail source updates installed by the composed EditorView.
class FakeThumbnail final : public common::audio::IThumbnail
{
public:
    // Records the thumbnail source applied by the arrangement view.
    void setSource(const common::core::AudioAsset& audio_asset) override
    {
        last_source = audio_asset;
        has_source = true;
        set_source_call_count += 1;
    }

    // Reports whether setSource() has supplied drawable source data.
    [[nodiscard]] bool hasSource() const override
    {
        return has_source;
    }

    // Reports that this fake never performs asynchronous proxy generation.
    [[nodiscard]] bool isGeneratingProxy() const override
    {
        return false;
    }

    // Reports fixed proxy progress for the synchronous fake.
    [[nodiscard]] float getProxyProgress() const override
    {
        return 0.0f;
    }

    // Accepts draw requests so Editor construction tests can ignore paint details.
    [[nodiscard]] bool drawChannels(
        juce::Graphics& /*g*/, juce::Rectangle<int> /*bounds*/,
        common::core::TimeRange /*visible_range*/, float /*vertical_zoom*/) override
    {
        return true;
    }

    // Last thumbnail source supplied by the view.
    std::optional<common::core::AudioAsset> last_source{};

    // Number of source assignments received.
    int set_source_call_count{0};

    // Source-readiness flag returned by hasSource().
    bool has_source{false};
};

// Creates fake thumbnails while recording the owner component passed by Editor.
class FakeThumbnailFactory final : public common::audio::IThumbnailFactory
{
public:
    // Creates a fake thumbnail and records the component that requested it.
    [[nodiscard]] std::unique_ptr<common::audio::IThumbnail> createThumbnail(
        juce::Component& owner) override
    {
        last_owner = &owner;
        create_call_count += 1;
        auto thumbnail = std::make_unique<FakeThumbnail>();
        last_thumbnail = thumbnail.get();
        return thumbnail;
    }

    // Last component that requested a thumbnail.
    juce::Component* last_owner{nullptr};

    // Last fake thumbnail returned to the composed view.
    FakeThumbnail* last_thumbnail{nullptr};

    // Number of thumbnails created by the factory.
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

} // namespace

// Verifies Editor owns the concrete view and pushes initial controller state during construction.
TEST_CASE("Editor constructs a wired editor view", "[ui][editor]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeTransport transport;
    FakeAudio audio;
    FakeThumbnailFactory thumbnail_factory;

    Editor editor{transport, audio, thumbnail_factory};
    auto& component = editor.component();

    CHECK(dynamic_cast<EditorView*>(&component) != nullptr);
    auto& menu_bar = findRequiredChild<juce::MenuBarComponent>(component, "file_menu_bar");
    CHECK(menu_bar.isVisible());
    CHECK(thumbnail_factory.create_call_count == 1);
    REQUIRE(thumbnail_factory.last_owner != nullptr);
    CHECK(thumbnail_factory.last_owner->getComponentID() == "arrangement_view");
    REQUIRE(thumbnail_factory.last_thumbnail != nullptr);
    CHECK(thumbnail_factory.last_thumbnail->set_source_call_count == 0);
    CHECK(audio.set_active_arrangement_call_count == 0);
    CHECK_FALSE(audio.last_active_audio_asset.has_value());
    CHECK(transport.listeners.size() == 1);
}

} // namespace rock_hero::editor::ui
