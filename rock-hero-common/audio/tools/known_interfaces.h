/*!
\file known_interfaces.h
\brief Audio interfaces whose instrument-input sensitivity is known, and the gain each needs.
*/

#pragma once

#include <cstdint>
#include <rock_hero/common/audio/shared/gain.h>
#include <span>
#include <string_view>

namespace rock_hero::common::audio
{

/*!
\brief How a row's level was established, so a surface can say how far to trust it.

Ordered from the weakest claim up, so a value-initialized basis under-claims rather than
over-claims.
*/
enum class KnownInterfaceBasis : std::uint8_t
{
    /*! \brief Worked out from indirect evidence; no measurement or specification states it. */
    Inferred,

    /*! \brief Players measured the input with a test tone and a meter. */
    CommunityMeasurement,

    /*! \brief The manufacturer publishes the input's maximum level. */
    ManufacturerSpec,
};

/*! \brief One interface whose instrument-input sensitivity at minimum gain is known. */
struct KnownInterface
{
    /*! \brief The model as its maker spells it, brand first, e.g. "Neural DSP Quad Cortex". */
    std::string_view model;

    /*!
    \brief The input setting the level holds for, as a lower-case phrase that completes "Set it
    to ___" (the guide table's column), e.g. "the instrument input at minimum gain".
    */
    std::string_view unity_input;

    /*! \brief The dBu level the input reaches at 0 dBFS with that setting; the authored datum. */
    double level_at_0dbfs_dbu{};

    /*! \brief How the level was established. */
    KnownInterfaceBasis basis{};

    /*! \brief Where the level comes from: a specification sheet, a manual or a measurement. */
    std::string_view source;
};

/*!
\brief Returns every known interface, sorted by model.
\return The table; it lives for the program's lifetime.
*/
[[nodiscard]] std::span<const KnownInterface> knownInterfaces() noexcept;

/*!
\brief Returns the calibration gain an interface needs: its level at 0 dBFS less the reference,
rounded as every calibration gain is.
\param known_interface The interface to calibrate.
\return The gain to apply before the live guitar chain.
*/
[[nodiscard]] Gain knownInterfaceGain(const KnownInterface& known_interface) noexcept;

/*!
\brief Returns the one sentence that says how far to trust a row's figure, as the guide's table
words it.
\param basis How the figure was established.
\return A sentence ending in a period.
*/
[[nodiscard]] std::string_view knownInterfaceBasisText(KnownInterfaceBasis basis) noexcept;

} // namespace rock_hero::common::audio
