# Plan 48 — Editor Audio Setup: toggle-aware device + calibration surfaces

Status: In progress — amended 2026-07-13 (final startup ruleset below), 2026-07-14 (device-failure
popup) | baseline `refactor @ 75cc26dd`

## Amendment (2026-07-14) — closed-device failure popup replaces silent closure and auto-reopen

User-directed redesign of how a closed audio device is surfaced (the app realistically cannot
function without one). This section is authoritative over the rule-1 phrasing below wherever they
conflict; the game-audio ruleset itself is unchanged.

- **Any closed-audio-device state raises a persistent modal**: "Failed to open audio device:
  \<specific reason\>" with **Retry** and **Open Audio Settings** (no Close — it stays until a
  Retry succeeds or the settings window opens; Escape maps to Open Audio Settings). Raised at
  startup when the
  saved route cannot open, and on a mid-session disconnect. Suppressed while the audio settings
  window is open (its staged edit deliberately closes the device); re-raised when the window tears
  down with the device still closed. Deferred behind the plan-48 startup prompts so at most one
  modal shows at a time. Rule 1's "route-kept-closed notice" is now this popup; the toggle still
  stays ON (the failure is about the device, not the source).
- **The engine's automatic reopen is removed** (it speculatively instantiated ASIO drivers
  mid-enumeration and re-opened devices inside its policy pass — both intermittently crashed
  flaky drivers). The engine's no-fallback policy now only undoes JUCE's hard-coded disconnect
  fallback; every reopen is the user's explicit Retry (or a settings-window apply). The engine
  records why the device is closed on `AudioDeviceStatus::unavailable_reason` for the popup.
- The editor-controller source/route side effects consolidated into one application path
  (`applyAudioSourceAndRoute`) shared by startup, the settings-window toggle, the recommendation
  decision, and Retry; `EditorViewState::use_game_audio_settings` now derives from the live store
  selection rather than re-reading the persisted toggle.
