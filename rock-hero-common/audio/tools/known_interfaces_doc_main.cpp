// Writes the user guide's table of known audio devices from knownInterfaces(), so the guide never
// restates a figure the code owns. Built and run by the docs target; the output is a markdown
// fragment the guide includes.

#include "input/known_interfaces.h"

#include <cstddef>
#include <format>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace
{

// The gain as the editor prints it: signed to one decimal, and a plain zero for no change.
[[nodiscard]] std::string gainText(double gain_db)
{
    const std::string text = std::format("{:+.1f}", gain_db);
    return text == "+0.0" ? "0.0" : text;
}

// The basis sentence without its period, to sit in a table cell.
[[nodiscard]] std::string_view basisCell(rock_hero::common::audio::KnownInterfaceBasis basis)
{
    std::string_view text = rock_hero::common::audio::knownInterfaceBasisText(basis);
    if (text.ends_with('.'))
    {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

int main(int argc, char** argv)
{
    const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
    if (args.size() != 2)
    {
        std::cerr << "usage: rock_hero_known_interfaces_doc <output.md>\n";
        return 1;
    }

    std::ofstream out{args[1]};
    if (!out)
    {
        std::cerr << "cannot write " << args[1] << '\n';
        return 1;
    }

    out << "| Audio device | Set it to | Level at 0 dBFS | Rock Hero gain | Basis |\n"
        << "|---|---|---|---|---|\n";
    for (const rock_hero::common::audio::KnownInterface& row :
         rock_hero::common::audio::knownInterfaces())
    {
        out << std::format(
            "| [{}]({}) | {} | {:+.1f} dBu | {} dB | {} |\n",
            row.model,
            row.source,
            row.unity_input,
            row.level_at_0dbfs_dbu,
            gainText(rock_hero::common::audio::knownInterfaceGain(row).db),
            basisCell(row.basis));
    }
    return out ? 0 : 1;
}
