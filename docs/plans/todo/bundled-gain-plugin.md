# The Gain plugin: one rule, two adapters, shipped two ways

Status: planned 2026-10-02 against `5c0299a8`; nothing built. Facts re-verified that day against the
code and the vendored JUCE 8 / Tracktion sources (paths below are under `external/tracktion_engine/`
unless they start with a product folder). Re-verify before executing.

The user's request: make the Gain block loadable in the chain now, have it ship with the app so
every user can load it, and make it modular so people can use our plugins in their own DAWs.

## First deliverable: the in-app Gain block, with nothing distributed

Ruled 2026-10-02 after the user asked for the simplest path to "insert it like a normal VST in
our signal chain" first. That path is Phase 1 of `gain-block-and-signal-chain-panel.md`, under
its heading "The simplest path, ordered": one layer, no format change, two to three days, and no
dependency on anything below. The shared `GainStage` core (Phase 1 here) is NOT a prerequisite —
it deletes nothing on that path — and the catalog filter (R5) waits until a VST3 of ours exists.
Everything below this line is LATER work, in the order given.

## The answer in one paragraph

Inside the app the Gain block is a Tracktion **built-in**: it appears in the plugin picker with no
file on disk, no scan, and no way to be missing, which is a stronger form of "ships with the app"
than any bundled binary. That is Phase 1 of `gain-block-and-signal-chain-panel.md`, unchanged. For
other DAWs we build a **VST3** from the same DSP core in a new product, `rock-hero-plugins/`, and
ship it first as a download beside the app on the release page, later as an installer option. The
app never hosts its own VST3. The DSP rule lives once, in a JUCE-free core both adapters call.

Simple parts: the core and the built-in (small), the VST3 target itself (medium, JUCE does the
work). Not simple: signing and notarization, and installer placement into the system VST3 folder,
which need purchases, secrets and changes in two tooling repositories the user owns.

## Rulings

**R1. The in-app block stays a Tracktion built-in; the app does not host our VST3.** The ruling of
2026-10-02 in the gain-block plan stands. Hosting our own VST3 as an `ExternalPlugin` would:

- pass audio through at unity, silently, whenever the bundle is missing or fails to load
  (`modules/tracktion_engine/plugins/external/tracktion_ExternalPlugin.cpp:1375-1379`:
  `applyToBuffer` returns before touching the buffer when no instance loaded), so an authored level
  would vanish in editor and game alike with no error;
- make the game depend on a file beside the executable and on plugin registration at startup;
- store the explicit value in two places that must agree — the plugin's state chunk and
  Tracktion's explicit-value save (`tracktion_AutomatableEditItem.cpp:305-343`);
- bind the tone document to a plugin identity that is path-bound today (R6);
- pay `ExternalPlugin`'s per-block hosting cost on every always-loaded branch for a two-multiply
  stage.

**R2. "One type" re-ruled as one RULE, two ADAPTERS.** Within the app the earlier ruling is intact:
the output stage and the chain Gain block are both `ToneOutputPlugin` instances. What changes is
what that class is: an adapter over a shared core, `GainStage`, which owns everything that is a
rule — the ranges, the dB law, the balanced pan law, the ramp, and the parameter table. The VST3's
`juce::AudioProcessor` is the second adapter over the same core. Nothing is stated twice: not the
pan law (restated today at `rock-hero-common/audio/src/tracktion/tone_output_plugin.cpp:103-115`
because `juce::dsp::Panner` hides its gains), not the parameter ids, not the vendor name.

**R3. The core is a JUCE-free sub-library INSIDE `common/audio`, not a new `common/dsp` library.**
`rock-hero-common/audio/CMakeLists.txt` already builds `rock_hero::common::audio_calibration` from
two of the audio library's own sources with no JUCE link, "so the user guide's generator links the
one copy". The gain core follows that precedent exactly: `rock_hero::common::audio_gain_stage`,
built from `shared/gain.h`, `shared/pan.h` + `src/shared/pan.cpp`, `shared/tone_output.h` and the
new `gain_stage/` files, linking only `rock_hero::build_policy`. The audio library links it
PUBLIC, so every existing consumer of `Gain`, `Pan` and `ToneOutput` keeps its namespace and
include path. A `common/dsp` library would rename three value types across editor, game and tests
for a navigation gain nobody asked for, and would need the conventions script, both architecture
documents and `CLAUDE.md` to learn a fourth library kind.

