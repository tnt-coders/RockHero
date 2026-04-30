#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/audio/i_edit.h>
#include <rock_hero/audio/i_thumbnail.h>
#include <rock_hero/audio/i_thumbnail_factory.h>
#include <rock_hero/audio/i_transport.h>
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
        current_position = core::TimePosition{};
    }

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
        return current_position;
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
    core::TimePosition current_position{};
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
class FakeThumbnail final : public audio::IThumbnail
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

// Creates fake thumbnails while recording the owner component passed by Editor.
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
    FakeThumbnailFactory thumbnail_factory;

    Editor editor{session, transport, edit, thumbnail_factory};
    auto& component = editor.component();

    CHECK(dynamic_cast<EditorView*>(&component) != nullptr);
    auto& load_button = findRequiredChild<juce::TextButton>(component, "load_button");
    CHECK(load_button.isEnabled());
    CHECK(thumbnail_factory.create_call_count == 1);
    REQUIRE(thumbnail_factory.last_owner != nullptr);
    CHECK(thumbnail_factory.last_owner->getComponentID() == "track_view");
    REQUIRE(thumbnail_factory.last_thumbnail != nullptr);
    CHECK(thumbnail_factory.last_thumbnail->set_source_call_count == 1);
    CHECK(
        thumbnail_factory.last_thumbnail->last_source ==
        std::optional<core::AudioAsset>{audio_asset});
    CHECK(transport.listeners.size() == 1);
}

} // namespace rock_hero::ui
