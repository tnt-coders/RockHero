#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/audio/i_edit.h>

namespace rock_hero::audio
{

namespace
{

// Minimal fake audio-edit port that records the latest source-setting request.
struct FakeEdit final : IEdit
{
    // Seeds the fake with the result returned by setTrackAudioSource().
    explicit FakeEdit(bool set_result)
        : result{set_result}
    {}

    // Records the requested track and asset so controller tests can inspect the boundary call.
    bool setTrackAudioSource(core::TrackId track_id, const core::AudioAsset& audio_asset) override
    {
        last_track_id = track_id;
        last_audio_asset = audio_asset;
        ++call_count;
        return result;
    }

    // Result returned from setTrackAudioSource() to simulate backend success or failure.
    bool result{false};

    // Last track id received through setTrackAudioSource(), if the fake has been called.
    std::optional<core::TrackId> last_track_id{};

    // Last audio asset received through setTrackAudioSource(), if the fake has been called.
    std::optional<core::AudioAsset> last_audio_asset{};

    // Number of source-setting attempts observed by the fake.
    int call_count{0};
};

} // namespace

// Verifies the audio-edit port receives the session track identity.
TEST_CASE("IEdit fake receives a track id", "[audio][edit]")
{
    FakeEdit edit{true};

    const auto applied = edit.setTrackAudioSource(
        core::TrackId{7}, core::AudioAsset{std::filesystem::path{"guitar.wav"}});

    CHECK(applied);
    REQUIRE(edit.last_track_id.has_value());
    CHECK(edit.last_track_id == core::TrackId{7});
}

// Verifies the audio-edit port receives the framework-free asset reference.
TEST_CASE("IEdit fake receives an audio asset", "[audio][edit]")
{
    FakeEdit edit{true};
    const core::AudioAsset asset{std::filesystem::path{"drums.wav"}};

    const auto applied = edit.setTrackAudioSource(core::TrackId{3}, asset);

    CHECK(applied);
    REQUIRE(edit.last_audio_asset.has_value());
    CHECK(edit.last_audio_asset == asset);
}

// Verifies the port return value can represent failed source changes.
TEST_CASE("IEdit fake can report failed source changes", "[audio][edit]")
{
    FakeEdit edit{false};

    const auto applied = edit.setTrackAudioSource(
        core::TrackId{1}, core::AudioAsset{std::filesystem::path{"missing.wav"}});

    CHECK_FALSE(applied);
    CHECK(edit.call_count == 1);
}

} // namespace rock_hero::audio
