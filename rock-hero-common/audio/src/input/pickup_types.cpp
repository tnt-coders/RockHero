#include "input/pickup_types.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// In PickupClass order, every field listed.
constexpr std::array g_pickup_types{
    PickupType{
        .pickups = PickupClass::Humbucker,
        .name = "humbucker",
        .covers = "Passive humbuckers of any size: full-size, Filter'Trons, and single-coil-sized "
                  "ones, whether rails (Hot Rails, Fast Track) or side-by-side (Little '59).",
        .hard_strum_peak_volts = 2.0,
        .evidence = "Six calibrated measurements; rails read level with full-size; side-by-side "
                    "inferred.",
    },
    PickupType{
        .pickups = PickupClass::SingleCoil,
        .name = "single-coil",
        .covers = "Strat, Tele, Jazzmaster, Jaguar and lipstick pickups, and stacked noiseless "
                  "pickups, which are humbuckers inside but built to sound and measure like "
                  "single coils.",
        .hard_strum_peak_volts = 1.0,
        .evidence = "Three calibrated measurements; stacked read level with vintage single "
                    "coils.",
    },
    PickupType{
        .pickups = PickupClass::P90,
        .name = "P-90",
        .covers = "Soapbar and dog-ear P-90s: single coils by construction that measure like "
                  "humbuckers.",
        .hard_strum_peak_volts = 2.0,
        .evidence = "Inferred from pickup makers' output figures.",
    },
    PickupType{
        .pickups = PickupClass::MiniHumbucker,
        .name = "mini-humbucker",
        .covers = "Narrow humbuckers, as on a Firebird or a Les Paul Deluxe.",
        .hard_strum_peak_volts = 1.2,
        .evidence = "Inferred from two makers' output figures.",
    },
    PickupType{
        .pickups = PickupClass::Active,
        .name = "active",
        .covers = "Battery-powered pickups: EMG, Fishman Fluence.",
        .hard_strum_peak_volts = 2.1,
        .evidence = "One measurement; the battery's voltage caps the output.",
    },
};

// True when each row sits at its kind's index and is complete: a name, a positive peak, and what
// it covers and the evidence written as sentences, since the window shows covers as running text.
[[nodiscard]] consteval bool rowsAreWellFormed()
{
    for (std::size_t index = 0; index < g_pickup_types.size(); ++index)
    {
        const PickupType& row = g_pickup_types.at(index);
        if (std::size_t{std::to_underlying(row.pickups)} != index || row.name.empty() ||
            !row.covers.ends_with('.') || !row.evidence.ends_with('.') ||
            !(row.hard_strum_peak_volts > 0.0))
        {
            return false;
        }
    }
    return true;
}

// One row per kind through Active, the last kind, each at its kind's index, so pickupType() is a
// lookup. A kind added after Active must become the new last kind named here.
static_assert(g_pickup_types.size() == std::size_t{std::to_underlying(PickupClass::Active)} + 1);
static_assert(rowsAreWellFormed());

} // namespace

std::span<const PickupType> pickupTypes() noexcept
{
    return g_pickup_types;
}

const PickupType& pickupType(PickupClass pickups) noexcept
{
    return pickupTypes()[std::to_underlying(pickups)];
}

std::string pickupTypeLabel(PickupClass pickups)
{
    std::string label{pickupType(pickups).name};
    label.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(label.front())));
    return label;
}

} // namespace rock_hero::common::audio
