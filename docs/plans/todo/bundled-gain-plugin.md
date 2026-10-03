# Our plugins: a JUCE-only `rock-hero-plugins/` scope, hosted in-process, shipped as VST3 later

Status: planned 2026-10-02 against `5c0299a8`; nothing built. Rewritten four times that day as the
user's direction sharpened — this is the current ruling and supersedes every earlier shape (a
Tracktion built-in with eight seams; a shared DSP core with two adapters; a separate plugin
repository as a submodule). Facts are verified against the code and the vendored JUCE 8 /
Tracktion sources (paths under `external/tracktion_engine/modules/` unless they start with a
product folder). Re-verify before executing.

The user's direction, in order: make the Gain block loadable in the chain now; gain only, one
knob with a typed dB box; clean structure designed for use outside the app; the decisive point —
"parameters being automatable is a standard feature of VST3 plugins, why would we need Tracktion
for that? It should drop in clean as just a VST plugin without any adapter logic"; and finally
a top-level folder in THIS repo, kept to a strict JUCE-only dependency rule by convention like
every other library's rule, because the plugins are closely tied to this project and JUCE is
already here for free.

## The answer in one paragraph

The user is right on both counts. Tracktion's `ExternalPlugin` is already the adapter: it turns a
hosted `juce::AudioProcessor`'s parameters into `AutomatableParameter`s keyed by the processor's
own parameter ids (`tracktion_ExternalPlugin.cpp:798-804`), persists its state through
`getStateInformation`, opens its editor (`:2062`, `:2134`), and every RockHero path — lanes,
undo trackers, capture, export, restore, the window — already handles it. So the Gain block is
written ONCE as a plain JUCE processor in `rock-hero-plugins/gain/`, and RockHero hosts it
through `ExternalPlugin` with no Gain-specific code at all. The only question is where the
hosted instance COMES FROM, and JUCE offers a seam made for exactly this: a custom
`juce::AudioPluginFormat` that creates instances in-process from compiled-in code — the pattern
JUCE's own AudioPluginHost ships as `InternalPluginFormat`
(`juce/extras/AudioPluginHost/Source/Plugins/InternalPlugins.h:44`). No file is bundled, nothing
is scanned, nothing can go missing, the game gets it by linking `common/audio`, and the identity
is a fixed string, never a path. The same processor is wrapped as a VST3 by a `juce_add_plugin`
target behind a build option, for other DAWs, later. Keeping the folder in this repo deletes the
JUCE-injection machinery a separate repo would have needed: the hosted library simply links the
`rock_hero::juce_*` wrappers every other library links.

## Rulings

**R1. In-app hosting: our processors are hosted in-process through Tracktion's `ExternalPlugin`,
created by a RockHero `AudioPluginFormat`, never from a bundled file.** Three routes were weighed
for the insertable Gain block:

| | (A) Tracktion built-in, the eight seams | (B) bundled `.vst3` hosted like a third-party plugin | (C) compiled-in processor hosted through a RockHero `AudioPluginFormat` |
|---|---|---|---|
| Gain-specific code in RockHero | a Tracktion plugin type, an editor component, an identity authority, the generalisation of six refusals and four restores, catalog and insert branches (~10 files) | none | none |
| Shared plumbing in RockHero | — | UID-matching identity fix (prerequisite: bundled paths differ per install), startup registration, duplicate handling against a system-wide copy | one format class (~120 lines, ten trivial overrides), its registration, one catalog constant |
| Packaging | none | copy each `.vst3` beside the executable on three OSes (`.app/Contents/PlugIns` on macOS), installer rules, the game ships and registers the same bundle | none |
| Can it go missing | no | yes — surfaced like any missing plugin (`PluginNotFound`, the load refuses with the missing list, `engine_live_rig.cpp:1363-1375`), but a Gain block then silently drops out of every tone that used it | no |
| Explicit value stated twice (R1 of the earlier plan) | no | yes, as for every hosted plugin today | yes, as for every hosted plugin today — an existing property, not new |
| Later | the VST3 is a second implementation | the VST3 is the same binary | the VST3 wraps the same processor class |
| Size | ~3 days | ~4-6 days, mostly build and install on three OSes | ~2 days |