**R4. A new product scope, `rock-hero-plugins/`, one folder per plugin.** Namespace
`rock_hero::plugins::gain`, three levels deep as the two-axis rule requires (product `plugins`,
library `gain`). It is a product because it ships to users on its own and must obey the product
dependency rules: it may link `common` (the core sub-library only), never editor or game. The
folder is `rock-hero-plugins/gain/`, not `rock-hero-gain/`, because the second plugin will want the
same home and the same CMake and CI wiring.

**R5. The app's catalog hides our own VST3s.** A user who installs the VST3 system-wide for their
DAW must still see exactly one "Gain" in the picker, the built-in; otherwise a tone could carry the
hosted copy with every defect in R1. `knownPluginCatalog`
(`rock-hero-common/audio/src/engine/engine_plugin_host.cpp:344`) skips VST3 descriptions whose
`manufacturerName` is `g_plugin_vendor_name`, the core's one vendor constant that the Tracktion
adapter's `getVendor()` and the VST3's `JucePlugin_Manufacturer` both equal (checked by a
`static_assert` in the processor TU, see Phase 2).

**R6. The path-bound plugin identity is a separate backlog fix, not a prerequisite.** Confirmed:
`hasKnownPluginForIdentity` (`engine_plugin_host.cpp:473-503`) checks two hints and then
`isDuplicateOf`, and all three carry the install path — both hints hash `fileOrIdentifier`
(`juce_PluginDescription.cpp:48-52`, `tracktion_ExternalPlugin.cpp:14-18`) and `isDuplicateOf`
ties it (`juce_PluginDescription.cpp:38-46`). A tone whose VST3 lives in a different folder on
another machine is refused with `PluginNotFound` even when the catalog knows the plugin. Tracktion's
own restore would have succeeded through `deprecatedUid`
(`tracktion_ExternalPlugin.cpp:840-843`), so the fix is the gate alone: match `uniqueId`, then
`deprecatedUid`, before any path-bearing key. Neither the built-in nor the VST3-for-other-DAWs
needs it; it is recorded in `docs/tracking/backlog.md` (2026-10-02) because it hits every
third-party plugin a `.tone` file carries between machines.

**R7. Formats: VST3 now; AU after macOS signing exists; CLAP only with a dependency the user
approves.** The VST3 SDK is vendored (3.8.0, MIT) and `juce_add_plugin` builds it. AU is
macOS-only and worthless unsigned. CLAP is not in JUCE; it needs `clap-juce-extensions`, a new
submodule or Conan package, which `CLAUDE.md` says to propose, not add.

## User decisions (each with a recommendation)

- **D1. The four-character codes, fixed forever.** JUCE requires a manufacturer code with at least
  one upper-case letter and a plugin code with exactly one, first letter upper-case for GarageBand
  (`modules/juce/docs/CMake API.md:517-525`). The VST3 class UID is derived from exactly these two
  codes (`juce_VST3ModuleInfo.h:61`), so changing either orphans every DAW project that used the
  plugin. Recommend manufacturer `Rkhr`, plugin `Gain`.
- **D2. Names.** Product name "Rock Hero Gain", vendor "Rock Hero", bundle id
  `com.rockhero.gain`. Whether "VST" may appear in any product name is a legal open item below;
  the recommendation keeps it out of the name and uses it only descriptively ("a VST3 plugin").
- **D3. Where pluginval runs in CI.** The build goes through `tnt-coders/ci-workflows`, which
  offers no step injection (verified 2026-10-02 against its `cpp-cmake-conan-build.yml`).
  Recommend adding an `install-pluginval` input there (the user owns that repository) and
  registering the validation as a ctest in this tree (Phase 2); the alternative, a second workflow
  in this repository that configures and builds just the plugin, doubles the Conan and JUCE cost
  on three runners.
- **D4. How the VST3 ships first.** Recommend a separate release archive per OS (Phase 3) before
  any installer work; it is what most free plugins ship as, and it needs nothing outside this
  repository. Installer placement (Phase 4) needs `cmake-package-builder` changes.
- **D5. Signing purchases and when.** macOS distribution needs an Apple Developer ID ($99/yr) and
  notarization; without it a downloaded `.vst3` is quarantined and most users will not get past
  Gatekeeper. Windows DAWs load an unsigned `.vst3` without complaint (SmartScreen checks
  installers, not DLLs a DAW opens), so Authenticode can wait for the installer phase. Recommend:
  ship Windows and Linux unsigned in Phase 3, hold the macOS archive until Phase 5.
