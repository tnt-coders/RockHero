# `rock-hero-plugins/` and the Gain plugin

Status: planned 2026-10-02 against `5c0299a8`; nothing built. Facts verified against the code and
the vendored JUCE 8 / Tracktion sources (paths under `external/tracktion_engine/modules/` unless
they start with a product folder). Re-verify before executing.

## Goal

Give the charter a Gain block to insert anywhere in a tone's chain — one parameter, gain in dB
over ±24, one knob with a typed dB box — that ships inside the app and can never be missing. Write
it once as a plain JUCE plugin so the same code later ships as a VST3 for other DAWs, and start a
home for more such plugins.

## Rulings

- **Placement: a top-level `rock-hero-plugins/` scope in this repo, one folder per plugin**, not
  a separate repository, because the plugins are closely tied to the app, JUCE is already here,
  agents may commit here but not elsewhere, and both an own licence and own releases remain
  possible from one repo. Namespace `rock_hero::plugins::<plugin>`, include path
  `<rock_hero/plugins/<plugin>/...>`, target alias `rock_hero::plugins::<plugin>`.
- **Dependency rule: a plugin links the `rock_hero::juce_*` wrappers and nothing of RockHero** —
  no `rock_hero::common::*`, editor or game include or link — so the code is portable as-is. The
  build enforces it: the tree has no global `include_directories()`, `add_compile_options()`,
  `add_definitions()` or `link_libraries()` call (verified 2026-10-02 over the root, `cmake/`,
  every product `CMakeLists.txt` and `project-config/`), and every library exposes headers only
  through `target_include_directories(PUBLIC include)`, so a stray `#include <rock_hero/...>`
  fails to compile. No script is added, as for every other library's rule.
- **Shared flags, lint and presets, by the existing mechanisms**: the hosted library links
  `rock_hero::build_policy` like every Rock Hero-owned target (`RockHeroBuildPolicy.cmake:66-70`);
  the clang-tidy regex at `CMakeLists.txt:32` gains `rock-hero-plugins`; `.clang-tidy`,
  `.clang-format` and the pre-commit hooks already apply by directory with no path filter; the
  presets come from `project-config` through `CMakePresets.json`.
- **Hosting route: the processor is compiled into the app and hosted through Tracktion's
  `ExternalPlugin`, created by a RockHero `juce::AudioPluginFormat`**, because `ExternalPlugin` is
  already the adapter — parameters become `AutomatableParameter`s keyed by the processor's own
  ids (`tracktion_ExternalPlugin.cpp:798-804`), state goes through `getStateInformation`, the
  editor through `createEditorIfNeeded` (`:2062`), and lanes, undo, capture, export, restore and
  the window all handle it today — and an in-process format (JUCE's own
  `extras/AudioPluginHost/Source/Plugins/InternalPlugins.h:44` pattern; the format API is ten
  trivial virtuals, `juce_AudioPluginFormat.h:56-162`; Tracktion creates through
  `PluginManager::createPluginInstance`, `tracktion_PluginManager.h:107`) needs no file, no
  scan, no packaging, and gives the game the plugin by linking `common/audio`. Rejected in one
  line each: a Tracktion built-in (eight seams of Gain-specific code); a bundled `.vst3` (a file
  that can go missing, per-install paths, three OSes of packaging, the game shipping it too).
- **The processor derives from `juce::AudioPluginInstance`** because its one extra override,
  `fillInPluginDescription` (`juce_AudioPluginInstance.h:70`), is where name, vendor, format and
  ids are stated once for both the host format and the VST3; JUCE's forwarding-wrapper precedent
  is a second class with no hidden decision.
- **Identity is the fixed string `"RockHero:gain"`**, never a path, so every hash RockHero takes
  of it is the same on every machine; the path-bound identity defect of third-party plugins stays
  a backlog item (`docs/tracking/backlog.md`, 2026-10-02), not a prerequisite.
- **The catalog lists our format beside VST3 and hides a system-wide VST3 of ours**
  (`engine_plugin_host.cpp:346`, `:370`), so "Gain" appears once, always compiled in.
- **The tone's output stage stays `ToneOutputPlugin`, a Tracktion built-in**, because hosting it
  would route the authored tone level through the hosted explicit-value path, whose echo
  behaviour needs a probe, and pay per-block hosting cost on every always-loaded branch;
  revisit after Gain ships.