(C) wins on every row but one it shares with (B), and that row is the condition of every plugin
a tone already carries. Verified for (C): `AudioPluginFormat` has ten pure virtuals and a
`createPluginInstance` callback (`juce_audio_processors_headless/format/juce_AudioPluginFormat.h:
56-162`); `AudioPluginFormatManager::addFormat` (`juce_AudioPluginFormatManager.h:83`);
Tracktion's `PluginManager` exposes `pluginFormatManager` and a replaceable
`createPluginInstance` (`tracktion_PluginManager.h:51`, `:107`), and `ExternalPlugin` creates
through it (`tracktion_ExternalPlugin.cpp:1862`, `:1885`); its only format-conditioned branch
skips the default channel configuration for AU/VST3 (`:562`), so a hosted internal effect gets
the default stereo config, which is what Gain wants. The audio-path `processMutex.tryEnter`
(`:1394-1420`) and the explicit-value rule are what every hosted plugin already lives with.

**R2. One implementation per plugin, zero adapters.** A plugin is one `juce::AudioProcessor`
subclass plus its editor. RockHero adds no type, no helper, no core. The ±24 dB range appears in
the processor and, separately, in the tone's output stage (`common/audio/shared/gain.h`); these
are two rules that happen to agree — a generic gain plugin's range and how far a tone balance
may go — and nothing must keep them equal by hand.

**R3. The processor derives from `juce::AudioPluginInstance`, not `AudioProcessor`.** The only
difference is one override, `fillInPluginDescription` (`juce_AudioPluginInstance.h:70`), which
is also where the plugin states its name, vendor and unique id ONCE; the RockHero format reads
it, and the VST3 wrapper takes the same class through `createPluginFilter()` since an
`AudioPluginInstance` is an `AudioProcessor`. The alternative — JUCE's precedent wraps an
`AudioProcessor` in a forwarding `InternalPlugin` class (`InternalPlugins.cpp:88` onward, ~150
lines of pass-through) — is a second class with no hidden decision, so it is rejected.

**R4. The plugins live in THIS repo, as the top-level scope `rock-hero-plugins/`, one folder per
plugin, under a strict rule: a plugin links JUCE and nothing of RockHero.** Ruled 2026-10-02 over
the separate-repo shape after the user's review. The points, weighed:

- *Closely tied to this project.* Yes: the host format in `common/audio` names each plugin's
  class, the editor's knob look will be shared, and the first consumer of every plugin is the
  app. A submodule would make every one of those a two-repo change (commit there, bump here).
- *JUCE comes free.* In-repo, the hosted library links `rock_hero::juce_audio_processors` and
  `rock_hero::juce_gui_basics` exactly as every library does, so the whole JUCE-injection seam of
  the earlier shape (two cache variables, a `find_package`/`add_subdirectory` fallback, the
  no-JUCE-submodule caveat) is deleted. The VST3 target, when built, is in the same tree and
  compiles its own JUCE modules against the one vendored copy.
- *Agents commit in-repo.* The user's standing rule forbids agent commits in any other repo, so a
  submodule would have made every plugin change a hand commit by the user. In-repo, the normal
  process applies.
- *Maybe own releases, probably own licence.* Both are possible from one repo (R6, and the legal
  item below); neither requires a second repo now. The split trigger is stated in R6.

Namespace `rock_hero::plugins::gain`, three levels as the two-axis rule requires (product
`plugins`, library `gain`); include path `<rock_hero/plugins/gain/...>`; CMake target
`rock_hero_plugins_gain` with alias `rock_hero::plugins::gain`. It is a product scope in the
sense of `architectural-principles.md` "Product Scopes" (it ships to users on its own), with the
narrowest dependency rule in the tree.

