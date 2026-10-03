# Our plugins: a JUCE-only plugin repo, hosted in-process by RockHero, shipped as VST3 later

Status: planned 2026-10-02 against `5c0299a8`; nothing built. Rewritten three times that day as the
user's direction sharpened — this is the current ruling and supersedes every earlier shape (a
Tracktion built-in with eight seams; a shared DSP core with two adapters). Facts are verified
against the code and the vendored JUCE 8 / Tracktion sources (paths under
`external/tracktion_engine/modules/` unless they start with a product folder). Re-verify before
executing.

The user's direction, in order: make the Gain block loadable in the chain now; gain only, one
knob with a typed dB box; clean structure designed for use outside the app; its own repo that
depends only on JUCE, with JUCE injectable so RockHero builds it from its one vendored copy;
and — the decisive point — "parameters being automatable is a standard feature of VST3 plugins,
why would we need Tracktion for that? It should drop in clean as just a VST plugin without any
adapter logic."

## The answer in one paragraph

The user is right: Tracktion's `ExternalPlugin` is already the adapter. It turns a hosted
`juce::AudioProcessor`'s parameters into `AutomatableParameter`s keyed by the processor's own
parameter ids (`tracktion_ExternalPlugin.cpp:798-804`), persists its state through
`getStateInformation`, opens its editor (`:2062`, `:2134`), and every RockHero path — lanes,
undo trackers, capture, export, restore, the window — already handles it. So the Gain block is
written ONCE as a plain JUCE processor in a JUCE-only plugin repo, and RockHero hosts it through
`ExternalPlugin` with no Gain-specific code at all. The only question is where the hosted
instance COMES FROM, and there JUCE offers a seam made for exactly this: a custom
`juce::AudioPluginFormat` that creates instances in-process from compiled-in code — the pattern
JUCE's own AudioPluginHost ships as `InternalPluginFormat`
(`juce/extras/AudioPluginHost/Source/Plugins/InternalPlugins.h:44`). With that, no file is
bundled, nothing is scanned, nothing can go missing, the game gets it by linking `common/audio`,
and the identity is a fixed string, never a path. The same processor is wrapped as a VST3 by a
`juce_add_plugin` target in the plugin repo for other DAWs, later.

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
a tone already carries. (B)'s objections from the first version of this plan were general
missing-plugin concerns, not Gain-specific, but (C) makes them moot rather than merely general.
Verified for (C): `AudioPluginFormat` has ten pure virtuals and a `createPluginInstance` callback
(`juce_audio_processors_headless/format/juce_AudioPluginFormat.h:56-162`);
`AudioPluginFormatManager::addFormat` (`juce_AudioPluginFormatManager.h:83`); Tracktion's
`PluginManager` exposes `pluginFormatManager` and a replaceable `createPluginInstance`
(`tracktion_PluginManager.h:51`, `:107`), and `ExternalPlugin` creates through it
(`tracktion_ExternalPlugin.cpp:1862`, `:1885`); its only format-conditioned branch skips the
default channel configuration for AU/VST3 (`:562`), so a hosted internal effect gets the
default stereo config, which is what Gain wants. The audio-path `processMutex.tryEnter`
(`:1394-1420`) and the explicit-value rule are what every hosted plugin already lives with.

**R2. One implementation per plugin, zero adapters.** A plugin is one `juce::AudioProcessor`
subclass plus its editor, in the plugin repo. RockHero adds no type, no helper, no core. The
earlier "one rule, two adapters" and "two types sharing a core" are withdrawn; the shared "gain
core" is deleted from the plan because the processor IS the one statement of the gain rule, and
JUCE's `SmoothedValue` and `Decibels` are available to it (the repo depends on JUCE). The ±24 dB
range appears in the processor and, separately, in the tone's output stage
(`common/audio/shared/gain.h`); these are two rules that happen to agree — a generic gain
plugin's range and how far a tone balance may go — and nothing must keep them equal by hand.

**R3. The processor derives from `juce::AudioPluginInstance`, not `AudioProcessor`.** The only
difference is one override, `fillInPluginDescription`
(`juce_AudioPluginInstance.h:70`), which is also where the plugin states its name, vendor and
unique id ONCE; the RockHero format reads it, and the VST3 wrapper takes the same class through
`createPluginFilter()` since an `AudioPluginInstance` is an `AudioProcessor`. The alternative —
JUCE's precedent wraps an `AudioProcessor` in a forwarding `InternalPlugin` class
(`InternalPlugins.cpp:88` onward, ~150 lines of pass-through) — is a second class with no hidden
decision, so it is rejected.