- **D6. The VST3's editor.** Recommend JUCE's `GenericAudioProcessorEditor` first: two stock
  sliders, zero drawing code, nothing restated. The app's fader-and-knob look can be shared later
  (Phase 6) by compiling the same paint source into both products, never by linking the app's
  `common/ui` into the plugin.

## The tree

```text
rock-hero-common/audio/
  include/rock_hero/common/audio/
    shared/gain.h, pan.h, tone_output.h      - unchanged: the value types and their ranges
    gain_stage/gain_stage.h                  - NEW: GainStage, stereoGains, gainStageParameters,
                                               g_plugin_vendor_name
  src/
    shared/pan.cpp                           - unchanged (formatPan / parsePan)
    gain_stage/gain_stage.cpp                - NEW: the ramp and the apply loop
    tracktion/gain_plugin.{h,cpp}            - the renamed ToneOutputPlugin, as an adapter
  tests/test_gain_stage.cpp                  - NEW: laws, ramp, parameter table
  CMakeLists.txt                             - adds rock_hero::common::audio_gain_stage (JUCE-free,
                                               POSITION_INDEPENDENT_CODE ON); common::audio links
                                               it PUBLIC

rock-hero-plugins/                           - NEW product scope, namespace rock_hero::plugins
  CMakeLists.txt                             - add_subdirectory per plugin
  README.md                                  - what a plugin here may and may not link
  gain/                                      - namespace rock_hero::plugins::gain
    CMakeLists.txt                           - juce_add_plugin(rock_hero_plugins_gain FORMATS VST3)
    src/plugin/gain_processor.{h,cpp}        - juce::AudioProcessor over GainStage
    src/plugin/gain_editor.{h,cpp}           - Phase 6 only; Phase 2 uses the generic editor
    tests/CMakeLists.txt                     - rock_hero_plugins_gain_tests + the pluginval ctest
    tests/test_gain_processor.cpp
```

Files the new scope touches silently (no compile error when forgotten): the clang-tidy path regex
at `CMakeLists.txt:32` (add `rock-hero-plugins`); `scripts/verify-project-conventions.py`
(`PRODUCTS`/`LIBRARIES` enumerate a fixed grid — generalize the root check to every
`rock-hero-*/*/{include,src}` the tree contains); `.clang-format`/`.clang-tidy` already apply by
path; `CONTRIBUTING.md` and the two architecture documents' repository trees (design-doc changes
are listed for the user at the end).

Why the plugin's own sources sit in `src/plugin/`: the plugin is one feature, and the library-root
rule holds for every library, so its files take a folder named for the hub object they compose.

## The core's interface

```cpp
namespace rock_hero::common::audio {

constexpr std::string_view g_plugin_vendor_name{"Rock Hero"};

struct GainStageParameter { std::string_view id, name, label; double minimum, maximum, initial; };
// Append-only, forever: hosted parameter ids are indices in Tracktion
// (tracktion_ExternalPlugin.cpp:798-804) and hashes of `id` in VST3.
constexpr std::array<GainStageParameter, 2> gainStageParameters();   // {level, pan}

struct StereoGains { float left, right; };
constexpr StereoGains stereoGains(ToneOutput output) noexcept;      // dB law x balanced pan law

class GainStage {
public:
    void prepare(double sample_rate, double ramp_seconds);           // message thread
    void setTarget(ToneOutput output) noexcept;                      // block rate
    void process(std::span<float* const> channels, int num_samples) noexcept; // audio thread
};
}
```

The dB law is `std::pow(10, db / 20)`; `juce::Decibels::decibelsToGain`'s -100 dB silence floor
can never engage inside the `[-24, +24]` range, so the two agree exactly. The ramp is a per-channel
linear ramp (current, target, step, samples remaining): `juce::SmoothedValue` cannot be used in a
JUCE-free target, and the ramp is the trivial logic `CLAUDE.md` lets us hand-roll. The ramp LENGTH
stays the adapter's decision — the app's is `g_tone_switch_ramp_seconds / 2` because the stage
must settle inside the branch crossfade; the VST3's is its own constant with no such bound.

## Hard constraints

