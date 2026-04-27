#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/audio/i_edit.h>
#include <rock_hero/audio/i_transport.h>
#include <rock_hero/audio/thumbnail.h>
#include <rock_hero/core/session.h>
#include <rock_hero/ui/editor.h>
#include <stdexcept>
#include <vector>

namespace rock_hero::ui
{

namespace
{

// Records transport listeners and state so EditorController can subscribe during construction.
class FakeTransport final : public audio::ITransport
{
public:
    void play() override
    {
        current_state.playing = true;
    }

    void pause() override
    {
        current_state.playing = false;
    }

    void stop() override
    {
        current_state.playing = false;
        current_state.position = core::TimePosition{};
    }

    void seek(core::TimePosition position_value) override
    {
        current_state.position = position_value;
    }

    [[nodiscard]] audio::TransportState state() const override
    {
        return current_state;
    }

    [[nodiscard]] core::TimePosition position() const noexcept override
    {
        return current_state.position;
    }

    void addListener(Listener& listener) override
    {
        listeners.push_back(&listener);
    }

    void removeListener(Listener& listener) override
    {
        std::erase(listeners, &listener);
    }

    audio::TransportState current_state{};
    std::vector<Listener*> listeners{};
};

// Minimal edit port fake; Step 11 only needs construction and initial state projection.
class FakeEdit final : public audio::IEdit
{
public:
    bool setTrackAudioSource(core::TrackId track_id, const core::AudioAsset& audio_asset) override
    {
        last_track_id = track_id;
        last_audio_asset = audio_asset;
        return true;
    }

    std::optional<core::TrackId> last_track_id{};
    std::optional<core::AudioAsset> last_audio_asset{};
};

// Records thumbnail source updates installed by the composed EditorView.
class FakeThumbnail final : public audio::Thumbnail
{
public:
    void setSource(const core::AudioAsset& audio_asset) override
    {
        last_source = audio_asset;
        set_source_call_count += 1;
    }

    [[nodiscard]] bool isGeneratingProxy() const override
    {
        return false;
    }

    [[nodiscard]] float getProxyProgress() const override
    {
        return 0.0f;
    }

    [[nodiscard]] double getLength() const override
    {
        return 1.0;
    }

    void drawChannels(
        juce::Graphics& /*g*/, juce::Rectangle<int> /*bounds*/, float /*vertical_zoom*/) override
    {}

    std::optional<core::AudioAsset> last_source{};
    int set_source_call_count{0};
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
    core::Session session;
    const core::AudioAsset audio_asset{std::filesystem::path{"mix.wav"}};
    session.addTrack("Full Mix", audio_asset);
    FakeTransport transport;
    transport.current_state.duration = core::TimeDuration{8.0};
    FakeEdit edit;
    FakeThumbnail* thumbnail_ptr = nullptr;
    juce::Component* thumbnail_owner = nullptr;
    int create_thumbnail_call_count = 0;
    const ThumbnailCreator create_thumbnail = [&](juce::Component& owner) {
        thumbnail_owner = &owner;
        create_thumbnail_call_count += 1;
        auto thumbnail = std::make_unique<FakeThumbnail>();
        thumbnail_ptr = thumbnail.get();
        return thumbnail;
    };

    Editor editor{session, transport, edit, create_thumbnail};
    auto& component = editor.component();

    CHECK(dynamic_cast<EditorView*>(&component) != nullptr);
    auto& load_button = findRequiredChild<juce::TextButton>(component, "load_button");
    CHECK(load_button.isEnabled());
    CHECK(create_thumbnail_call_count == 1);
    REQUIRE(thumbnail_owner != nullptr);
    CHECK(thumbnail_owner->getComponentID() == "track_view");
    REQUIRE(thumbnail_ptr != nullptr);
    CHECK(thumbnail_ptr->set_source_call_count == 1);
    CHECK(thumbnail_ptr->last_source == std::optional<core::AudioAsset>{audio_asset});
    CHECK(transport.listeners.size() == 1);
}

} // namespace rock_hero::ui