**R4. The plugin repo is its own repository, created NOW, mounted as the submodule
`external/rock-hero-plugins/`.** The user's direction, and it follows the two precedents already
wired (`.gitmodules`: `external/tracktion_engine`, `project-config`; CI checks out recursively;
the build helper initialises recursively). It depends only on JUCE; it never includes a
`rock_hero::` header or links a `rock_hero::` library. Namespace `rock_hero_plugins::<plugin>`
(its own root, deliberately outside the app's `rock_hero::<product>::<library>` grid, since it is
not a product of this repo), include path `<rock_hero_plugins/<plugin>/...>`, CMake targets
`rock_hero_plugins::<plugin>`. Costs accepted with the direction: every plugin change is a
commit in the plugin repo by the USER (agents never commit outside RockHero; they leave the
working tree ready) followed by a submodule bump here; version pinning is the gitlink.

**R5. JUCE is injected, never fetched, when RockHero builds it.** The seam is in the plugin repo's
root `CMakeLists.txt` and is two variables, below. RockHero sets them before
`add_subdirectory(external/rock-hero-plugins)`; standalone, the repo finds or adds its own JUCE
and builds the VST3 targets. The plugin repo must NOT carry JUCE as a git submodule: RockHero's
recursive init would fetch a second JUCE tree. Standalone JUCE comes from `find_package(JUCE)`
or a `ROCK_HERO_PLUGINS_JUCE_DIR` the builder points at.

**R6. The tone's fixed output stage stays `ToneOutputPlugin`, a Tracktion built-in.** Making it a
hosted processor would delete that class (~250 lines) but would route the authored tone level
through the hosted explicit-value path, whose echo behaviour the juce-tracktion-expert flagged as
needing a probe, and pay `ExternalPlugin`'s per-block cost on every always-loaded branch. Not a
deletion without a correctness cost today. Candidate to revisit once the Gain block has shipped
through (C) and the hosted path is proven, at which point `LiveRigGainPlugin` is the same
question.

**R7. The app's catalog shows our plugins through their own format name.** `knownPluginCatalog`
(`engine_plugin_host.cpp:346`, `:370`) accepts "VST3" only; it accepts our format's name too.
Scanning stays VST3-only (`vst3PluginFormat()`, `:219`). If a user also installs our VST3
system-wide for a DAW, the catalog hides VST3 entries whose manufacturer is ours, so "Gain"
appears once and always as the compiled-in instance (nothing a tone saves can then depend on a
file).

**R8. The path-bound identity defect stays a backlog item, not a prerequisite.** The hosted
instance's `fileOrIdentifier` is the fixed string `"RockHero:gain"`, so every hash RockHero
takes of it (`juce_PluginDescription.cpp:48-52`, `tracktion_ExternalPlugin.cpp:14-18`) is
identical on every machine. The defect still bites third-party plugins and is recorded in
`docs/tracking/backlog.md` (2026-10-02).

## User decisions (each with a recommendation)

- **D1. Repository name and licence.** Recommend `rock-hero-plugins` under the same GitHub
  account. Its licence is a legal open item (below): RockHero is AGPLv3 and consumes it, so
  anything AGPL-compatible works for RockHero; JUCE's licence applies to every plugin binary
  regardless.
- **D2. The permanent codes** (only the VST3 target reads them, but they also seed the hosted
  unique id): manufacturer `Rkhr`, plugin `Gain`. JUCE's rules: manufacturer code with at least
  one upper-case letter, plugin code with exactly one, first letter upper-case
  (`juce/docs/CMake API.md:517-525`). Never change.
- **D3. Names.** Processor name "Gain", vendor "Rock Hero", product name "Rock Hero Gain" for
  the VST3, bundle id `com.rockhero.gain`. "VST" stays out of names pending the trademark item.
- **D4. The knob's look.** Stock JUCE look-and-feel now (one `juce::Slider`); the app's knob
  drawing lives in `editor/ui` (`signal_chain_view.cpp`) and cannot be reached from a JUCE-only
  repo, so sharing it means lifting that drawing into the plugin repo's own look-and-feel later
  and having the editor consume it — a direction, not a step now.
- **D5. Where pluginval runs** (later, with the VST3): in the plugin repo's own CI, not RockHero's.
- **D6. Shipping the VST3** (later): first as a release archive from the plugin repo's releases;
  installer placement and signing after that, as the later phases say.

## The trees

**The plugin repo** (`rock-hero-plugins`, mounted at `external/rock-hero-plugins/`):