- **Recommendation decline button (supersedes rule 3's "no settings window" clause):** the
  decline button now reads **"Open Audio Settings"** and opens the audio device settings window
  after
  persisting the toggle off, landing the user where custom settings are actually configured. Its
  decision semantics are otherwise unchanged (Esc/close still writes nothing and re-asks).
- **Chosen-but-unopenable device stays the saved choice:** an OK onto a device whose driver fails
  to initialize now re-serializes the staged route as the saved choice (JUCE's own updateXml runs
  only after a successful open), so the failure popup and its Retry target the newly chosen
  device, never the previous one.

## Amendment (2026-07-13) — final startup ruleset

User-directed redesign of the toggle's default, the unavailable-game handling, and the startup
prompts. This section is authoritative wherever it conflicts with the original "Decisions already
made" bullets or phase text below; the superseded bullets are marked in place.

**State model.** Game-source availability is a three-state
`GameAudioSourceState { NotConfigured, Uncalibrated, Available }` (replacing the bool):
`NotConfigured` = game audio-config file missing/unreadable or no stored route; `Uncalibrated` =
route stored but no matching input calibration for that exact route; `Available` = route +
calibration. Two persisted editor-settings values govern behavior: the `useGameAudioSettings`
toggle, now **defaulting OFF** (`value_or(false)` — the absent state no longer means ON and needs
no special casing), and a `suppressGameAudioRecommendation` bool (default false).

**Invariant: a persisted ON means adoption actually succeeded.** Whenever ON cannot be honored,
the editor says so clearly, writes OFF, and falls back to its own settings. `ON + broken` is
therefore never a standing state — it can only be entered by the game's config regressing after a
successful adoption, and it self-heals to OFF with an error popup on the next startup. The
checkbox never shows ON while the editor is actually running on its own settings.

**Startup decision tree** (at most one popup can fire per launch, keyed on the toggle):

1. **ON + Available** → adopt the game route silently. A merely unplugged/in-use device is the
   existing route-kept-closed `DeviceUnavailable` notice, not a config regression — stays ON.
2. **ON + Uncalibrated / NotConfigured** → state-specific error popup, write OFF, stay on the
   editor's own route, and open the audio device settings window after dismissal.
3. **OFF (or never written) + Available + not suppressed** → recommendation popup: "Use game audio
   settings (recommended)" / "Use custom audio settings" plus a "Don't show this message again"
   checkbox that persists the suppression flag on any close. The recommended button always
   succeeds because the popup is gated on `Available`. "Use custom" writes OFF and dismisses (no
   settings window — the user declined guidance). Esc/close writes nothing and re-asks next launch.
4. **Otherwise** → the editor's own settings, silently. This silence is correct: the editor is
   doing exactly what the toggle says.

**Checkbox (device settings window).** When no game configuration exists at window open
(`NotConfigured`, from a fresh `IEditorController::gameAudioSourceState()` read), the checkbox is
**disabled** with the tooltip "Game audio settings unavailable" — there is nothing a click could
adopt or usefully explain. Otherwise it stays clickable, and enabling routes through the attempt
logic: `Available` → adopt + persist ON; `Uncalibrated` (or a mid-dialog regression) → a clear
state-specific error dialog, the checkbox reverts, and **nothing is persisted** — the popup is
reserved for the case where the user can act (calibrate in the game). Canonical error copy lives
in editor-core
(`GameAudioSourceError` with fixed per-code messages) so the startup popup and the checkbox dialog
share one text: `Uncalibrated` → "Game audio settings cannot be used until input calibration has
been completed in the game."; `NotConfigured` → no game audio settings were found, set up audio in
the game first.

**Consequences for the original plan text:**

- "First-run default: the toggle is ON" is **superseded**: the default is OFF, and the
  recommendation popup (not a silent default) is the encouragement mechanism.
- "Enabling with no calibrated game configuration still persists the choice" is **superseded**:
  a failed enable persists nothing and reverts.
- The "genuinely-unconfigured dismissible prompt / notice bar" (original open questions 1–2) is
  **superseded** by the startup error popup + settings-window open; no standing in-window
  unconfigured-game guidance is needed because `ON + broken` is no longer a standing state.
- The cached availability bool on the controller and its `EditorViewState` field are **removed**;
  availability is read fresh at the decision points (startup, checkbox click, recommendation
  accept) and surfaced only through the transient prompts.
- Open question 3 (toggle placement) remains as originally answered by the implementation
  (top-of-panel row).

## Goal

Give the editor a first-class "use the game's audio settings" toggle so a player who has already
configured audio in the game does not re-configure it in the editor, while preserving the editor's
own fully-editable audio device and calibration surfaces for anyone who wants the editor to own its
audio route.

Concretely: introduce a headless effective-source facade
(`editor::core::EditorEffectiveAudioConfigStore : common::audio::IAudioConfigStore`) that reads
**either** the editor's own audio-config store **or** a freshly-reconstructed read-only view of the
game's audio-config file, while routing **all** writes unconditionally to the editor's own store.
A single "use game settings" toggle — persisted editor workflow state on `IEditorSettings`, and
**defaulting ON at first run** — governs both the editor's existing audio device settings window and
its existing input-calibration window: when OFF, both are the full editable flow; when ON, both
become read-only reflections of the game's configuration with an explanatory notice.

This plan deliberately does **not** consolidate the two existing windows into one modal. The editor
keeps its **separate** audio device settings window and its **separate** calibration window, each
made toggle-aware in place. The headless core (facade + toggle state) is the design carried forward
from the re-architecture output section 1.4; only the UI shape differs from that output — separate
windows made toggle-aware, not a single "Audio Setup" modal.

## Non-goals

- Consolidating the audio device settings window and the calibration window into one modal. The
  re-architecture output proposed that; this plan explicitly rejects it per user direction and keeps
  the two windows separate. (See "Decisions already made".)
- Any change to `LiveInputMonitor`'s gate logic (plan 14 owns it; it is unchanged here — this plan
  only injects a different concrete `IAudioConfigStore` into it).
- The game's native audio configuration surfaces (plan 32) or any game code linkage. The editor
  reads the game's file by well-known path only, with zero dependency on `game/core`.
- The shared per-app audio-config store type, `ActiveDeviceRoute`, `AudioConfigStore`, or the two
  identity constants (all owned by plan 13 P1) — consumed here, not defined here.
