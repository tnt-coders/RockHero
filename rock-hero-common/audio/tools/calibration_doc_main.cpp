// Writes what the user guide states about calibration from the code that owns it, so the guide
// never restates a figure: the known audio devices and the pickup types as markdown tables, and the
// figures and sentences its prose quotes as Doxygen aliases. Built and run by the docs targets.

#include "known_interfaces.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <rock_hero/common/audio/input/input_calibration.h>
#include <rock_hero/common/audio/input/pickup_types.h>
#include <span>
#include <string>
#include <string_view>

namespace
{

using rock_hero::common::audio::guitarSetupInstructionText;
using rock_hero::common::audio::hardStrumInstructionText;
using rock_hero::common::audio::inputCalibrationCeilingPercentile;
using rock_hero::common::audio::inputCalibrationGainFormulaText;
using rock_hero::common::audio::inputCalibrationGainText;
using rock_hero::common::audio::inputCalibrationListenSampleCount;
using rock_hero::common::audio::inputCalibrationSampleRateHz;
using rock_hero::common::audio::inputLevelReferenceDbu;
using rock_hero::common::audio::KnownInterface;
using rock_hero::common::audio::KnownInterfaceBasis;
using rock_hero::common::audio::knownInterfaceBasisText;
using rock_hero::common::audio::knownInterfaceGain;
using rock_hero::common::audio::knownInterfaces;
using rock_hero::common::audio::PickupType;
using rock_hero::common::audio::pickupTypeLabel;
using rock_hero::common::audio::pickupTypes;

// A sentence without its final period, to sit in a table cell.
[[nodiscard]] std::string_view cellText(std::string_view sentence)
{
    if (sentence.ends_with('.'))
    {
        sentence.remove_suffix(1);
    }
    return sentence;
}

void writeKnownInterfaces(std::ostream& out)
{
    out << "| Audio device | Set it to | Level at 0 dBFS | Rock Hero gain | Basis |\n"
        << "|---|---|---|---|---|\n";
    for (const KnownInterface& row : knownInterfaces())
    {
        out << std::format(
            "| [{}]({}) | {} | {:+.1f} dBu | {} dB | {} |\n",
            row.model,
            row.source,
            row.unity_input,
            row.level_at_0dbfs_dbu,
            inputCalibrationGainText(knownInterfaceGain(row).db),
            cellText(knownInterfaceBasisText(row.basis)));
    }
}

void writePickupTypes(std::ostream& out)
{
    out << "| Pickup | Covers | Assumed hard-strum peak | How sure |\n"
        << "|---|---|---|---|\n";
    for (const PickupType& row : pickupTypes())
    {
        out << std::format(
            "| **{}** | {} | {:.1f} V | {} |\n",
            pickupTypeLabel(row.pickups),
            cellText(row.covers),
            row.hard_strum_peak_volts,
            cellText(row.evidence));
    }
}

// Each alias is a command the guide's prose writes in place of the figure or sentence, as
// \calibrationReference for "+12 dBu". The worked example is a device whose maker publishes its
// figure.
void writeFigures(std::ostream& out, const KnownInterface& example)
{
    const auto alias = [&out](std::string_view name, std::string_view value) {
        out << std::format("ALIASES += \"{}={}\"\n", name, value);
    };
    alias("calibrationReference", std::format("{:+.0f} dBu", inputLevelReferenceDbu()));
    alias("calibrationGainFormula", inputCalibrationGainFormulaText());
    alias(
        "calibrationListenSeconds",
        std::format(
            "{}",
            inputCalibrationListenSampleCount() /
                static_cast<std::size_t>(inputCalibrationSampleRateHz())));
    alias(
        "calibrationIgnoredPercent",
        std::format("{:.0f}%", (1.0 - inputCalibrationCeilingPercentile()) * 100.0));
    alias("guitarSetupInstruction", guitarSetupInstructionText());
    alias("hardStrumInstruction", hardStrumInstructionText());
    alias(
        "calibrationGainExample",
        std::format(
            "The {} reaches 0 dBFS at {:+.1f} dBu, so its gain is {:.1f} - {:.0f} = {} dB.",
            example.model,
            example.level_at_0dbfs_dbu,
            example.level_at_0dbfs_dbu,
            inputLevelReferenceDbu(),
            inputCalibrationGainText(knownInterfaceGain(example).db)));
}

// Writes one output, reporting a file it could not write.
[[nodiscard]] bool writeOutput(
    const std::filesystem::path& path, const std::function<void(std::ostream&)>& write)
{
    std::ofstream out{path};
    if (out)
    {
        write(out);
    }
    // Closing flushes, so a write that fails only at the end is still caught.
    out.close();
    if (!out)
    {
        std::cerr << "cannot write " << path.string() << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] int run(std::span<char*> args)
{
    if (args.size() != 2)
    {
        std::cerr << "usage: rock_hero_calibration_doc <output directory>\n";
        return 1;
    }

    const auto rows = knownInterfaces();
    const auto example =
        std::ranges::find(rows, KnownInterfaceBasis::ManufacturerSpec, &KnownInterface::basis);
    if (example == rows.end())
    {
        std::cerr << "no known audio device has a manufacturer's figure to work an example from\n";
        return 1;
    }

    const std::filesystem::path directory{args[1]};
    const bool written =
        writeOutput(directory / "known_interfaces.md", writeKnownInterfaces) &&
        writeOutput(directory / "pickup_types.md", writePickupTypes) &&
        writeOutput(directory / "calibration_figures.doxyfile", [&example](std::ostream& out) {
            writeFigures(out, *example);
        });
    return written ? 0 : 1;
}

} // namespace

// The path and format machinery can throw; a tool run by the build reports that and fails.
int main(int argc, char** argv)
try
{
    return run(std::span<char*>{argv, static_cast<std::size_t>(argc)});
}
catch (const std::exception& error)
{
    std::cerr << "rock_hero_calibration_doc: " << error.what() << '\n';
    return 1;
}