**R5. The build is the enforcement; no new check.** The user is right that no library's
dependency rule has an automated check — they are kept by convention — and here CMake linkage
already enforces this one mechanically: the tree has NO global `include_directories()`,
`add_compile_options()`, `add_definitions()` or `link_libraries()` call (`rg` over the root,
`cmake/`, every product `CMakeLists.txt` and `project-config/`, 2026-10-02), and every library
exposes its headers only through `target_include_directories(... PUBLIC include)`. A target that
links no `rock_hero::common::*` target therefore has no `rock_hero/common/...` header on its
include path, so a stray `#include <rock_hero/common/...>` fails to compile and a stray call fails
to link. The rule is written in the plugin scope's `README.md` and the design documents (below)
the way the other libraries' rules are written, and nothing more.

The scope is a first-party library in every respect but its dependency rule (user ruling,
2026-10-02: "the same warning flags, lint rules, and possibly even presets"). In-tree it takes,
through the mechanisms the tree already uses:

- **Flags.** The hosted library links `rock_hero::build_policy`, the bundle every Rock Hero-owned
  target links (`cmake/RockHeroBuildPolicy.cmake:66-70`: `warning_flags`, `config_flags`,
  `lto_flags`, `warnings_as_errors`), so `/WX` and `-Werror` apply to our sources. The VST3
  target is the one place JUCE's own module sources compile inside a first-party target, and the
  tree's rule for that case is the wrappers' one (`RockHeroExternalModules.cmake:62-68`,
  `RockHeroBuildPolicy.cmake:48-50`: never `build_policy` on a target that compiles third-party
  sources): the target links `warning_flags`, `config_flags` and `lto_flags`, and
  `warnings_as_errors` is applied to our sources alone through
  `set_source_files_properties(${sources} PROPERTIES COMPILE_OPTIONS
  "$<TARGET_PROPERTY:rock_hero_warnings_as_errors,INTERFACE_COMPILE_OPTIONS>")`. Verify that
  genex under Ninja/MSVC first; if it misbehaves, the fallback is an OBJECT library of our
  sources compiled with the VST3 target's definitions and `build_policy`.
- **Lint.** The clang-tidy regex at `CMakeLists.txt:32` gains `rock-hero-plugins`, so the scope's
  translation units are in the `clang-tidy` target; the JUCE sources a VST3 target compiles stay
  out by path (`external/`). `.clang-tidy` applies by directory, so `readability-identifier-naming`
  binds the scope: `rock_hero::plugins::gain` is `lower_case` namespaces (`.clang-tidy:15`,
  `:245`), the include path `<rock_hero/plugins/gain/...>` mirrors it as every library's does,
  and types, functions and members follow the same table as everywhere.
- **Format and conventions.** `.clang-format` applies by directory; the pre-commit hooks
  (`cmake-format`, `clang-format`, `verify-project-conventions`) have no path filters, so they
  already cover the scope. The conventions script's root check enumerates a fixed product grid
  and should be generalized to `rock-hero-*/*/{include,src}` so the library-root rule binds here
  too.
- **Presets.** `CMakePresets.json` includes the `project-config` submodule's presets; the scope
  builds through them like everything else.

Standalone or extracted (later, light): the folder would add the same `project-config` submodule
for presets, Conan and lint, and a small `cmake/` of its own re-declaring the four flag targets
over JUCE's `juce_recommended_*` flags, which is all `RockHeroBuildPolicy.cmake` does. No C++
changes either way.

**R6. Releases from this repo; the split trigger.** When the VST3 option is on in a release
build, the bundle installs under a CPack `Plugins` component, archived separately from `Runtime`
through `PACKAGE_BUILDER_CPACK_COMPONENTS` and a `CPACK_PROJECT_CONFIG_FILE` that keeps NSIS, DEB
and DragNDrop on `Runtime` only, so the plugin zip appears beside the app on the same release
page with no workflow change (the release job uploads whatever CPack produced). A plugin-only
release cadence, if ever wanted, is a `plugins-v*` tag pattern driving the same job with the
option on. The trigger for `git subtree split -P rock-hero-plugins` into its own repository is a
need the one repo cannot meet: a CI pipeline (signing, notarization, pluginval on three OSes)
that must not gate the app's builds, or outside contributors who should not need the app's tree.
Because the folder links nothing of RockHero and has its own `CMakeLists.txt`, that split is a
history-preserving move plus the JUCE-acquisition CMake it does not need today; no C++ changes.