```text
rock-hero-plugins/
  CMakeLists.txt                      - project(); the JUCE seam (R5); options;
                                        add_subdirectory(gain)
  cmake/RockHeroPluginsAddPlugin.cmake - ONE function: a source set, the hosted library, the VST3
  LICENSE                             - D1 (legal open item)
  README.md                           - what a plugin here may link (JUCE only), the two build modes
  gain/
    CMakeLists.txt                    - rock_hero_plugins_add_plugin(gain PLUGIN_CODE Gain ...)
    include/rock_hero_plugins/gain/gain_processor.h   - juce::AudioPluginInstance: one
                                                        AudioParameterFloat "gain", ±24 dB, 0 dB
                                                        default, 5 ms SmoothedValue, XML state
    src/gain_processor.cpp
    src/gain_editor.{h,cpp}           - fixed-size editor: one rotary juce::Slider, TextBoxBelow,
                                        editable, " dB" suffix, one decimal, double-click → 0 dB,
                                        begin/endChangeGesture around drags and typed entry
    tests/test_gain_processor.cpp     - Catch2: a block at −6 dB halves; state round-trip; the
                                        parameter id is "gain"; the description's ids are fixed
```

**RockHero** (what changes here, all in `common/audio`):

```text
.gitmodules, external/rock-hero-plugins/      - the submodule
CMakeLists.txt                                - sets the two seam variables, then
                                                add_subdirectory(external/rock-hero-plugins)
rock-hero-common/audio/CMakeLists.txt         - common::audio links rock_hero_plugins::gain PRIVATE
rock-hero-common/audio/src/tracktion/
  rock_hero_plugin_format.{h,cpp}             - NEW: juce::AudioPluginFormat "RockHero": the
                                                compiled-in table {identifier → factory}, one
                                                row per plugin; createPluginInstance; the ten
                                                trivial overrides (no scanning, no files)
rock-hero-common/audio/src/engine/engine.cpp  - after the Tracktion engine is built (:93):
                                                addFormat, then knownPluginList.addType for each
                                                description the format lists
rock-hero-common/audio/src/engine/engine_plugin_host.cpp:346,370 - the catalog accepts the
                                                format's name; hides VST3 entries whose
                                                manufacturer is ours (R7)
rock-hero-common/audio/tests/test_rock_hero_plugin_format.cpp - NEW
```

Nothing under `rock-hero-common/audio/include/` changes: the format is a private adapter unit,
exactly where `architectural-principles.md` "Framework-Adapter Units" puts a class that plugs
into a JUCE extension point. Tracktion and JUCE stay inside `common/audio` implementation files.
The dependency direction is `rock_hero_plugins::gain` ← `common/audio` ← editor, game: the
plugin repo sits with the other externals, below `common`, and knows nothing above it.

## The CMake seam

In the plugin repo's root `CMakeLists.txt`:

```cmake
# Consumer-injected JUCE. RockHero sets both before add_subdirectory(); standalone leaves them
# empty and the repo finds or adds its own JUCE and builds the VST3 bundles.
set(ROCK_HERO_PLUGINS_JUCE_DIR "" CACHE PATH "A JUCE source tree to add_subdirectory()")
set(ROCK_HERO_PLUGINS_HOST_JUCE_TARGETS "" CACHE STRING
    "Already-built JUCE module targets to link the hosted libraries against (consumer-provided)")
option(ROCK_HERO_PLUGINS_BUILD_VST3 "Build the VST3 bundles" ${PROJECT_IS_TOP_LEVEL})

if(NOT ROCK_HERO_PLUGINS_HOST_JUCE_TARGETS AND NOT TARGET juce::juce_audio_processors)
    if(ROCK_HERO_PLUGINS_JUCE_DIR)
        add_subdirectory("${ROCK_HERO_PLUGINS_JUCE_DIR}" juce)
    else()
        find_package(JUCE CONFIG REQUIRED)
    endif()
endif()
```

`rock_hero_plugins_add_plugin(<name> ...)` builds each plugin from ONE source list in two target
kinds, so no source is ever listed twice:

- an INTERFACE source set (`target_sources(... INTERFACE ...)` plus include dirs), as JUCE's own
  modules propagate their sources;
