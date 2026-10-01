#include "input/known_interfaces.h"

#include "input/input_calibration.h"

#include <array>

namespace rock_hero::common::audio
{

namespace
{

// Sorted by model, every field listed. Behringer's UMC202HD, UMC204HD and UMC404HD are withheld:
// Behringer's sheet gives -3 dBu and a community measurement +16.8 dBu, a 20 dB disagreement that
// only a meter reading settles (the hardware checklist of the input-calibration rework plan). A row
// that may be 20 dB wrong is worse than the automatic measurement those players fall back to.
constexpr std::array g_known_interfaces{
    KnownInterface{
        .model = "Arturia MiniFuse",
        .unity_input = "the instrument input at minimum gain",
        .level_at_0dbfs_dbu = 11.5,
        .basis = KnownInterfaceBasis::ManufacturerSpec,
        .source = "https://www.arturia.com/products/audio/minifuse/minifuse-2",
    },
    KnownInterface{
        .model = "Audient iD4 MKI",
        .unity_input = "the D.I. input at minimum gain",
        .level_at_0dbfs_dbu = 12.0,
        .basis = KnownInterfaceBasis::ManufacturerSpec,
        .source = "https://support.audient.com/hc/en-us/articles/210725106-iD4-Detailed-Specs",
    },
    // A community table lists +17.5 dBu; Audient's own sheet states 12 dBu = 0 dBFS.
    KnownInterface{
        .model = "Audient iD4 MKII",
        .unity_input = "the D.I. input at minimum gain",
        .level_at_0dbfs_dbu = 12.0,
        .basis = KnownInterfaceBasis::ManufacturerSpec,
        .source = "https://audient.com/products/audio-interfaces/id4/tech-specs/",
    },
    KnownInterface{
        .model = "Behringer U-Phoria UMC22",
        .unity_input = "the instrument input at minimum gain",
        .level_at_0dbfs_dbu = 2.0,
        .basis = KnownInterfaceBasis::ManufacturerSpec,
        .source = "https://manualmachine.com/behringer/umc22/1942648-quick-start-guide/",
    },
    KnownInterface{
        .model = "Focusrite Scarlett 2i2 3rd Gen",
        .unity_input = "the instrument input at minimum gain",
        .level_at_0dbfs_dbu = 12.5,
        .basis = KnownInterfaceBasis::ManufacturerSpec,
        .source = "https://us.focusrite.com/products/scarlett-2i2-3rd-gen",
    },
    KnownInterface{
        .model = "Focusrite Scarlett 2i2 4th Gen",
        .unity_input = "the instrument input at minimum gain",
        .level_at_0dbfs_dbu = 12.0,
        .basis = KnownInterfaceBasis::ManufacturerSpec,
        .source = "https://focusrite.com/products/scarlett-2i2",
    },
    KnownInterface{
        .model = "Focusrite Scarlett Solo 3rd Gen",
        .unity_input = "the instrument input at minimum gain",
        .level_at_0dbfs_dbu = 12.5,
        .basis = KnownInterfaceBasis::ManufacturerSpec,
        .source = "https://userguides.focusrite.com/hc/en-gb/articles/"
                  "23031457381138-Scarlett-Solo-3rd-Gen-specifications",
    },
    KnownInterface{
        .model = "Focusrite Scarlett Solo 4th Gen",
        .unity_input = "the instrument input at minimum gain",
        .level_at_0dbfs_dbu = 12.0,
        .basis = KnownInterfaceBasis::ManufacturerSpec,
        .source = "https://userguides.focusrite.com/hc/en-gb/articles/"
                  "17505454908562-Scarlett-Solo-Specifications",
    },
    // A community table lists +17.5 dBu; the MOTU user guide's figure is used.
    KnownInterface{
        .model = "MOTU M2, M4, M6",
        .unity_input = "the combo guitar input at minimum gain",
        .level_at_0dbfs_dbu = 16.0,
        .basis = KnownInterfaceBasis::ManufacturerSpec,
        .source = "https://cdn-data.motu.com/manuals/usb-c-audio/M_Series_User_Guide.pdf",
    },
    KnownInterface{
        .model = "Neural DSP Nano Cortex",
        .unity_input = "the instrument input at minimum gain",
        .level_at_0dbfs_dbu = 10.0,
        .basis = KnownInterfaceBasis::Inferred,
        .source = "https://neuraldsp.com/manual/nano-cortex",
    },
    // Inferred from Neural DSP support's -15.1 dBFS reading of a 1 V peak sine; a community table
    // lists +14.8 dBu.
    KnownInterface{
        .model = "Neural DSP Quad Cortex",
        .unity_input = "the instrument input, 1 MOhm, at 0.0 dB input level",
        .level_at_0dbfs_dbu = 14.3,
        .basis = KnownInterfaceBasis::Inferred,
        .source = "https://unity.neuraldsp.com/t/"
                  "optimal-input-level-for-highest-accuracy-when-using-ndsp-plugins/11048",
    },
    KnownInterface{
        .model = "Neural DSP Quad Cortex mini",
        .unity_input = "input 1 or 2 (TRS), 1 MOhm, at minimum gain",
        .level_at_0dbfs_dbu = 14.5,
        .basis = KnownInterfaceBasis::Inferred,
        .source = "https://neuraldsp.com/manual/quad-cortex-mini",
    },
    KnownInterface{
        .model = "PreSonus Studio 24c",
        .unity_input = "the instrument input at minimum gain",
        .level_at_0dbfs_dbu = 19.0,
        .basis = KnownInterfaceBasis::ManufacturerSpec,
        .source = "https://www.presonus.com/en-US/interfaces/usb-audio-interfaces/studio-series/"
                  "2777700403.html",
    },
    KnownInterface{
        .model = "Solid State Logic SSL 2 / SSL 2+ MKII",
        .unity_input = "the instrument input at minimum gain",
        .level_at_0dbfs_dbu = 15.0,
        .basis = KnownInterfaceBasis::ManufacturerSpec,
        .source = "https://support.solidstatelogic.com/hc/en-gb/articles/"
                  "19991374319773-SSL-2-MKII-User-Guide",
    },
    KnownInterface{
        .model = "Universal Audio Volt",
        .unity_input = "the instrument input at minimum gain",
        .level_at_0dbfs_dbu = 12.5,
        .basis = KnownInterfaceBasis::ManufacturerSpec,
        .source = "https://help.uaudio.com/hc/en-us/articles/4409522227092-Volt-Specifications",
    },
};

} // namespace

std::span<const KnownInterface> knownInterfaces() noexcept
{
    return g_known_interfaces;
}

Gain knownInterfaceGain(const KnownInterface& known_interface) noexcept
{
    return Gain{quantizeInputCalibrationGainDb(
        known_interface.level_at_0dbfs_dbu - inputLevelReferenceDbu())};
}

std::string_view knownInterfaceBasisText(KnownInterfaceBasis basis) noexcept
{
    switch (basis)
    {
        case KnownInterfaceBasis::ManufacturerSpec:
        {
            return "Manufacturer's figure.";
        }
        case KnownInterfaceBasis::CommunityMeasurement:
        {
            return "Community-measured figure.";
        }
        case KnownInterfaceBasis::Inferred:
        {
            return "Estimated figure.";
        }
    }

    return "Estimated figure.";
}

} // namespace rock_hero::common::audio