**R7. The tone's fixed output stage stays `ToneOutputPlugin`, a Tracktion built-in.** Making it a
hosted processor would delete that class (~250 lines) but would route the authored tone level
through the hosted explicit-value path, whose echo behaviour the juce-tracktion-expert flagged as
needing a probe, and pay `ExternalPlugin`'s per-block cost on every always-loaded branch. Not a
deletion without a correctness cost today. Candidate to revisit once the Gain block has shipped
through (C), at which point `LiveRigGainPlugin` is the same question.

**R8. The app's catalog shows our plugins through their own format name.** `knownPluginCatalog`
(`engine_plugin_host.cpp:346`, `:370`) accepts "VST3" only; it accepts our format's name too.
Scanning stays VST3-only (`vst3PluginFormat()`, `:219`). If a user also installs our VST3
system-wide for a DAW, the catalog hides VST3 entries whose manufacturer is ours, so "Gain"
appears once and always as the compiled-in instance.

**R9. The path-bound identity defect stays a backlog item, not a prerequisite.** The hosted
instance's `fileOrIdentifier` is the fixed string `"RockHero:gain"`, so every hash RockHero takes
of it (`juce_PluginDescription.cpp:48-52`, `tracktion_ExternalPlugin.cpp:14-18`) is identical on
every machine. The defect still bites third-party plugins and is recorded in
`docs/tracking/backlog.md` (2026-10-02).

## User decisions (each with a recommendation)

- **D1. The folder's licence** — a legal open item (below). Mechanics once counsel decides:
  `rock-hero-plugins/LICENSE`; the root `LICENSE` or `README.md` noting that the scope is
  licensed separately; `CONTRIBUTING.md` stating that contributions under that folder are under
  its licence; SPDX `SPDX-License-Identifier` lines in its sources only if counsel asks for them
  (the tree carries no per-file licence headers today). RockHero links the folder's code, so the
  folder's licence must be one an AGPLv3 work may incorporate; JUCE's licence applies to every
  plugin binary regardless.
- **D2. The permanent codes** (only the VST3 target reads them, but they also seed the hosted
  unique id): manufacturer `Rkhr`, plugin `Gain`. JUCE's rules: manufacturer code with at least
  one upper-case letter, plugin code with exactly one, first letter upper-case
  (`juce/docs/CMake API.md:517-525`). Never change.
- **D3. Names.** Processor name "Gain", vendor "Rock Hero", product name "Rock Hero Gain" for
  the VST3, bundle id `com.rockhero.gain`. "VST" stays out of names pending the trademark item.
- **D4. The knob's look.** Stock JUCE look-and-feel now (one `juce::Slider`). The app's knob
  drawing lives in `editor/ui` (`signal_chain_view.cpp`), which the plugin scope may not link;
  sharing it later means the plugin scope owns the look and the editor consumes it — a
  direction, not a step now.