- when `ROCK_HERO_PLUGINS_HOST_JUCE_TARGETS` is set: `add_library(rock_hero_plugins_<name>
  STATIC)` linking the source set and those targets — in RockHero,
  `rock_hero::juce_audio_processors` and `rock_hero::juce_gui_basics`, the already-compiled
  wrappers (`cmake/RockHeroExternalModules.cmake`), so the processor compiles with exactly the
  module configuration every first-party library uses (`JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`,
  `JUCE_PLUGINHOST_VST3=1`, exported as INTERFACE definitions) and no JUCE source is compiled a
  second time. No `JucePlugin_*` definition is involved: no header under
  `juce_audio_processors*/processors/` references one (`rg` 2026-10-02), so a processor library
  needs none and there is no ODR or config mismatch;
- when `ROCK_HERO_PLUGINS_BUILD_VST3` is on: `juce_add_plugin(rock_hero_plugins_<name>_vst3
  FORMATS VST3 ...)` linking the source set and the raw `juce::juce_*` modules. The processor
  sources compile inside that target, where `JucePlugin_*` is defined
  (`JUCEUtils.cmake:1586-1615`), which is the only arrangement JUCE supports — a static library
  that links raw modules would compile the module sources into itself and clash with the plugin
  target's own copy.

In RockHero: `ROCK_HERO_PLUGINS_BUILD_VST3 OFF`, so no bundle is ever built or copied in the app's
build; the hosted library is all the app consumes.

## Hard constraints

1. **Parameter ids are permanent strings and the list is append-only**: `ExternalPlugin` keys
   lanes on `AudioProcessorParameterWithID::paramID` (`tracktion_ExternalPlugin.cpp:801-802`),
   and VST3 hosts hash the same id. New parameters go at the end with the next
   `juce::ParameterID` version hint.
2. **The description's identifier and unique id never change**: `fileOrIdentifier`
   `"RockHero:gain"` and the unique id are what every saved tone and project resolves
   (`findMatchingPluginDescription`, `tracktion_ExternalPlugin.cpp:835-843`).
3. **The codes never change** (D2) once a VST3 ships.
4. **The plugin repo depends only on JUCE**; no `rock_hero::` include or link, enforced by its
   standalone build.
5. **State is the processor's own XML**, read tolerantly, append-only; it is what the tone
   sidecar carries, through `ExternalPlugin` as for any plugin.
6. **Smoothing lives in the processor** (5 ms `SmoothedValue`), so it is identical hosted and as
   a VST3. The output stage's crossfade bound does not apply to a chain block.
7. **Audio-thread rules** are `ExternalPlugin`'s: no allocation in `processBlock`; the
   `SmoothedValue` and the parameter read are allocation-free.

## First deliverable: the Gain block in the app

Each step builds and its tests pass before the next. Steps 1-2 are in the plugin repo (agents
prepare the working tree; the user commits and pushes); steps 3-6 are in RockHero.

0. **The user creates the empty `rock-hero-plugins` repository** (D1) and the agent clones it
   at `external/rock-hero-plugins` (the gitlink is added in step 3).
1. **The plugin repo skeleton and `GainProcessor`**: root `CMakeLists.txt` with the seam, the one
   `add_plugin` function, `gain/` with the processor (R3; `fillInPluginDescription` states name,
   vendor, format name "RockHero", identifier `"RockHero:gain"`, unique id) and its Catch2 test.
   Verified standalone with `ROCK_HERO_PLUGINS_BUILD_VST3 OFF` and a `find_package(JUCE)` or a
   `ROCK_HERO_PLUGINS_JUCE_DIR` pointed at RockHero's vendored JUCE
   (`external/tracktion_engine/modules/juce`).
2. **The editor**: `gain_editor.{h,cpp}` as in the tree; the slider's `onDragStart`/`onDragEnd`
   and text-entry commit call `beginChangeGesture`/`endChangeGesture` on the parameter so the
   hosted gesture gate (`plugin_dirty_tracking.cpp`) produces one undo entry per drag or typed
   value.
3. **Mount and build in RockHero**: `.gitmodules`, the two seam variables and
   `add_subdirectory` in the root `CMakeLists.txt`, `common::audio` links
   `rock_hero_plugins::gain`. A configure is the determinate reason to pass `-Configure`.
4. **The format**: `rock_hero_plugin_format.{h,cpp}`; registration in `engine.cpp` after `:93`;
   the catalog constant and the vendor filter (R7). Tests: the catalog lists "Gain" with format
   "RockHero" and no file; `insertPluginCandidateToTrack` creates an `ExternalPlugin` whose
   instance is a `GainProcessor`; a chain holding it at −6 dB round-trips through save/load,
   export/import and the undo memento with no path in the identity; `hasKnownPluginForIdentity`
   accepts the saved identity on a fresh engine.