- **Licence and releases are later items**: the folder may carry its own `LICENSE` (legal item
  below); a VST3 ships as a CPack `Plugins` component on the same release page; a split into its
  own repo (`git subtree split -P rock-hero-plugins`) is triggered only by a CI pipeline that must
  not gate the app or by outside contributors, and costs no C++ change.

## Folder tree

```text
rock-hero-plugins/                            - NEW scope, namespace rock_hero::plugins
  CMakeLists.txt                              - option(ROCK_HERO_BUILD_PLUGIN_VST3 OFF);
                                                add_subdirectory(gain)
  README.md                                   - the dependency rule; the two targets per plugin
  LICENSE                                     - LATER, once counsel decides
  gain/
    CMakeLists.txt                            - one source list; rock_hero_plugins_gain STATIC
                                                linking rock_hero::juce_audio_processors,
                                                rock_hero::juce_gui_basics, rock_hero::build_policy
                                                (alias rock_hero::plugins::gain); LATER, behind the
                                                option: juce_add_plugin(..._vst3 FORMATS VST3)
                                                over the same list
    include/rock_hero/plugins/gain/plugin/gain_processor.h
                                              - juce::AudioPluginInstance: one AudioParameterFloat
                                                "gain" (±24 dB, default 0), 5 ms SmoothedValue,
                                                XML state; fillInPluginDescription: name "Gain",
                                                vendor "Rock Hero", format "RockHero",
                                                id "RockHero:gain", a fixed unique id
    src/plugin/gain_processor.cpp
    src/plugin/gain_editor.{h,cpp}            - fixed size; one rotary juce::Slider, TextBoxBelow,
                                                editable, " dB", one decimal, double-click → 0 dB,
                                                begin/endChangeGesture around drags and typed entry
    tests/CMakeLists.txt, tests/test_gain_processor.cpp

CMakeLists.txt                                - add_subdirectory(rock-hero-plugins); the lint regex
rock-hero-common/audio/CMakeLists.txt         - common::audio links rock_hero::plugins::gain PRIVATE
rock-hero-common/audio/src/tracktion/rock_hero_plugin_format.{h,cpp}
                                              - NEW: juce::AudioPluginFormat "RockHero" over a
                                                table {identifier → factory}, one row per plugin
rock-hero-common/audio/src/engine/engine.cpp  - after :93: addFormat; knownPluginList.addType
rock-hero-common/audio/src/engine/engine_plugin_host.cpp:346,370 - the catalog constant + filter
rock-hero-common/audio/tests/test_rock_hero_plugin_format.cpp - NEW
scripts/verify-project-conventions.py         - generalize the root check to rock-hero-*/*/
```

`plugin/` is the library's hub folder (the plugin is one feature; roots hold folders only). The
header is public because the host format includes it. The dependency direction, stated as the
architecture documents state the others: `rock-hero-plugins` code must not depend on `common`,
`editor` or `game`; `common/audio` may link `rock-hero-plugins` libraries privately; editor and
game never link them directly.

## Hard constraints

