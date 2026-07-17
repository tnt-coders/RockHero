> [!WARNING]
> Rock Hero is in early development and is not yet ready for use. Follow along (or contribute) as we build it!

# Rock Hero
[![Build - Windows](https://github.com/tnt-coders/RockHero/actions/workflows/build-windows.yml/badge.svg)](https://github.com/tnt-coders/RockHero/actions/workflows/build-windows.yml)
[![Build - Linux](https://github.com/tnt-coders/RockHero/actions/workflows/build-linux.yml/badge.svg)](https://github.com/tnt-coders/RockHero/actions/workflows/build-linux.yml)
[![Build - macOS](https://github.com/tnt-coders/RockHero/actions/workflows/build-macos.yml/badge.svg)](https://github.com/tnt-coders/RockHero/actions/workflows/build-macos.yml)
[![Docs](https://github.com/tnt-coders/RockHero/actions/workflows/docs.yml/badge.svg)](https://github.com/tnt-coders/RockHero/actions/workflows/docs.yml)

**Your guitar, your tone, your game. Plug in, rock out, and level up your playing!**

Rock Hero is an open-source, guitar-driven rhythm game. Plug in a real guitar, hear it processed
live through VST amp sims and effects, and play along to songs on a scrolling 3D note highway —
while your tone shifts, sweeps, and evolves with the music. A companion chart editor lets you
author songs and tab them out yourself.

## What works today

Rock Hero is at an early, first-playable stage: the editor is the furthest along, and the game has
just reached a playable vertical slice. Right now you can:

- **Author charts in the editor.** Place notes and techniques on a tablature view and edit them with
  full undo.
- **Design guitar tones.** Build signal chains from real VST3 amp sims, cabinets, and effects, and
  hear your guitar processed through them live.
- **Automate tone across a song.** Tone changes and plugin-parameter automation authored alongside
  the chart play back automatically.
- **See a 3D note highway.** Notes scroll in time with the music — in the editor's preview and in the
  game.
- **Play a song end to end.** Launch the game on a song and play along with the backing track and
  your live tone on the highway. This is still a developer vertical slice: one hardcoded song, fixed
  audio devices, no menus.
- **Package songs as `.rock` files.** Each song is one self-contained package of chart, audio, and
  art.

## Planned

> Rock Hero is in **early development**. Everything in this section is intended, not built yet, and
> the details are all subject to change.

- **Scored play** — real-time detection of what you actually play, then scoring, star power, and a
  fail meter.
- **A front end** — menus, a song library and browser, results screens, and player profiles.
- **More authoring** — interactive tempo-map editing, chart validation, and song information and art.
- **Later** — practice mode and online leaderboards.

## Built with

| Area | Technology |
|---|---|
| Language | C++23 |
| Audio engine | [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) — transport, plugin hosting, automation |
| Audio & UI framework | [JUCE](https://juce.com/) |
| Guitar plugins | VST3 |
| 3D rendering | [SDL3](https://www.libsdl.org/) + [bgfx](https://github.com/bkaradzic/bgfx) |

## Getting started

Everything you need to build, test, and contribute — required tools, a from-scratch quick start, and
the pre-push checklist — lives in **[CONTRIBUTING.md](CONTRIBUTING.md)**.

The short version, once the [prerequisites](CONTRIBUTING.md#prerequisites) are installed:

```sh
git clone --recurse-submodules https://github.com/tnt-coders/RockHero.git
cd RockHero
cmake --preset debug            # configure (Conan resolves dependencies automatically)
cmake --build --preset debug    # build
```

## Project status & roadmap

Rock Hero is early and moving fast: the editor is further along, and the game is still in its early
stages. The maintained plan of record — ordering, gates, and open questions — lives in
[`docs/plans/roadmap/00-roadmap.md`](docs/plans/roadmap/00-roadmap.md), and the system architecture
is described under [`docs/design/`](docs/design/index.md).

## Contributing

Contributions are welcome. Start with [CONTRIBUTING.md](CONTRIBUTING.md); it links the Developer
Guide (plain-language concept tours and step-by-step recipes) and the binding design documents.

## License

Rock Hero is licensed under the **GNU Affero General Public License v3.0** (AGPL-3.0). See
[LICENSE](LICENSE) for the full text.

## Acknowledgments

Rock Hero owes a lot to **[Charter](https://github.com/Lordszynencja/Charter)** by
**Lordszynencja** — the open-source guitar-chart viewer and editor whose underlying engineering and
infrastructure made this whole project possible. Thanks for building it and sharing it openly!