- Latency-offset accessors on `IAudioConfigStore` (plan 13 P3 adds them); see the forward-dependency
  note under "Dependencies".
- Redesigning the editor's audio device settings view or the calibration view layout. This plan adds
  a toggle and a read-only/notice mode to the existing views; it does not restyle them.

## Constraints

Applicable subset of the roadmap's non-negotiable constraints (see docs/roadmap/00-roadmap.md):

- (a) **Layering**: common never depends on editor or game code. The facade lives in
  `rock-hero-editor/core` and depends only on `common::audio::IAudioConfigStore` /
  `common::audio::AudioConfigStore`; it reaches the game's file by a well-known path composed from
  common constants, never through `game/core`. Tracktion headers stay isolated to
  rock-hero-common/audio implementation files.
- (b) **Public-header minimalism**: only headers that must be public are public; ports-and-adapters
  per docs/design/architectural-principles.md ("Ports and Adapters", "Typed Boundary Errors"). The
  facade is a `common::audio::IAudioConfigStore` implementation; its own header is editor-core
  public only because `main.cpp` composes it.
- (c) **Naming firewall**: the commercial real-guitar game that inspired this project is never
  named; use RS/neutral phrasing.
- (d) **Error channels**: fallible accessors return `std::expected<…, Error>`, never
  `std::optional<Error>` (docs/design/coding-conventions.md). The facade preserves the
  `IAudioConfigStore` signatures verbatim.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`), never raw cmake/ctest/ninja. Intermediate phases run only the checks their
  changes warrant; the final acceptance phase runs the sanctioned bundle as separate invocations.
- (i) **Real guitar input**: the toggle-ON read-only calibration display shows the game's measured
  value only; it offers **no** measure action (a strum-to-measure gesture belongs solely to the
  OFF/editable flow), so a two-hands-on-guitar player is never asked to interact mid-display.

Design-doc anchors: docs/design/architectural-principles.md "Separate State From Side Effects" (the
facade flip and write-routing are pure state; the two engine adoptions are side effects living in
the controller, not the store) and "Ports and Adapters"; docs/design/coding-conventions.md
(`std::expected` boundary errors, feature folders, naming).

## Current state inventory

Verified paths and behavior on the baseline tree:

- **The editor owns audio config today, on its profile file.** `IEditorSettings`
  (rock-hero-editor/core/include/rock_hero/editor/core/settings/i_editor_settings.h) exposes the
  opaque serialized device state at `audioDeviceState()` / `setAudioDeviceState()` (`:66`), the
  workflow toggle precedent `waveformVisible()` (`:80`) with its
  `setWaveformVisible()` setter (`:87`), and the per-route input-gain calibration trio
  `inputCalibrationFor` (`:179`), `saveInputCalibration` (`:186`), `removeInputCalibration`
  (`:194`). These persist to the editor's `Rock Hero Editor.settings` profile file. Plan 13 P2
  moves device state off this header onto the editor's own `AudioConfigStore`; plan 14 P3 relocates
  the calibration call sites into `LiveInputMonitor` and deletes the calibration trio from this
  header. This plan **adds** the toggle bool beside `waveformVisible()` and consumes the plan-14
  monitor.
- **The calibrate-first gate already reads a store and checks device-settings-open.**
  rock-hero-editor/core/src/.../input_calibration_handlers.cpp reads calibration via
  `m_settings.inputCalibrationFor` (`:127`) and writes via `m_settings.saveInputCalibration`
  (`:151`); disables the live-tone gate whenever `audioDeviceSettingsOpen()` is true (`:193`);
  disables on route calibration mismatch via `calibrationMatches(...)` (`:220`); arms through
  `setLiveInputMonitoringEnabled(true)` (`:234`); and meters the calibration surface through the
  **independent** audition path `setCalibrationInputMonitoringEnabled(...)` (`:58`, `:292`) — a
  separate `ILiveInput` family from the live-tone gate. After plan 14 P3 this logic lives in
  `LiveInputMonitor`; the branch order and arm sequence are unchanged.
- **The device restore/persist side effects live in the controller.**
  `EditorController::Impl::restoreAudioDeviceState()` (editor_controller.cpp:2088) and
  `persistAudioDeviceState()` (`:2108`) apply and capture the serialized device state through the
  device-configuration port; `restoreAudioDeviceState()` is called at startup (`:1045`) and
  `persistAudioDeviceState()` on device-config change (`:1427`). The engine applies a serialized
  route via `Engine::restoreSerializedDeviceState` (engine_device_config.cpp:96). These are the
  side-effect seams the toggle ON/OFF handlers reuse.
- **The two audio surfaces are already separate windows.** rock-hero-editor/ui has
  `AudioDeviceSettingsWindow` (ui/src/audio_device/audio_device_settings_window.{cpp,h}) wrapping the
  reusable `audio_device_settings_view` (ui/src/audio_device/audio_device_settings_view.{cpp,h}),
  and `InputCalibrationWindow` (ui/src/input_calibration/input_calibration_window.{cpp,h}, whose
  private `Content` at input_calibration_window.cpp:98 is the reusable body). The main view wires an
  "Audio Device" button to `showAudioDeviceSettingsWindow()` (editor_view.cpp:282-283, definition at
  `:1422`) and constructs the calibration window at editor_view.cpp:1318. Both windows already have
  complete headless cores (`AudioDeviceSettings` + the plan-14 `LiveInputMonitor`), so this plan is
  UI recomposition (toggle + read-only mode), not new domain logic.
- **No editor audio-config store or facade exists yet.** rock-hero-editor/core has no
  `EditorEffectiveAudioConfigStore`; the shared `common::audio::IAudioConfigStore` /
  `AudioConfigStore` / `ActiveDeviceRoute` / identity constants arrive with plan 13 P1. This plan
  cannot start until plan 13 P1 and plan 14 P3 have landed.

Verified against code on 2026-07-12, refactor @ 75cc26dd.

## Dependencies

Upstream (both must land before this plan starts):

- **plan 13 P1** — the shared per-app `common::audio::AudioConfigStore` (with
  `Access::ReadOnly`), the `ActiveDeviceRoute` value type (opaque blob **+** resolved
  `InputDeviceIdentity`), the `IAudioConfigStore` port, and the two identity constants
  (`editorAudioConfigApplicationName()` / `gameAudioConfigApplicationName()` in
  `common/audio/settings/audio_config_identity.h`). The facade wraps the editor's own instance and a
  read-only view of the game's file, both of this type.
- **plan 14 P3** — `LiveInputMonitor` holding its `IAudioConfigStore&` as a single **swappable**
  dependency (the plan-14 D3 constraint), so this plan injects the facade with no monitor edit. The
  editor is a thin driver over the monitor by then; the calibration read/write call sites live in
  the monitor, not `IEditorSettings`.

Forward-dependency (record it in both plans):

- **plan 13 P3** adds `latencyOffsetsFor` / `saveLatencyOffsets` / `removeLatencyOffsets` to
  `IAudioConfigStore`. Every implementor — including this plan's `EditorEffectiveAudioConfigStore` —
  must then override them (getters → selected source, setters → editor's own store, exactly as the
  existing accessors). Whichever of plan 13 P3 / plan 48 lands second **must** add those overrides
  in the same change or the facade stops compiling. This is the plan-13-P3 fan-out note (F#8),
  mirrored here.

Cross-references: plan 13 P1 (per-app store + `ActiveDeviceRoute` + `Access::ReadOnly` + identity
constants), plan 13 P3 (latency accessors — forward-dependency above), plan 14 P3 (swappable
monitor dependency), plan 32 (game native audio config — produces the file this plan reads
read-only; not a build dependency, and the toggle reads **unavailable** until that file exists with
a calibrated route), plan 26 Phase 8 (SDL presentation over plan 32).

Not gated on the game track: the toggle degrades to "unavailable" whenever a game audio-config file
with a calibrated active route does not exist.

## Decisions already made

- **Keep the two windows separate; do not consolidate into one "Audio Setup" modal (user
  directive, overrides the re-architecture output section 1.6).** The re-architecture output
  proposed retiring the standalone `InputCalibrationWindow` and the separate
  `AudioDeviceSettingsWindow` into a single "Audio Setup" modal. The user decided instead to keep the
  editor's calibration window **where it is**, made toggle-aware, and keep the device settings window
  separate as well. The headless facade + toggle-state design from output section 1.4 is carried
  forward **unchanged** (it is the correct headless core); only the UI differs — separate windows
  made toggle-aware, not a consolidated modal.
- **The "use game settings" toggle lives in the editor's audio device settings panel and governs
  BOTH surfaces.** The toggle appears in the device settings window; flipping it re-scopes both the
  device fields (read-only when ON) and the separate calibration window (read-only + notice when ON).
  One toggle, two governed surfaces, device and calibration moving together in both directions.
- **Toggle OFF = full editable flow on both surfaces.** The device settings window edits the
  editor's own route; the calibration window runs the full editable strum-to-calibrate flow against
  the editor's own store. This is today's behavior, preserved.
- **Toggle ON = read-only reflection of the game's config on both surfaces.** The device fields
  display the game's route read-only; the calibration window shows a **read-only display of the
  game's calibration value** plus the notice "These come from your Game audio settings" and offers
  **no measure action**. The editor's live route is adopted from the game (a controller side effect),
  so the gate resolves the game's calibration.
- **The toggle bool is editor workflow state, not audio config.** It does not affect the game and is
  not in the shared schema, so it lives on `IEditorSettings` beside `waveformVisible()`
  (`i_editor_settings.h:80`), persisted to the editor's profile file.
- **[Superseded by the 2026-07-13 amendment — the default is now OFF and the recommendation popup
  replaces the silent default.]** ~~First-run default: the toggle is ON.~~ At first
  run the editor defaults "use game settings" ON. Thereafter the persisted bool is authoritative:
  honor the user's stored choice on every subsequent launch; never reset it to ON. Concretely,
  `useGameAudioSettings()` returns `std::optional<bool>` and an **absent** value (never written)
  resolves to the ON default; once the user flips it, the written value wins.
- **[Superseded by the 2026-07-13 amendment — replaced by the startup error popup + write-OFF
  fallback and the Available-gated recommendation popup.]** ~~Genuinely-unconfigured game → a
  dismissible prompt offering both paths as equals.~~ When the toggle defaults ON but the game is unconfigured (`gameSourceAvailable()`
  false), show a **dismissible** prompt — not a recurring hard modal — recommending the user
  configure audio in the Game for a consistent cross-app experience, and offering **both** paths as
  equals: set it up in the Game, **or** configure the editor's own audio here. The editor-own path is
  a frictionless one-action opt-out that turns the toggle OFF and opens the editor's own audio setup.
  The prompt appears only in the genuinely-unconfigured case, never when a valid game config exists.
- **All writes go to the editor's own store; the game file is a read source only (output F3 fix).**
  The facade never fuses read-source with write-target. Getters select a source; setters route
  unconditionally to the editor's own store. There is no "setter errors because the game source is
  active" path. `Access::ReadOnly` on the game view is defense-in-depth only.
- **Availability is calibration-aware, not device-presence (output F2/F9 fix).**
  `gameSourceAvailable()` requires the game's persisted `activeDeviceRoute().identity` to have a
  matching calibration; a game that recorded a device but has no calibration for that exact route
  must **not** arm the toggle into a silent, locked, dead state.
- **Freshness is real, not nominal (output F1/F6 fix).** `juce::PropertiesFile` parses once at
  construction and serves getters from memory, so the facade **reconstructs** the read-only game view
  at each `useGameSource(true)` and each explicit availability check / apply — a one-shot fresh read,
  deliberately not a live watch.
- **The ON/OFF engine adoptions are controller side effects, not store behavior.** Per "Separate
  State From Side Effects": the facade flip and write-routing are pure state; applying the game's (ON)
  or the editor's own (OFF) serialized device state to the editor engine via
  `restoreSerializedDeviceState` lives in the controller.
- **`persistAudioDeviceState()` is suppressed while the game source is active.** The adopted route
  came from the game, not from a user edit of the editor's own route; writing it would corrupt the
  route the editor must restore on toggle-OFF.
- **The `audioDeviceSettingsOpen()` gate stays as-is (output F5).** It correctly suppresses the
  live-tone gate for the settings surface's lifetime; the calibration surface meters through the
  independent audition path (`setCalibrationInputMonitoringEnabled`), which works whenever the device
  is open. No predicate rescoping is needed.

## Open questions for the user

Questions 1 and 2 are resolved by the 2026-07-13 amendment (startup error popup + write-OFF
fallback; Available-gated recommendation popup). Question 3 stands as implemented (top-of-panel).

1. **Prompt copy and channel.** The genuinely-unconfigured prompt is described as "dismissible, both
   paths as equals, frictionless opt-out." Is a non-modal in-view notice bar (dismiss + two action
   buttons: "Set up in the Game" guidance / "Use the editor's own audio") the right surface, or do
   you want a one-time dismissible dialog on first launch? Recommendation: a non-modal notice bar
   anchored to the audio device settings entry, so it never blocks the editor and re-appears only
   while the toggle is ON and the game is unconfigured.
2. **"Set up in the Game" action semantics.** The editor cannot launch or drive the game's audio
   setup. Should that path be purely advisory text ("open the game and complete audio setup"), or
   should it also offer to re-check availability (re-run `gameSourceAvailable()`) so the notice clears
   without an editor restart? Recommendation: advisory text plus a "Re-check" affordance that
   reconstructs the game view and re-evaluates availability.
3. **Toggle placement within the device settings window.** The toggle lives "in the editor's audio
   device settings panel." Top-of-panel (governs the whole panel visually) vs. a labeled section
   header. Recommendation: top-of-panel, so its read-only effect on the fields below is visually
   obvious. This is a P2 layout detail; flag if you have a preference.

## Phased implementation

### Phase 1 — Effective-source facade + toggle state + startup default (editor/core, headless)

Scope: the headless core only — the facade, the persisted toggle bool with its ON-by-default
resolution, the availability + freshness logic, and the two controller side effects. No UI. This is
output section 1.4 carried forward unchanged, plus the first-run default and the availability signal
that drives the P2 prompt.

- **`EditorEffectiveAudioConfigStore : common::audio::IAudioConfigStore`**
  (rock-hero-editor/core/include/rock_hero/editor/core/audio/editor_effective_audio_config_store.h):
  - Ctor `(common::audio::IAudioConfigStore& own_store, std::filesystem::path game_settings_file)`.
    The game path is composed from `applicationDataFolderName()` + `gameAudioConfigApplicationName()`
    at the composition root — **zero `game/core` linkage**.
  - Getters delegate to **either** `own_store` **or** a freshly-reconstructed read-only
    `AudioConfigStore` over the game path, per `usingGameSource()`. **All setters delegate
    unconditionally to `own_store`** (read-source and write-target are not fused).
  - `void useGameSource(bool enabled)` — on enable, (re)constructs a fresh `Access::ReadOnly` game
    view so the read is genuinely current; on disable, drops it.
  - `[[nodiscard]] bool usingGameSource() const noexcept`.
  - `[[nodiscard]] bool gameSourceAvailable()` — rebuilds the game view, then returns true only when
    `activeDeviceRoute()` has an `identity` **and** `inputCalibrationFor(*identity)` yields a present
    calibration (calibration-aware, not device-presence).
  - When plan 13 P3 has landed, also override `latencyOffsetsFor` / `saveLatencyOffsets` /
    `removeLatencyOffsets` with the same source-selection (getters) / own-store (setters) discipline
    (forward-dependency note).
- **Toggle bool on `IEditorSettings`** (beside `waveformVisible()` at `i_editor_settings.h:80`):
  - `[[nodiscard]] virtual std::optional<bool> useGameAudioSettings() const = 0;`
  - `[[nodiscard]] virtual std::expected<void, EditorSettingsError> setUseGameAudioSettings(bool enabled) = 0;`
  - **First-run default resolution:** an absent (never-written) value resolves to **ON**. Provide the
    resolution as a small free function or controller helper (e.g. `useGameAudioSettingsOrDefault()`
    returning `settings.useGameAudioSettings().value_or(true)`) so the ON default is expressed once;
    once the user flips the toggle, the persisted value is authoritative on every later launch.
    Update `NullEditorSettings` / editor test fakes accordingly.
- **Controller side effects (message thread), per "Separate State From Side Effects":**
  - **Toggle ON** (guarded on `gameSourceAvailable()`): apply the game's
    `activeDeviceRoute().serialized_state` to the **editor's own** engine via
    `restoreSerializedDeviceState` (engine_device_config.cpp:96); `useGameSource(true)`; refresh the
    monitor so the unchanged gate resolves the game's calibration; suppress
    `persistAudioDeviceState()` while active.
  - **Toggle OFF:** `useGameSource(false)`; **re-restore the editor's own**
    `activeDeviceRoute().serialized_state` to the engine (the symmetric side effect — without it the
    editor is stranded on the game's device); resume `persistAudioDeviceState()`.
- **Startup composition:** at editor launch, resolve the toggle (ON by default), and if ON: when
  `gameSourceAvailable()` adopt the game route; when **not** available, leave the editor on its own
  route and raise the P2 "game unconfigured" signal (a queryable state on the facade/controller, e.g.
  `gameSourceAvailable()` returning false while the toggle is ON) that P2's UI turns into the
  dismissible prompt. No hard modal in core; core only exposes the signal.
- **Inject the facade into `LiveInputMonitor`** as the swappable `IAudioConfigStore&` (plan 14 D3),
  wired in `rock-hero-editor/app/main.cpp`, which also constructs the editor's own
  `AudioConfigStore` and computes the game path from the two common constants.

Files/modules: new
`rock-hero-editor/core/include/rock_hero/editor/core/audio/editor_effective_audio_config_store.h`
and `rock-hero-editor/core/src/audio/editor_effective_audio_config_store.cpp`; `IEditorSettings` /
`EditorSettings` toggle accessors + default-resolution helper; `EditorController` ON/OFF handlers
and startup resolution; `rock-hero-editor/app/main.cpp` composition; `NullEditorSettings` / test
fakes; CMake source-list additions.

Public-header impact: new editor-core public header for the facade (composed by `main.cpp`);
`IEditorSettings` gains the two toggle accessors. No common headers change.

Testing plan: new
`rock-hero-editor/core/tests/test_editor_effective_audio_config_store.cpp` using the shared
in-memory `IAudioConfigStore` fake (plan 13 P2 provides it): availability true (route + matching
calibration) vs. false (route present, no calibration; no route); **stale-then-fresh** read across a
simulated game-file write (reconstruction picks up the new value); getters follow the selected
source while setters always hit the own store; ON-then-OFF round-trip restores the editor's own
route. Editor-core toggle-default tests: absent value resolves ON; a written `false` resolves OFF and
survives reload. Controller tests: ON adopts the game route and suppresses persist; OFF re-restores
the editor route; unavailable-game keeps the editor route and raises the unconfigured signal. Update
`test_editor_settings.cpp` for the new accessors and default.

Exit criteria: facade + toggle state build and pass tests; the monitor consumes the facade with no
monitor edit; the unconfigured signal is queryable; no UI touched yet.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets rock_hero_editor_core_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 2 — Toggle-aware separate windows (editor/ui) — NOT a consolidated modal

Scope: make the two **existing separate** windows toggle-aware in place. Add the "use game settings"
toggle to the audio device settings window, add a read-only + notice mode to the calibration window,
and render the genuinely-unconfigured dismissible prompt from P1's signal. The two windows stay
separate; nothing is retired or merged.

- **Audio device settings window** (ui/src/audio_device/audio_device_settings_window.cpp +
  audio_device_settings_view): add the "use game settings" toggle to the panel (placement per open
  question 3). Flipping it calls the P1 controller ON/OFF handlers. When ON, the device fields render
  **read-only**, reflecting the game's route; when ON but `gameSourceAvailable()` is false, surface
  the disabled reason (no game config / game route uncalibrated) rather than an inert switch, and
  offer the frictionless opt-out that flips the toggle OFF and drops into the editable device flow.
  When OFF, the panel is the full editable device flow (today's behavior). The "Audio Device" button
  (editor_view.cpp:282-283) is unchanged — it still opens this window.
- **Input calibration window** (ui/src/input_calibration/input_calibration_window.cpp, `Content` at
  `:98`; constructed at editor_view.cpp:1318): stays a **separate** window where it is. Add a
  toggle-aware mode:
  - Toggle OFF → the full editable strum-to-calibrate flow against the editor's own store (today's
    behavior), metering through the independent audition path.
  - Toggle ON → a **read-only display** of the game's calibration value with the notice "These come
    from your Game audio settings" and **no measure action** (no strum-to-calibrate button, no gesture
    prompt). The value shown is the game's route calibration the facade reads.
  The window subscribes to the toggle state so it re-scopes live when the toggle flips in the device
  window (one toggle governs both surfaces).
- **Genuinely-unconfigured dismissible prompt:** when the toggle is ON and P1's signal reports the
  game unconfigured, render a **dismissible** prompt (not a recurring hard modal; surface per open
  question 1) recommending the user configure audio in the Game for a consistent experience, offering
  **both paths as equals** — "set it up in the Game" (advisory, per open question 2) and "configure
  the editor's own audio here" (the one-action opt-out that flips the toggle OFF and opens the
  editable device flow). The prompt shows only in the genuinely-unconfigured case and is dismissible;
  it never appears when a valid game config exists.
- **Nothing retired.** `AudioDeviceSettingsWindow` and `InputCalibrationWindow` both remain; no
  consolidated modal is introduced. This is the explicit divergence from the re-architecture output
  section 1.6.

Files/modules: rock-hero-editor/ui audio_device + input_calibration window/view TUs; editor_view
wiring for the toggle-shared state and the prompt; no new window class.

Public-header impact: none beyond editor-ui internals; the two window headers gain toggle-awareness
parameters/state, not new public ports.

Testing plan: editor-ui view tests where a headless core exists (extend
test_audio_device_settings_view.cpp for the read-only-when-ON field state and the disabled-reason
path). Read-only calibration display and the dismissible prompt are UI states driven by P1's
queryable signals; cover the state resolution (which mode each window shows for ON-available /
ON-unavailable / OFF) at the controller/core seam in P1's tests and keep the UI layer thin. Manual
editor check: toggle ON with a calibrated game file locks both windows read-only with the notice;
toggle ON with an unconfigured game shows the dismissible both-paths prompt; the opt-out flips OFF
and opens the editable device flow; toggle OFF restores full editing on both windows.

Exit criteria: both existing windows are toggle-aware in place; the calibration window shows the
read-only game value + notice with no measure action when ON; the unconfigured dismissible prompt
offers both paths with a one-action opt-out; no window is merged or retired; editor builds and
touched tests pass.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

## Final acceptance phase

Per constraint (h), run the sanctioned bundle as separate invocations from the repo root, plus
formatting:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Acceptance: the editor reads either its own audio config or a fresh read-only view of the game's
file, with all writes going to its own store; a single persisted "use game settings" toggle (ON by
default at first run, honoring the stored choice thereafter) governs both the separate device
settings window and the separate calibration window; ON locks both read-only with the game's values
and the calibration notice (no measure action); a genuinely-unconfigured game raises a dismissible
both-paths prompt with a frictionless opt-out; both existing windows remain separate (no consolidated
modal); availability is calibration-aware; the game is never affected by the editor's read; and the
editor has zero `game/core` linkage.

## Rollback/abort notes

- **This plan is editor-only and additive.** Reverting it removes the toggle and the facade and
  restores the two windows to their pre-plan (editor-own-only) behavior with no data loss — the
  editor's own store was always the sole write target, and the game file was only ever read.
- **Phase 1 is the substantive one** (facade + toggle state + controller side effects). It is pure
  editor-core plus composition; reverting the commit removes the facade injection and the monitor
  falls back to the editor's own store directly. The toggle bool, if left on `IEditorSettings`,
  degrades harmlessly to unused.
- **Phase 2 is UI-only.** Reverting restores the plain (non-toggle-aware) separate windows; the
  headless facade continues to function and simply reads the editor's own store (toggle effectively
  OFF) with no UI to flip it.
- **Toggle degrades to "unavailable" off the game track.** With no game audio-config file, the toggle
  reports unavailable and the editor stays on its own route; the plan is safe to land before plan 32.
- **Plan 13 P3 forward-dependency:** if the facade fails to compile after plan 13 P3 lands (new
  latency accessors unoverridden), the fix is additive — add the three overrides with the same
  source-selection/own-store discipline; no rollback needed.