- **The ids and codes never change**: `"RockHero:gain"`, the unique id, and the VST3 codes
  (manufacturer `Rkhr`, plugin `Gain` — JUCE's rules, `juce/docs/CMake API.md:517-525`) are what
  every saved tone, project and DAW session resolves.
- **The parameter list is append-only** with permanent id strings: `ExternalPlugin` keys lanes on
  `paramID` (`tracktion_ExternalPlugin.cpp:801-802`) and VST3 hosts hash the same id.
- **`JucePlugin_*` exists only in the standalone VST3 target**: no header under
  `juce_audio_processors*/processors/` references it, so the hosted library compiles with the
  wrappers' module configuration and no definition of its own.
- **No second JUCE compile in the in-app build**: the hosted library links the already-built
  wrappers; only the VST3 target, OFF by default, compiles JUCE modules
  (`JUCEUtils.cmake:1586-1615`), and then with the wrappers' non-strict flag set on the target
  and `warnings_as_errors` on our sources alone (per-source `COMPILE_OPTIONS`; OBJECT-library
  fallback).
- **State is the processor's XML**, read tolerantly, append-only; smoothing lives in the processor
  (5 ms); `processBlock` allocates nothing.

## First deliverable: the Gain block in the app (~2 days)

1. **Scope and processor**: `rock-hero-plugins/{CMakeLists.txt,README.md}`, `gain/` CMake, the
   processor, the root `add_subdirectory`, the clang-tidy regex (pass `-Configure` once).
   Verify: `test_gain_processor.cpp` — a block at −6 dB halves; state round-trips; the parameter
   id is `gain`; the description's ids are the fixed ones.
2. **Editor**: `gain_editor.{h,cpp}` as in the tree. Verify: a test drives the slider and sees
   `beginChangeGesture`/`endChangeGesture` once per drag and per typed value.
3. **Format and catalog**: `rock_hero_plugin_format.{h,cpp}`, registration in `engine.cpp`,
   the `common::audio` link, the catalog constant and vendor filter. Verify:
   `test_rock_hero_plugin_format.cpp` — the catalog lists "Gain" with format "RockHero" and no
   file; `insertPluginCandidateToTrack` yields an `ExternalPlugin` holding a `GainProcessor`; a
   chain at −6 dB round-trips through save/load, export/import and the undo memento with no path
   in the identity; `hasKnownPluginForIdentity` accepts it on a fresh engine.
4. **Probe the hosted path once**: insert, drag, undo, redo; open the window from the tile; a lane
   on `gain` plays; the game loads a tone containing it. Verify by observation; the only known
   format-conditioned branch in `ExternalPlugin` is the AU/VST3 channel-config skip (`:562`).
5. **Sight**: Gain at the head and the tail of a chain; −6 dB by drag and by typing; the window
   shows it; the tone file round-trips it; the game plays at that level.

## Later phases

- **VST3 release artifacts**: `ROCK_HERO_BUILD_PLUGIN_VST3 ON` in release builds, a pluginval
  ctest when `find_program` finds it, the bundle as a separate CPack `Plugins` zip per OS;
  Windows and Linux unsigned.
- **Installer option** "install for other DAWs": needs `cmake-package-builder` to grow a
  components page and a `$COMMONFILES64\VST3` section; Linux DEB to `lib/vst3/`.
- **Signing and notarization**: Apple Developer ID + notarytool, Windows Authenticode, secrets in
  `ci-workflows`; macOS artifacts wait for this.
- **AU and CLAP**: AU after signing; CLAP only with `clap-juce-extensions` as an approved
  dependency.
- **More plugins**: one folder each, one row in the format's table; the output stage and
  `LiveRigGainPlugin` as hosted processors if the probe clears the explicit-value echo.

## Open items

- **Design-document wording, for approval** (no `docs/design/` edit yet): in
  `architecture.md` "Repository Layout" and `CLAUDE.md` "Architecture", add `rock-hero-plugins/`
  ("our JUCE plugins: each a `juce::AudioProcessor` and its editor, hosted in-process by
  `common/audio` and buildable as VST3") and the rule "`rock-hero-plugins` code must not depend
  on `common`, `editor` or `game` code; it links the `rock_hero::juce_*` wrappers only.
  `common/audio` may link `rock-hero-plugins` libraries privately. Editor and game code never
  link them directly." In `architectural-principles.md`, the fourth scope under "Product
  Scopes", and under "Third-Party Module Linkage" that a plugin's VST3 target is the one place
  a raw `juce::` module is linked outside `RockHeroExternalModules.cmake`.
- **Legal, for counsel or the ip-legal-expert**: the folder's licence and whether an AGPLv3 repo
  may carry a differently licensed subfolder it links (mechanics once decided: folder `LICENSE`,
  root note, contributor terms in `CONTRIBUTING.md`, SPDX lines only if asked); JUCE's
  AGPLv3-or-EULA terms for plugin binaries and its splash-screen rule
  (`juce/LICENSE.md:6-8`); AGPL plugins loaded in closed hosts; the "VST" trademark in names
  and pages (SDK MIT; marks Steinberg's, `VST3_SDK/README.md:256-267`).

## Relation to other records

- `docs/plans/todo/gain-block-and-signal-chain-panel.md` — its Phase 1 is this first deliverable.
- `docs/tracking/backlog.md` (2026-10-02) — the path-bound identity defect; "standard tones built
  only from plugins we ship", which this plugin could back with no file dependency.
