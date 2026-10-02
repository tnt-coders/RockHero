// Writes the user guide's two calibration tables, the known audio devices and the pickup types,
// from the code's own rows, so the guide never restates a figure the code owns. Built and run by
// the docs targets; each output is a markdown fragment the guide includes.

#include "input/known_interfaces.h"
#include "input/pickup_types.h"

#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace
{

using rock_hero::common::audio::KnownInterface;
using rock_hero::common::audio::knownInterfaceBasisText;
using rock_hero::common::audio::knownInterfaceGain;
using rock_hero::common::audio::knownInterfaces;
using rock_hero::common::audio::PickupType;
using rock_hero::common::audio::pickupTypeLabel;
using rock_hero::common::audio::pickupTypes;

// The gain as the editor prints it: signed to one decimal, and a plain zero for no change.
[[nodiscard]] std::string gainText(double gain_db)
{
    const std::string text = std::format("{:+.1f}", gain_db);
    return text == "+0.0" ? "0.0" : text;
}

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
            gainText(knownInterfaceGain(row).db),
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

// Writes one fragment, reporting a file it could not write.
[[nodiscard]] bool writeFragment(const std::filesystem::path& path, void (*write)(std::ostream&))
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

} // namespace

int main(int argc, char** argv)
{
    const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
    if (args.size() != 2)
    {
        std::cerr << "usage: rock_hero_calibration_doc <output directory>\n";
        return 1;
    }

    const std::filesystem::path directory{args[1]};
    const bool written = writeFragment(directory / "known_interfaces.md", writeKnownInterfaces) &&
                         writeFragment(directory / "pickup_types.md", writePickupTypes);
    return written ? 0 : 1;
}
