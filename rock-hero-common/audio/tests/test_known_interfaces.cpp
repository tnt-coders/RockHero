#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cctype>
#include <compare>
#include <cstddef>
#include <rock_hero/common/audio/input/known_interfaces.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <string_view>

namespace rock_hero::common::audio
{

// The table is authored in model order with one row per model, so a chooser lists it as is.
TEST_CASE("Known interfaces are sorted and unique by model", "[audio][known-interfaces]")
{
    const auto rows = knownInterfaces();
    REQUIRE_FALSE(rows.empty());
    for (std::size_t index = 1; index < rows.size(); ++index)
    {
        CHECK(rows[index - 1].model < rows[index].model);
    }
}

// Every row says what it is, how to set the interface up and where its figure comes from; the
// setting completes "Set it to ___", so it starts lower-case and carries no period.
TEST_CASE("Known interface rows are complete and phrased to fit", "[audio][known-interfaces]")
{
    for (const KnownInterface& row : knownInterfaces())
    {
        CAPTURE(row.model);
        CHECK_FALSE(row.model.empty());
        CHECK_FALSE(row.source.empty());
        REQUIRE_FALSE(row.unity_input.empty());
        CHECK(std::islower(static_cast<unsigned char>(row.unity_input.front())) != 0);
        CHECK(row.unity_input.back() != '.');
        // A defaulted level is an omitted field: no instrument input reaches 0 dBFS at exactly
        // 0 dBu at minimum gain.
        CHECK(std::is_neq(row.level_at_0dbfs_dbu <=> 0.0));
        CHECK(row.level_at_0dbfs_dbu >= -10.0);
        CHECK(row.level_at_0dbfs_dbu <= 30.0);

        const double gain_db = knownInterfaceGain(row).db;
        CHECK(gain_db >= minimumGainDb());
        CHECK(gain_db <= maximumGainDb());
    }
}

// A row's gain is its level less the +12 dBu reference, rounded as every calibration gain is: the
// Quad Cortex lands on exactly the +2.3 dB Neural DSP gives for its plugins.
TEST_CASE("Known interface gain derives from the reference", "[audio][known-interfaces]")
{
    const auto rows = knownInterfaces();
    const auto quad_cortex =
        std::ranges::find(rows, std::string_view{"Neural DSP Quad Cortex"}, &KnownInterface::model);
    REQUIRE(quad_cortex != rows.end());

    CHECK_THAT(knownInterfaceGain(*quad_cortex).db, Catch::Matchers::WithinULP(2.3, 0));
}

// Each basis has one sentence, so every surface grades trust in the same words.
TEST_CASE("Known interface basis text words every basis once", "[audio][known-interfaces]")
{
    for (const KnownInterfaceBasis basis : {
             KnownInterfaceBasis::ManufacturerSpec,
             KnownInterfaceBasis::CommunityMeasurement,
             KnownInterfaceBasis::Inferred,
         })
    {
        const std::string_view text = knownInterfaceBasisText(basis);
        REQUIRE_FALSE(text.empty());
        CHECK(text.back() == '.');
    }

    CHECK(knownInterfaceBasisText(KnownInterfaceBasis::Inferred) == "Estimated figure.");
}

} // namespace rock_hero::common::audio