1. **The codes never change** (D1): `PLUGIN_MANUFACTURER_CODE` and `PLUGIN_CODE` are the class UID.
2. **The parameter list is append-only** and each entry's `id` string is permanent; a new
   parameter goes at the end with the next `juce::ParameterID` version hint.
3. **The plugin builds its own JUCE modules.** `juce_add_plugin` compiles the raw `juce::juce_*`
   modules into its shared-code target with the `JucePlugin_*` definitions
   (`JUCEUtils.cmake:1586-1615`), which the app's `rock_hero::juce_*` wrappers were built without
   (`cmake/RockHeroExternalModules.cmake`). The plugin therefore links NO `rock_hero::juce_*`
   wrapper and none of `rock_hero::common::core`, `::audio`, `::ui` — only
   `rock_hero::common::audio_gain_stage` and the raw JUCE modules `juce_add_plugin` wires.
4. **Strictness scoped to our sources.** JUCE's module sources are compiled inside the plugin
   target, so `rock_hero::build_policy` (`/WX`, `-Werror`) cannot go on the target; it goes on our
   files through `set_source_files_properties(... COMPILE_OPTIONS ...)`, and the target links the
   wrappers' non-strict set (`rock_hero::warning_flags`, `config_flags`, `lto_flags`).
5. **The VST3 state is the plugin's own**: two values written as XML through
   `getStateInformation`, read back tolerantly, append-only. A `.tone` file never carries it; the
   two adapters' persisted forms never need to agree.
6. **No `COPY_PLUGIN_AFTER_BUILD`** on developer builds: the system folders need elevation on
   Windows and would pollute every developer's DAW; `VST3_AUTO_MANIFEST` stays on so
   `moduleinfo.json` is generated (`JUCEUtils.cmake:1829`, `:1275`).

## Phases (all later than the first deliverable above)

Each phase ships on its own.

**Phase 1 — the core, and the built-in re-based on it.** Small: about a day. Add `gain_stage/`
and the `audio_gain_stage` sub-library; `GainPlugin` (the renamed `ToneOutputPlugin`) deletes its
two smoothers, its pan law and its dB conversion and keeps only parameter plumbing and
`applyToBuffer` calling `GainStage::process`. Its tests keep passing; `test_gain_stage.cpp` pins
the laws at the range ends, the ramp length in samples, and the table's ids and order. No CI
change. The gain-block plan's deferred item 8, folding `LiveRigGainPlugin` in, becomes a third
`GainStage` caller and deletes that class's own ramp.

**Phase 2 — the VST3 product.** Medium: two to three days, most of it CI on three OSes. The
`rock-hero-plugins/gain` target with `GenericAudioProcessorEditor`; `gain_processor.cpp` builds
its `AudioParameterFloat`s from `gainStageParameters()` and carries
`static_assert(std::string_view{JucePlugin_Manufacturer} == g_plugin_vendor_name)`. Its test
target processes a block through the processor and checks state round-trip and the parameter ids.
`find_program(pluginval)`: when found, `add_test` runs `pluginval --strictness-level 10
--validate <bundle>` and configure logs which case it took; CI gets the binary per D3. The
catalog filter of R5 lands here. Release builds already build every target, so the bundle exists
in `build/release/` on all three CI platforms from this phase on.

**Phase 3 — ship it as a download.** Small to medium: a day plus CI rounds. A `Plugins` CPack
component with an `install(DIRECTORY <bundle>)` rule (a `MODULE` target's own install rule would
flatten the bundle, dropping `Contents/<arch>/` and `moduleinfo.json`), listed through
`PACKAGE_BUILDER_CPACK_COMPONENTS`, and a `CPACK_PROJECT_CONFIG_FILE` that gives the archive
generator `CPACK_ARCHIVE_COMPONENT_INSTALL ON` so the plugin lands in its own zip while NSIS, DEB
and DragNDrop keep `Runtime` only. The release job uploads whatever CPack produced, so the plugin
zip appears beside the app on the release page with no workflow change. Per D5, the macOS zip is
withheld (or published with a plain "unsigned" note) until Phase 5.