- **D5. Where pluginval runs** (later, with the VST3): a ctest registered when `find_program`
  finds it, and an `install-pluginval` input in `ci-workflows` (the user's), as the earlier plan
  said.
- **D6. Shipping the VST3** (later): first as the separate release archive of R6; installer
  placement and signing after that, as the later phases say.

## The tree

```text
rock-hero-plugins/                            - NEW top-level scope, namespace rock_hero::plugins
  CMakeLists.txt                              - option(ROCK_HERO_BUILD_PLUGIN_VST3 OFF);
                                                add_subdirectory(gain)
  README.md                                   - the rule: JUCE wrappers only, nothing of RockHero;
                                                the two targets per plugin
  LICENSE                                     - D1, once counsel decides
  gain/                                       - namespace rock_hero::plugins::gain
    CMakeLists.txt                            - one source list; rock_hero_plugins_gain STATIC
                                                (hosted) linking rock_hero::juce_audio_processors,
                                                rock_hero::juce_gui_basics, rock_hero::build_policy;
                                                alias rock_hero::plugins::gain; when the option is
                                                on, juce_add_plugin(rock_hero_plugins_gain_vst3
                                                FORMATS VST3 ...) over the same list
    include/rock_hero/plugins/gain/plugin/gain_processor.h
                                              - juce::AudioPluginInstance: one AudioParameterFloat
                                                "gain", ±24 dB, 0 dB default, 5 ms SmoothedValue,
                                                XML state; fillInPluginDescription states name,
                                                vendor, format "RockHero", id "RockHero:gain"
    src/plugin/gain_processor.cpp
    src/plugin/gain_editor.{h,cpp}            - fixed-size editor: one rotary juce::Slider,
                                                TextBoxBelow, editable, " dB" suffix, one decimal,
                                                double-click → 0 dB, begin/endChangeGesture around
                                                drags and typed entry
    tests/CMakeLists.txt, tests/test_gain_processor.cpp
                                              - Catch2: a block at −6 dB halves; state round-trip;
                                                the parameter id is "gain"; the description's ids

CMakeLists.txt                                - add_subdirectory(rock-hero-plugins) before the
                                                products; the clang-tidy regex gains the folder
rock-hero-common/audio/CMakeLists.txt         - common::audio links rock_hero::plugins::gain PRIVATE
rock-hero-common/audio/src/tracktion/
  rock_hero_plugin_format.{h,cpp}             - NEW: juce::AudioPluginFormat "RockHero": the
                                                compiled-in table {identifier → factory}, one row
                                                per plugin; createPluginInstance; the ten trivial
                                                overrides (no scanning, no files)
rock-hero-common/audio/src/engine/engine.cpp  - after the Tracktion engine is built (:93):
                                                addFormat, then knownPluginList.addType for each
                                                description the format lists
rock-hero-common/audio/src/engine/engine_plugin_host.cpp:346,370
                                              - the catalog accepts the format's name; hides VST3
                                                entries whose manufacturer is ours (R8)
rock-hero-common/audio/tests/test_rock_hero_plugin_format.cpp - NEW
scripts/verify-project-conventions.py         - the root check enumerates a fixed grid; either
                                                generalize it to every rock-hero-*/*/{include,src}
                                                or leave the new scope unchecked (small either way)
```

Why `plugin/` as the folder inside the library: the plugin is one feature and the library-root
rule holds for every library, so its files take a folder named for the hub object they compose.
The header is public because the host format in `common/audio` includes it.

Nothing under `rock-hero-common/audio/include/` changes: the format is a private adapter unit,
exactly where `architectural-principles.md` "Framework-Adapter Units" puts a class that plugs
into a JUCE extension point. Tracktion and JUCE stay inside `common/audio` implementation files.

**The dependency direction**, stated as the architecture documents state the others:
`rock-hero-plugins` code must not depend on `common`, `editor` or `game` code; it may link the
`rock_hero::juce_*` wrappers and the build-policy flags only. `common/audio` may link
`rock-hero-plugins` hosted libraries (private, implementation only). Editor and game code reach
the plugins only through `common/audio`'s ports, never directly. The scope therefore sits BELOW
`common`, beside the externals in the dependency graph.

## The two targets per plugin

`gain/CMakeLists.txt` builds each plugin from ONE source list in two target kinds, so no source
is listed twice:

- always: `add_library(rock_hero_plugins_gain STATIC ${sources})` linking
  `rock_hero::juce_audio_processors`, `rock_hero::juce_gui_basics` and `rock_hero::build_policy`
  — the already-compiled wrappers (`cmake/RockHeroExternalModules.cmake`), so the processor
  compiles with exactly the module configuration every first-party library uses
  (`JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`, `JUCE_PLUGINHOST_VST3=1`, exported as INTERFACE
  definitions) and no JUCE source is compiled a second time. No `JucePlugin_*` definition is
  involved: no header under `juce_audio_processors*/processors/` references one (`rg`
  2026-10-02), so a processor library needs none and there is no ODR or config mismatch;
- when `ROCK_HERO_BUILD_PLUGIN_VST3` is on: `juce_add_plugin(rock_hero_plugins_gain_vst3 FORMATS
  VST3 ...)` with `target_sources(... PRIVATE ${sources})`, linking the raw `juce::juce_*`
  modules and the wrappers' non-strict flag set. The processor sources compile inside that
  target, where `JucePlugin_*` is defined (`JUCEUtils.cmake:1586-1615`), which is the only
  arrangement JUCE supports — a static library that links raw modules would compile the module
  sources into itself and clash with the plugin target's own copy. The hosted library and the
  VST3 never link together, so no ODR issue.

The option defaults OFF, so the app's build never builds or copies a bundle.

## Hard constraints

1. **Parameter ids are permanent strings and the list is append-only**: `ExternalPlugin` keys
   lanes on `AudioProcessorParameterWithID::paramID` (`tracktion_ExternalPlugin.cpp:801-802`),
   and VST3 hosts hash the same id. New parameters go at the end with the next
   `juce::ParameterID` version hint.
2. **The description's identifier and unique id never change**: `fileOrIdentifier`
   `"RockHero:gain"` and the unique id are what every saved tone and project resolves
   (`findMatchingPluginDescription`, `tracktion_ExternalPlugin.cpp:835-843`).
3. **The codes never change** (D2) once a VST3 ships.
4. **The scope depends on JUCE only**: no `rock_hero::common::*`, editor or game include or link
   (R5 is the enforcement).
5. **State is the processor's own XML**, read tolerantly, append-only; it is what the tone
   sidecar carries, through `ExternalPlugin` as for any plugin.
6. **Smoothing lives in the processor** (5 ms `SmoothedValue`), so it is identical hosted and as
   a VST3. The output stage's crossfade bound does not apply to a chain block.
7. **Audio-thread rules** are `ExternalPlugin`'s: no allocation in `processBlock`; the
   `SmoothedValue` and the parameter read are allocation-free.

## First deliverable: the Gain block in the app

Each step builds and its tests pass before the next; all in this repo.

1. **The scope and `GainProcessor`**: `rock-hero-plugins/{CMakeLists.txt,README.md}`, `gain/` with
   its CMake, the processor (R3) and its Catch2 test; the root `add_subdirectory` and the
   clang-tidy regex. A configure is the determinate reason to pass `-Configure`.
2. **The editor**: `gain_editor.{h,cpp}` as in the tree; the slider's `onDragStart`/`onDragEnd`
   and text-entry commit call `beginChangeGesture`/`endChangeGesture` on the parameter so the
   hosted gesture gate (`plugin_dirty_tracking.cpp`) produces one undo entry per drag or typed
   value.
3. **The format**: `rock_hero_plugin_format.{h,cpp}`; registration in `engine.cpp` after `:93`;
   `common::audio` links `rock_hero::plugins::gain`; the catalog constant and the vendor filter
   (R8). Tests: the catalog lists "Gain" with format "RockHero" and no file;
   `insertPluginCandidateToTrack` creates an `ExternalPlugin` whose instance is a
   `GainProcessor`; a chain holding it at −6 dB round-trips through save/load, export/import and
   the undo memento with no path in the identity; `hasKnownPluginForIdentity` accepts the saved
   identity on a fresh engine.
4. **Probe the hosted path once**: insert, drag the knob, undo, redo; open the window from the
   tile; a lane on `gain` plays; the game loads a tone containing it. The one risk of (C) is a
   Tracktion assumption about hosted instances that only a VST3 has met; this probe is where it
   would show.
5. **Sight** as the gain-block plan's sighting says.

**Size.** The scope: five source files, three CMake files, a README, a test (~400 lines). In
`common/audio`: one new adapter unit, three edits, one test file. About two days. Nothing in
`editor/`, `game/`, `common/core` or any public header of `common/audio` changes.

## Later phases (the VST3 for other DAWs)

**Phase V1 — the VST3 target.** `ROCK_HERO_BUILD_PLUGIN_VST3 ON` builds `Rock Hero Gain.vst3`
through `juce_add_plugin` (bundle layout `JUCEUtils.cmake:1237-1281`, `moduleinfo.json` from
`VST3_AUTO_MANIFEST`, no `COPY_PLUGIN_AFTER_BUILD`); the pluginval ctest of D5. Small to medium.

**Phase V2 — release archives** per R6: the `Plugins` component as its own zip per OS. Windows and
Linux ship unsigned (DAWs load an unsigned `.vst3`); macOS waits for V4.

**Phase V3 — installer placement**: an optional "install for other DAWs" component needs
`cmake-package-builder` (the user's) to grow a components page and a section writing to
`$COMMONFILES64\VST3`; Linux DEB to `lib/vst3/`; macOS after V4. Medium, outside this repo.

**Phase V4 — signing and notarization**: Apple Developer ID + notarytool, Windows Authenticode,
secrets in `ci-workflows`. Medium; purchases. If this pipeline must not gate the app, R6's split
trigger has fired.

**Phase V5 — optional**: AU after V4; CLAP only with `clap-juce-extensions` as a dependency the
user approves; a second plugin (one folder in the scope, one row in the format's table); the
output stage and `LiveRigGainPlugin` as hosted processors if R7's probe clears them.

## Risks

- **Tracktion hosting a non-VST3 instance**: only the channel-config branch
  (`tracktion_ExternalPlugin.cpp:562`) is format-conditioned; step 4 probes the rest.
- **The known-plugin list is persisted by Tracktion**: our types are re-added at every start and
  `addType` replaces duplicates; a stale persisted row cannot resolve to a file because the
  format reports `doesPluginStillExist` from its table.
- **The JUCE splash screen** in the plugin's editor under JUCE's AGPL grant — settle with the
  licence item before the VST3 ships.
- **A VST3 target in the app's tree** compiles JUCE modules a second time when the option is on;
  it is OFF by default and CI leaves it off until V1.

## Open legal items (for the user, perhaps the ip-legal-expert; not decided here)

1. **The folder's licence** (D1): which licence, whether an AGPLv3 repository may carry a
   differently licensed subfolder that the AGPL work links, and JUCE's AGPLv3-or-EULA terms for
   plugin binaries (`juce/LICENSE.md:6-8`), including the splash-screen rule.
2. **AGPL plugins inside closed hosts**: whether a `.vst3` under an AGPL-family licence loaded in
   a proprietary DAW forms a combined work, and what the download page must say.
3. **The "VST" mark**: the SDK is MIT (`juce_audio_processors_headless/format_types/VST3_SDK/
   LICENSE.txt`), the name and logo remain Steinberg trademarks with usage rules (its
   `README.md:256-267`); whether "VST3" may appear in a product name or page is unverified.

## Design-document changes for the user to approve

- `docs/design/architecture.md` "Repository Layout" and `CLAUDE.md` "Architecture": add
  `rock-hero-plugins/` ("our JUCE plugins: each a `juce::AudioProcessor` and its editor, hosted
  in-process by `common/audio` and buildable as VST3") to the tree, and to the dependency rules:
  "`rock-hero-plugins` code must not depend on `common`, `editor` or `game` code; it links the
  `rock_hero::juce_*` wrappers only. `common/audio` may link `rock-hero-plugins` libraries
  privately. Editor and game code never link them directly."
- `docs/design/architectural-principles.md` "Product Scopes": the fourth scope with that rule,
  and under "Third-Party Module Linkage" that a plugin's VST3 target is the one place a raw
  `juce::` module is linked outside `cmake/RockHeroExternalModules.cmake`, because
  `juce_add_plugin` must compile the modules with its own definitions.

## Relation to other records

- `docs/plans/todo/gain-block-and-signal-chain-panel.md` — its Phase 1 is this first deliverable;
  the eight seams there are the rejected route (A) and are kept only as the record of why.
- `docs/tracking/backlog.md` (2026-10-02) — the path-bound identity defect (R9).
- `docs/tracking/backlog.md` "standard tones built only from plugins we ship" — this is the first
  plugin that could back such a tone, with no file dependency.