5. **Probe the hosted path once**: insert, drag the knob, undo, redo; open the window from the
   tile; a Volume-style lane on `gain` plays; the game loads a tone containing it. The one risk
   of (C) is a Tracktion assumption about hosted instances that only a VST3 has met; this probe
   is where it would show.
6. **Sight** as the gain-block plan's sighting says.

**Size.** Plugin repo: five source files, two CMake files, a README, a test (~400 lines). RockHero:
one new adapter unit, three edits, one test file. About two days plus the user's repository
bootstrap. Nothing in `editor/`, `game/` or `common/core` changes.

## Later phases (the VST3 for other DAWs)

**Phase V1 — the VST3 target.** In the plugin repo: `ROCK_HERO_PLUGINS_BUILD_VST3 ON` standalone
builds `Rock Hero Gain.vst3` through `juce_add_plugin` (bundle layout `JUCEUtils.cmake:1237-1281`,
`moduleinfo.json` from `VST3_AUTO_MANIFEST`, no `COPY_PLUGIN_AFTER_BUILD`). The repo's own CI
builds it on three OSes and runs pluginval (D5). Small to medium.

**Phase V2 — release archives** from the plugin repo's releases, one zip per OS. Windows and
Linux ship unsigned (DAWs load an unsigned `.vst3`); macOS waits for V4.

**Phase V3 — installer placement**: an optional "install for other DAWs" component in RockHero's
installer needs `cmake-package-builder` (the user's) to grow a components page and a section
writing to `$COMMONFILES64\VST3`; Linux DEB to `lib/vst3/`; macOS after V4. Medium, outside this
repo.

**Phase V4 — signing and notarization**: Apple Developer ID + notarytool, Windows Authenticode,
secrets in the plugin repo's CI. Medium; purchases.

**Phase V5 — optional**: AU after V4; CLAP only with `clap-juce-extensions` as a dependency the
user approves; a second plugin (one folder in the plugin repo, one row in the format's table);
the output stage and `LiveRigGainPlugin` as hosted processors if R6's probe clears them.

## Risks

- **Tracktion hosting a non-VST3 instance**: only the channel-config branch
  (`tracktion_ExternalPlugin.cpp:562`) is format-conditioned; step 5 probes the rest.
- **The known-plugin list is persisted by Tracktion**: our types are re-added at every start and
  `addType` replaces duplicates; a stale persisted row cannot resolve to a file because the
  format reports `doesPluginStillExist` from its table.
- **The JUCE splash screen** in the plugin's editor under JUCE's AGPL grant — settle with the
  licence item before the VST3 ships.
- **Two JUCE configurations**: the hosted library compiles against RockHero's wrapper
  definitions; the VST3 against `juce_add_plugin`'s. They are different targets in different
  builds and never link together, so no ODR issue; the processor code must not depend on a
  module flag that differs between them (none does today).

## Open legal items (for the user, perhaps the ip-legal-expert; not decided here)

1. **The plugin repo's licence** (D1) and JUCE's AGPLv3-or-EULA terms for plugin binaries
   (`juce/LICENSE.md:6-8`), including the splash-screen rule.
2. **AGPL plugins inside closed hosts**: whether a `.vst3` under an AGPL-family licence loaded in
   a proprietary DAW forms a combined work, and what the download page must say.
3. **The "VST" mark**: the SDK is MIT (`juce_audio_processors_headless/format_types/VST3_SDK/
   LICENSE.txt`), the name and logo remain Steinberg trademarks with usage rules (its
   `README.md:256-267`); whether "VST3" may appear in a product name or page is unverified.

## Design-document changes for the user to approve

- `docs/design/architecture.md` "Repository Layout" and `CLAUDE.md` "Architecture": add
  `external/rock-hero-plugins/` (our JUCE-only plugin repo as a submodule) and the rule that
  `common/audio` may link its hosted libraries while nothing in it may depend on RockHero.
- `docs/design/architectural-principles.md` "Third-Party Module Linkage": record that the plugin
  repo's hosted libraries are built against the `rock_hero::juce_*` wrappers through the injected
  `ROCK_HERO_PLUGINS_HOST_JUCE_TARGETS`, so the one-copy rule holds across the submodule.

## Relation to other records

- `docs/plans/todo/gain-block-and-signal-chain-panel.md` — its Phase 1 is this first deliverable;
  the eight seams there are the rejected route (A) and are kept only as the record of why.
- `docs/tracking/backlog.md` (2026-10-02) — the path-bound identity defect (R8).
- `docs/tracking/backlog.md` "standard tones built only from plugins we ship" — this is the first
  plugin that could back such a tone, with no file dependency.