**Phase 4 — installer placement.** Medium, and outside this repository: `cmake-package-builder`
(the user's) must grow a non-one-click NSIS mode with a components page and a section that writes
to `$COMMONFILES64\VST3`, which CPack's prefix-relative rules cannot express directly. Linux is
simple: the DEB installs the bundle under `lib/vst3/`. macOS waits on Phase 5 (a `.pkg` with the
plugin as an optional component, or a DMG with the `.vst3` beside the app).

**Phase 5 — signing and notarization.** Medium, mostly accounts and secrets, in `ci-workflows`:
Apple Developer ID with `codesign --deep --options runtime` inside-out and `notarytool`; Windows
Authenticode (or Azure Trusted Signing) for the installer and, once available, the `.vst3`. JUCE's
build-time ad-hoc signature (`JUCEUtils.cmake:998-1016`) is not a substitute.

**Phase 6 — optional follow-ons, each its own decision.** AU (after Phase 5); CLAP (needs a
dependency, R7); the shared fader-and-knob look compiled into both products (D6); a second plugin,
which by then costs one folder under `rock-hero-plugins/`.

## Risks

- **JUCE's splash screen.** Nothing in the tree sets `JUCE_DISPLAY_SPLASH_SCREEN` today; a plugin
  editor under the AGPL grant may disable it, under the EULA tiers it may not. Settle with the
  licence item below before Phase 2 ships an editor.
- **Two plugins named "Gain" in another DAW's list** is impossible (the class UID differs), but
  two of OURS with the same codes would be fatal; D1 is recorded here so no later plugin reuses
  `Gain`.
- **The MSVC `-Werror` scope** (constraint 4): a per-source `COMPILE_OPTIONS` genex is the
  mechanism to verify first; if it misbehaves under the Ninja/MSVC path, the fallback is a static
  library of our two sources compiled with the plugin's `JucePlugin_*` definitions, never the
  app's wrappers.
- **Linux `-fPIC`**: the core sub-library is static and a `MODULE` links it, so it needs
  `POSITION_INDEPENDENT_CODE ON` or the Linux link fails.
- **Catalog filter by vendor name** (R5) is a string match; a third party naming itself "Rock
  Hero" would be hidden. Accepted: the alternative, matching derived UIDs, restates the codes.

## Open legal items (for the user, perhaps the ip-legal-expert; not decided here)

1. **JUCE's licence for plugin binaries.** The repository is AGPLv3 (`LICENSE`) and JUCE is
   AGPLv3-or-EULA (`modules/juce/LICENSE.md:6-8`); the plugin inherits AGPLv3. Confirm the plugin
   binary may ship under AGPLv3 without a JUCE EULA tier, and what the splash-screen rule is for it.
2. **AGPL plugins inside closed hosts.** Loading an AGPLv3 `.vst3` into a proprietary DAW: whether
   the plugin and host form a combined work, and what the download page must say about source.
3. **The "VST" mark.** The VST3 SDK is MIT (`LICENSE.txt` under
   `modules/juce/modules/juce_audio_processors_headless/format_types/VST3_SDK/`), but the VST name
   and logo remain Steinberg trademarks with their own rules (its `README.md:256-267`: optional,
   and if used, only per Steinberg's guidelines, logos unaltered). Whether "VST3"
   may appear in a product name, a file name beyond the mandated `.vst3` extension, or the download
   page as a description is unverified.
4. **The ASIO SDK** does not enter the plugin (it links no device code); confirm nothing in the
   JUCE modules the plugin compiles pulls it in on Windows.

## Design-document changes for the user to approve

- `docs/design/architecture.md` "Repository Layout" and `CLAUDE.md` "Architecture": add
  `rock-hero-plugins/` with its dependency rule (may link `common`'s JUCE-free sub-libraries only;
  never the JUCE/Tracktion wrappers, never editor or game).
- `docs/design/architectural-principles.md` "Product Scopes": a fourth scope, and the rule that a
  plugin product builds its own JUCE modules.

## Relation to other records

- `docs/plans/todo/gain-block-and-signal-chain-panel.md` — Phase 1 there is the in-app Gain block
  and is the first deliverable; everything in this plan comes after it and builds on the type it
  renames and the identity authority it introduces.
- `docs/tracking/backlog.md` (2026-10-02) — the identity defect of R6.
- `docs/tracking/backlog.md` "standard tones built only from plugins we ship" — this plan is the
  first plugin that could back such a tone, as a built-in, with no file dependency.
- `docs/plans/roadmap/33-cross-platform-port.md` — Phase 5's signing work is the same work the
  app's own macOS release will need; do it once.
