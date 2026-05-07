#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/audio/i_edit.h>

namespace rock_hero::audio
{

namespace
{

// Test double that records arrangement-audio loading requests through the public port.
class FakeEdit final : public IEdit
{
public:
    // Seeds the fake with the accepted duration returned from loadAudio().
    explicit FakeEdit(std::optional<core::TimeDuration> load_result)
        : result(load_result)
    {}

    // Records the selected asset and returns the configured backend result.
    std::optional<core::TimeDuration> loadAudio(const core::AudioAsset& audio_asset) override
    {
        last_audio_asset = audio_asset;
        ++load_audio_call_count;
        return result;
    }

    // Records that the current arrangement audio should be cleared.
    void clearAudio() override
    {
        last_audio_asset.reset();
        ++clear_audio_call_count;
    }

    // Result returned from loadAudio() to simulate backend success or failure.
    std::optional<core::TimeDuration> result{};

    // Last audio asset received through loadAudio(), if the fake has been called.
    std::optional<core::AudioAsset> last_audio_asset{};

    // Number of audio-load calls received by the fake.
    int load_audio_call_count{0};

    // Number of audio-clear calls received by the fake.
    int clear_audio_call_count{0};
};

} // namespace

// Verifies the edit port exposes accepted arrangement-audio duration on success.
TEST_CASE("IEdit loads audio", "[audio][edit]")
{
    FakeEdit edit{core::TimeDuration{12.0}};
    const core::AudioAsset asset{std::filesystem::path{"drums.wav"}};

    const auto duration = edit.loadAudio(asset);

    CHECK(duration == std::optional{core::TimeDuration{12.0}});
    CHECK(edit.last_audio_asset == std::optional{asset});
    CHECK(edit.load_audio_call_count == 1);
}

// Verifies edit adapters can reject an arrangement-audio load without fallback values.
TEST_CASE("IEdit arrangement-audio loading can fail", "[audio][edit]")
{
    FakeEdit edit{std::nullopt};
    const core::AudioAsset asset{std::filesystem::path{"missing.wav"}};

    const auto duration = edit.loadAudio(asset);

    CHECK_FALSE(duration.has_value());
    CHECK(edit.last_audio_asset == std::optional{asset});
    CHECK(edit.load_audio_call_count == 1);
}

// Verifies the edit port exposes a command to clear the current arrangement audio.
TEST_CASE("IEdit clears audio", "[audio][edit]")
{
    FakeEdit edit{core::TimeDuration{12.0}};
    const core::AudioAsset asset{std::filesystem::path{"drums.wav"}};
    REQUIRE(edit.loadAudio(asset).has_value());

    edit.clearAudio();

    CHECK_FALSE(edit.last_audio_asset.has_value());
    CHECK(edit.clear_audio_call_count == 1);
}

} // namespace rock_hero::audio
