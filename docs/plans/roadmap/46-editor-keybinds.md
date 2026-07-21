# Plan 46 — Editor Keybinds

**Status:** **G46-KEYMAP CLOSED 2026-07-20** — Phase 0 complete. Q1 answered by the signed keymap
matrix (which supersedes the Appendix's tier A as the default map); Q2 = (a) parallel systems plus
an extraction watch-item; Q3 superseded — Undo/Redo/Play-Pause are **non-rebindable core
commands** (decision below), so Phase 4's injection seam is no longer needed and that phase
rescopes to the `Ctrl+Shift+Z` redo alias + predicate dedupe. Execution rides
docs/plans/roadmap/53-editor-keyboard-and-pointer-completion.md: its Phases 1–2 run this plan's
Phases 1–3/5. The framework evaluation is complete (build on JUCE, evidence below).

> **AMENDED 2026-07-20 by the settled editor keymap** (see
> docs/plans/roadmap/53-editor-keyboard-and-pointer-completion.md and
> docs/plans/in-progress/keymap-matrix.md). **Q1 (default keymap) is now answered** by the settled
> matrix. New scope joins this plan: the **keybind-discovery context menu** on every surface (each
> item shows its live shortcut) and the full per-surface keyboard model. `Ctrl+T` is **kept but
> guarded against `Alt`** (not removed). Plan 53 is the sequenced build (registry-first) and drives
> this plan's registry phases as its Phase 1. Re-read before executing.
Date: 2026-07-06. Baseline: `refactor @ 13e82fb0`.

## Goal

Every editor keyboard shortcut lives in one user-rebindable command system: a single registry of
stable command ids with default bindings, a "Keyboard Shortcuts" configuration dialog (defaults
listed, per-command clear and reset-to-default, conflict warning with overwrite-and-clear), diffed
persistence in the per-user editor settings, shortcut text shown automatically in menus, and
plugin editor windows honoring the user's actual undo/redo/play-pause bindings instead of a
hardcoded copy. Plans 40/41/44 register their new shortcuts through this system instead of growing
`EditorView::keyPressed`.

**User-facing keybind documentation rides this plan** (user direction 2026-07-16): shipping the
rebinding system is also the moment to write the user-facing documentation of the editing
interaction grammar (docs/plans/in-progress/editing-interaction-model.md is the design-side
truth). Both are deliberately deferred while the grammar is still being tuned against real
charting — do not author user docs from the model doc before the user declares the design
settled.

## Non-goals

- Chorded/sequenced bindings (Ctrl+K Ctrl+B) and mouse-button bindings — fundamentally outside
  `juce::KeyPress` (evidence below); revisit only if a product requirement appears, which would
  need a custom capture layer in front of the JUCE system.
- Game input handling. The game's menu/gameplay input layer (keyboard + gamepad + MIDI pedal) is
  owned by docs/plans/roadmap/26-game-startup-menus-library.md Phase 5; see Q2 for the relationship.
- Defining the final shortcut set for chart editing and tempo authoring — those commands are
  registered by docs/plans/roadmap/40-chart-editing.md and docs/plans/roadmap/41-tempo-map-authoring.md when their
  phases land; this plan provides the mechanism plus reserved conventions (Appendix tier B).
- Localizing shortcut display names (JUCE's `getTextDescription()` is English-only; accepted).
- Touching the in-flight editor-core tone work (docs/plans/in-progress/tone-track-tempo-map-plan.md);
  its Ctrl+T marker shortcut is migrated into the registry during Phase 1 inventory re-check, not
  redesigned here.

## Constraints

- (a) **Layering**: common never depends on editor or game code. The plugin-window shortcut seam
  in `rock-hero-common/audio` therefore receives binding *data* injected by the editor; it never
  includes editor headers. Anything both products need is extracted to rock-hero-common first, as
  its own phase with tests, before game code consumes it (relevant only if Q2 resolves to a shared
  concept).
- (b) **Public-header minimalism**: new headers start in `src/`; only the injected-bindings value
  type and port setter become public (`rock-hero-common/audio` include tree), per
  docs/design/architectural-principles.md "Placement Procedure for New Files" and "Ports and
  Adapters".
- (c) **NAMING FIREWALL**: the commercial real-guitar game that inspired this project is never
  named in any file; use "RS"/neutral phrasing. Charter (BSD 3-Clause) may be named.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`), never raw cmake/ctest/ninja. Intermediate phases run only the checks their
  changes determinately warrant; the final acceptance phase is the sanctioned bundle as separate
  invocations.

## Current state inventory

> **STALE — re-verify at Phase 1 execution (mandated below).** This inventory is a 2026-07-06
> snapshot; `EditorView::keyPressed` has since grown far past the three shortcuts listed here
> (caret navigation and jumps, digits, Delete/Insert, Esc, `Ctrl+T`, F3/F8, grid `+/-`,
> `Ctrl` zoom, the `Shift`+extend time-selection families — landed through 2026-07-20). The
> live end-to-end description is docs/developer/keyboard-input.md; the authoritative key list is
> the signed docs/plans/in-progress/keymap-matrix.md. The plugin-window seam and menu facts
> below remain accurate.

Scattered key handling (three copies of the same binding knowledge):

- `rock-hero-editor/ui/src/main_window/editor_view.cpp:122-147` — anonymous-namespace predicates
  `normalizedAsciiKeyCode`, `hasCommandShortcutModifier`, `isUndoShortcut`, `isRedoShortcut`.
  `EditorView::keyPressed` (`editor_view.cpp:621-652`) handles Ctrl+Z (gated on
  `m_state.undo_enabled`), Ctrl+Y (gated on `redo_enabled`), and Space → `onPlayPausePressed`.
- `rock-hero-editor/ui/src/main_window/main_window.cpp:101-114` — `MainWindow::keyPressed`
  manually forwards keys to the editor component when native focus lands on the window shell and
  no modal blocks it.
- `rock-hero-common/audio/src/tracktion/plugin_window.cpp:17-47` — a byte-for-byte second copy of
  the same predicates plus `isPlayPauseShortcut`, used by `PluginWindow::keyPressed`
  (`plugin_window.cpp:158-165` → `handleCommandShortcut` at 179-199). A *third*, VK-code copy
  lives in `commandForWindowsKeyMessage` (`plugin_window.cpp:277-315`) for the Win32
  `WH_GETMESSAGE` hook (`windowsShortcutHook`, 384-432) that intercepts keys before a focused
  native plugin window consumes them. `disposeCommandKey` (360-382) makes Space yield to plugin
  text input (VST3 `onKeyDown` probe, then system-caret check) while undo/redo never yield.
- The command *routing* out of plugin windows is already decoupled: `PluginWindowCommand`
  {Undo, Redo, PlayPause} and `PluginWindowCommandDispatcher`
  (`rock-hero-common/audio/src/tracktion/plugin_window.h:27-40`), surfaced through
  `IPluginHost::setPluginWindowCommandObserver`
  (`rock-hero-common/audio/include/rock_hero/common/audio/plugin/i_plugin_host.h:116,315`),
  dispatched by `Engine::Impl::dispatchPluginWindowCommand`
  (`rock-hero-common/audio/src/engine/engine_plugin_host.cpp:680-707`), observed by
  `EditorController` (`rock-hero-editor/core/src/controller/editor_controller.cpp:939-940,1008`).
  Only the key→command *matching* is hardcoded in common; that is the seam this plan replaces.
- Menus: `EditorView` implements `juce::MenuBarModel` with raw int item ids
  (`editor_view.cpp:30`, `g_open_command{1}`; menu construction 655-719; `menuItemSelected` 723+).
  No shortcut text is displayed next to menu items today.
- `BusyOverlay::keyPressed` (`rock-hero-editor/ui/src/busy/busy_overlay.cpp:248`) swallows keys
  while the busy overlay has focus — this continues to block shortcuts under the new system
  because a focused component that consumes a key stops propagation before any top-level mapping.
- Settings: `IEditorSettings`
  (`rock-hero-editor/core/include/rock_hero/editor/core/settings/i_editor_settings.h`) already has
  an opaque-serialized-string pattern (`audioDeviceState()`/`setAudioDeviceState`, lines 66-74);
  the implementation is a versioned `juce::PropertiesFile` stored as XML
  (`rock-hero-editor/core/src/settings/editor_settings.cpp`, `g_settings_xml_format_version{1}`).
- Existing tests touching key handling: `rock-hero-editor/ui/tests/test_editor_view_state.cpp`
  (103-104, 187-188 for undo/redo keys; 97-99, 177-181 for `menuItemSelected`) and
  `rock-hero-editor/ui/tests/test_editor_view_timeline.cpp:1055` (Space).
- History: docs/plans/completed/plugin-window-transport-key-routing-plan.md records why the native hook
  and duplicated predicates exist (VST3 keyboard contract, single-HWND JUCE plugin editors,
  `WM_GETDLGCODE` uselessness) — that mechanism is kept; only its binding data becomes injected.

Framework facts (verified in the vendored submodule under
`external/tracktion_engine/modules/juce/modules/`; load-bearing for the build-on-JUCE decision):

- One `juce::ApplicationCommandManager` owns one `juce::KeyPressMappingSet`
  (juce_gui_basics/commands/juce_ApplicationCommandManager.cpp:38-42); the set is a `KeyListener`
  + `ChangeBroadcaster` (juce_KeyPressMappingSet.h:95-97) attached to a top-level component; on
  match it invokes synchronously on the message thread (juce_KeyPressMappingSet.cpp:204-219;
  message-thread assert juce_ApplicationCommandManager.cpp:194-196).
- Complete runtime rebinding API with change notification: add/remove/clear/reset/lookup
  (juce_KeyPressMappingSet.cpp:65-202); multiple alternative keypresses per command are
  first-class (juce_KeyPressMappingSet.h:229-234).
- Conflict detection is a query (`findCommandForKeyPress`, juce_KeyPressMappingSet.cpp:186-193),
  NOT enforced: `addKeyPress`'s doc comment claims conflicting keys are removed
  (juce_KeyPressMappingSet.h:133-134) but the code does no such removal
  (juce_KeyPressMappingSet.cpp:72-104) — a bindings UI must do remove-then-add itself, as the
  stock editor does (juce_gui_extra/misc/juce_KeyMappingEditorComponent.cpp:159-192).
- XML persistence keyed on hex CommandID with a diff-versus-defaults mode so shipped default
  changes merge under user overrides (juce_KeyPressMappingSet.cpp:222-319; diff rationale
  juce_KeyPressMappingSet.h:205-211). Command ids must be stable forever; all targets must be
  registered before `restoreFromXml` (juce_KeyPressMappingSet.cpp:87-102); stale ids are ignored
  in release but hit a debug `jassertfalse` (juce_KeyPressMappingSet.cpp:101).
- `PopupMenu` auto-displays a command's shortcut by querying the manager's mapping set
  (juce_gui_basics/menus/juce_PopupMenu.cpp:309-322). Display names via
  `KeyPress::getTextDescription()` (juce_gui_basics/keyboard/juce_KeyPress.cpp:238-281),
  English-only; the persisted `key` attribute IS the display string.
- Exact multi-modifier matching (juce_KeyPress.cpp:52-63); on Windows `commandModifier ==
  ctrlModifier` at compile time (juce_ModifierKeys.h:164-166).
- Focus routing: from the focused component up the parent chain, each level's key listeners run
  before that component's own `keyPressed` (juce_gui_basics/windows/juce_ComponentPeer.cpp:196-221)
  — so focused text editors get first refusal before a top-level mapping set. Command targets
  resolve at invocation time along the focused `ApplicationCommandTarget` chain
  (juce_ApplicationCommandManager.cpp:215-303, juce_ApplicationCommandTarget.cpp:64-132).
- Hard limits: one key → one command per manager; no chords (juce_KeyPressMappingSet.h:229-234);
  no mouse buttons (juce_ComponentPeer.cpp:189-194); disabled-command fallthrough in `keyPressed`
  (juce_KeyPressMappingSet.cpp:326-354) is an accident — never design on it.

Verified against code on 2026-07-06, refactor @ 13e82fb0.

## Dependencies

- docs/plans/roadmap/26-game-startup-menus-library.md — mirrors open question Q2 (its open question 4);
  its Phase 5 menu input mirrors this plan's overwrite-and-clear conflict semantics without
  sharing code if Q2 resolves to parallel systems.
- Consumers (they depend on this plan, not vice versa): docs/plans/roadmap/40-chart-editing.md (interim
  keybind table hands its shortcuts to this registry — its line "new shortcuts start in the
  interim keybind table handed to docs/plans/roadmap/46-editor-keybinds.md"),
  docs/plans/roadmap/41-tempo-map-authoring.md (tap key and anchor-nudge keys register here when this
  lands), docs/plans/roadmap/44-editor-3d-preview.md (the preview window is one more consumer of the
  centralized map).
- docs/plans/in-progress/tone-track-tempo-map-plan.md — active work; any shortcut it ships (Ctrl+T tone
  marker) is migrated into the registry during Phase 1, referenced never absorbed.
- External decisions: Q1–Q3 below (Phase 0 gate).

## Decisions already made

- **Build on `juce::ApplicationCommandManager` + `juce::KeyPressMappingSet`, wrapped thinly —
  decided by this plan** from the verified framework facts in the Current state inventory: the
  pair provides runtime rebinding with change notifications, alternatives per command, diff-based
  XML persistence, automatic menu shortcut display, and focus-driven per-context enablement. The
  caveats (stable ids forever, own bindings UI with remove-then-add, no chords/mouse buttons) are
  all handled at the wrapper layer. A custom system would re-implement all of the above for zero
  additional capability this plan needs.
- **Undo/redo never yield to plugin text fields; Space yields when the plugin's focused control
  wants it** — docs/plans/completed/plugin-window-transport-key-routing-plan.md; preserved unchanged.
  The yield policy keys off command identity (`disposeCommandKey`,
  `rock-hero-common/audio/src/tracktion/plugin_window.cpp:360-382`), so it survives rebinding.
- **The plugin-window → editor command routing stays decoupled via
  `PluginWindowCommandDispatcher`/`PluginWindowCommandObserver`** — existing seam at
  `plugin_window.h:39-40` and `i_plugin_host.h:315`; this plan adds the mirror-image inbound seam
  (injected bindings), it does not restructure the outbound one.
- **Interim policy until this plan lands**: new shortcuts from plans 40/41 live in
  `EditorView::keyPressed` and are recorded in plan 40's interim keybind table —
  docs/plans/roadmap/40-chart-editing.md (Dependencies) and docs/plans/roadmap/41-tempo-map-authoring.md
  (Dependencies).
- **"L links notes / auto-creates slides"** is settled —
  docs/plans/in-progress/note-format-and-tablature-plan.md; Appendix tier B reserves plain `L`
  accordingly and this plan must not reassign it.
- **Charter's shortcut list is input, not a template** — the user disagrees with many of its
  choices; the Appendix is a from-scratch proposal and an explicit review gate (Q1).
- **Undo, Redo, and Play/Pause are non-rebindable core commands** — user decision 2026-07-20
  (answering Q3 by dissolving it): the three plugin-window-mirrored commands are definitively
  mapped where every user expects them and ship as fixed, non-rebindable registry rows (the same
  policy as the grammar keys). Consequences: the Win32 hook's hardcoded matching stays correct
  forever, Phase 4's injection seam is unnecessary, and `Ctrl+Shift+Z` joins `Ctrl+Y` as a
  first-class Redo alternative (DAW muscle memory; letter+Ctrl+Shift is hook-mirrorable). Making
  them rebindable later is purely additive — that is the moment Phase 4's original seam design
  (preserved below) becomes relevant again.
- **The pointer-modifier vocabulary is fixed** by `docs/plans/in-progress/editing-interaction-model.md`
  (settled 2026-07-09; keyboard grammar re-settled 2026-07-16/18 — re-read that doc plus
  `docs/plans/in-progress/chart-span-and-selection-model.md` §9/§9a at G46-KEYMAP; the list
  below was refreshed 2026-07-18 by the marker fold-in audit after two amendments had staled
  it): Ctrl = precision (snap bypass), Alt = the authoring/mutation modifier, Shift =
  extend/constrain. Keybind defaults must not repurpose these modifiers' *pointer* meanings;
  keyboard chords using them (Ctrl+Z, Alt+F4) are unaffected. The model additionally reserves
  keyboard verbs that register here when their surfaces land: Delete = delete selection, Esc =
  the marker Esc ladder (gesture → disarm → selection), plain arrows = move/arm the caret
  (Ctrl+Left/Right = measure jump), Alt+arrows = nudge the selection (Ctrl fine),
  Shift+Alt+Left/Right = extent resize, Shift+arrows = the plan-52 range gesture, digits 0–9 =
  fret typing, Space = play from the marker, Ctrl+D = duplicate, and `B` is held in reserve
  for a possible latched pencil mode. The grammar keys (digits, Esc, arrows, Space) register
  as commands whose `perform` branches (Phase 1 policy) but ship as
  non-rebindable-by-default rows, so a stray rebind cannot silently break the typing rule.

## Open questions for the user — ALL CLOSED 2026-07-20

- **Q1 — ANSWERED**: the signed keymap matrix (docs/plans/in-progress/keymap-matrix.md) is the
  authoritative default map, superseding the Appendix's tier A rows where they differ; tier B
  stays reservation-only.
- **Q2 — ANSWERED: (a) parallel systems**, confirmed by a deep-dive with the user: a shared
  common concept would have exactly one consumer per feature (the editor must sit on JUCE's
  `KeyPressMappingSet` to get menu display, focus dispatch, and diff persistence; the game's
  `MenuBindings` is a modifier-free device+code map; no action vocabulary overlaps). Shared
  semantics stay documented conventions. The extraction trigger — the editor wanting
  non-keyboard input (e.g. MIDI-pedal transport) — is recorded in docs/tracking/watch-items.md;
  extract `MenuBindings` to common only when that real second consumer appears.
- **Q3 — SUPERSEDED** by the non-rebindable-core-commands decision (see Decisions already made):
  with the trio fixed, no rebind can ever produce a non-mirrorable chord, so neither capture
  restriction nor a warning note is needed.

The original question text is kept below for the decision record.

1. **Q1 — Default keymap appendix sign-off.** The Appendix below proposes tier A defaults
   (commands existing at baseline). Options: (a) approve tier A as listed; (b) edit specific rows.
   Tier B rows are reservations only and get their real review when plans 40/41 register those
   commands. **Recommendation: (a)** — tier A is deliberately conservative (existing de-facto keys
   plus uncontroversial file-menu standards); diff-mode persistence means later default changes
   merge under user overrides, so nothing here is locked in except the command *ids*.
2. **Q2 — Editor/game bindable-action sharing** (mirrored in
   docs/plans/roadmap/26-game-startup-menus-library.md, its open question 4). Options: (a) parallel
   systems — editor uses JUCE command dispatch on the message thread; the game uses a headless
   binding resolver over plain input events (keyboard + gamepad + MIDI pedal) in game/core; only
   the *semantics* (stable action ids, overwrite-and-clear conflict flow, diff persistence) are
   shared as conventions. (b) one shared bindable-action concept in rock-hero-common, extracted
   first per constraint (a). **Recommendation: (a)** — the input models differ in kind
   (event-driven `juce::KeyPress` vs polled multi-source game input; JUCE cannot bind gamepads,
   mouse buttons, or MIDI), so a shared abstraction would be a lowest-common-denominator layer
   neither side wants. If (b) is chosen, Phase 1's registry data types move to rock-hero-common as
   a new extraction phase with tests before either product consumes them.
3. **Q3 — Rebinding the three plugin-window-mirrored commands to non-mirrorable chords.** The
   Win32 hook can only match chords expressible as virtual-key + ctrl/shift state; Phase 4 defines
   the mirrorable set as printable character keys and Space with optional Ctrl/Shift (Alt excluded,
   as today — `plugin_window.cpp:299`). Options: (a) restrict capture for Undo/Redo/PlayPause to
   mirrorable chords; (b) allow any chord and show a persistent "not active while a plugin window
   is focused" note on that row. **Recommendation: (b)** — no silent surprises, no arbitrary
   capture restrictions; the note makes the trade visible.

## Phased implementation

### Phase 0 — Decision gate — COMPLETE 2026-07-20

- **Scope**: present Q1–Q3 with the Appendix and the build-on-JUCE decision rationale; no code.
- **Exit criteria**: user sign-off recorded in this file (edit the Status line and Q answers).
- **Done**: all three questions closed 2026-07-20 (see the Status line and the Open questions
  section); the gate record lives in docs/plans/roadmap/00-roadmap.md's gate table.

### Phase 1 — Command registry and manager spine (assumes Q2 = (a)) — COMPLETE 2026-07-20

> **Execution record (2026-07-20, with Phase 5 folded in):** landed as planned with two
> recorded refinements. (i) **Grammar keys stay in the view's grammar decoder** rather than
> registering as branching-perform commands: their dispatch is sequential and context-ordered
> (digit fallthrough lanes→chart, the Esc ladder, Delete's propagate-when-empty), which flat
> command dispatch cannot express honestly; their chords are instead *reserved* in the registry
> conventions (`editor_command_id.h`) and the Phase 3 dialog surfaces them as fixed rows. The
> `+/-` grid/zoom family also stays decoder-side — its union match over key codes, text chars,
> and numpad shapes (WM_CHAR timing) is inexpressible as exact `KeyPress` entries. (ii) The
> **non-rebindable core trio** registers with fixed chords, and `Ctrl+Shift+Z` ships as Redo's
> registered alternative (editor-side; the plugin-window mirror rides Phase 4). Phase 5's
> substance landed here too: tier A defaults applied (File chords are live command items with
> shortcut display), growth conventions recorded in the registry header, and the locked-table
> test pins ids + defaults. `Ctrl+T` is a registered command — exact modifier matching supplies
> the against-Alt guard. Delivered: `rock-hero-editor/ui/src/keybinds/` (id enum, registry
> table), `EditorView` as `ApplicationCommandTarget` owning the manager, the mapping set
> attached on `MainWindow` (manual forwarding kept per plan), menus migrated to
> `addCommandItem` (strings submenu stays raw ids), editor-side predicates deleted, preview
> whitelist re-expressed over command ids, tests migrated + locked-table/mapping/dispatch
> coverage added. Build + editor UI suite green.

- **Scope**: introduce the single command vocabulary and wire existing behavior through it,
  preserving every current shortcut exactly. One `juce::ApplicationCommandManager` owned by the
  editor UI composition; `EditorCommandId` enum with explicit, append-only, never-reused values
  (persistence keys off the hex id forever); a registry table (id → name, category, default
  keypresses, enablement source). `EditorView` (or a small dedicated target class in the new
  feature folder) implements `juce::ApplicationCommandTarget`: `getCommandInfo` derives
  `setActive` from `EditorViewState` (undo_enabled, redo_enabled, busy, project_loaded);
  `perform` maps to the existing `IEditorController` intents — UI keeps presenting state and
  emitting intents per docs/design/architectural-principles.md "UI Modules". Attach
  `commandManager.getKeyMappings()` as a key listener on `MainWindow`; keep the manual
  `MainWindow::keyPressed` forwarding in place during this phase as a belt-and-braces fallback
  (removed in Phase 3 after parity is proven). Migrate `getMenuForIndex` items with shortcuts to
  `PopupMenu::addCommandItem` so shortcut text displays automatically; menu items without
  commands (e.g. the tablature-strings radio submenu) stay raw ids for now. Call
  `commandStatusChanged()` when `EditorViewState` changes so enablement refreshes. Delete the
  duplicated predicates in `editor_view.cpp:122-147`. Migrate the in-flight Ctrl+T tone-marker
  shortcut if it has landed by execution time (re-verify inventory first). One command vocabulary
  policy, recorded here: within the single manager a key maps to exactly one command; context
  sensitivity is expressed through focused-target enablement, never through the
  disabled-fallthrough accident; a future context-overloaded key (plan 40) is one command whose
  `perform` branches, or two commands with two keys — never one key on two ids.
- **Files/modules**: new `rock-hero-editor/ui/src/keybinds/` feature folder
  (`editor_command_id.h`, `editor_command_registry.h/.cpp`);
  `rock-hero-editor/ui/src/main_window/editor_view.h/.cpp`;
  `rock-hero-editor/ui/include/rock_hero/editor/ui/main_window/main_window.h` +
  `src/main_window/main_window.cpp` (listener attach);
  `rock-hero-editor/ui/include/rock_hero/editor/ui/main_window/editor.h` if the wrapper must
  expose the manager to `MainWindow`.
- **Public-header impact**: none beyond a possible accessor on the existing `Editor` wrapper /
  `MainWindow` headers; all new headers stay in `src/` (visibility rule: `src/` first).
- **Testing**: update `rock-hero-editor/ui/tests/test_editor_view_state.cpp` and
  `test_editor_view_timeline.cpp` to drive the mapping set / command invocation instead of raw
  `keyPressed`; new tests prove (i) Ctrl+Z/Ctrl+Y/Space invoke the same controller intents as
  before, (ii) disabled state blocks invocation, (iii) command ids match the locked table (a
  static table-equality test that fails on any renumbering). Tests are synchronous wiring checks
  per docs/design/architectural-principles.md "Selective UI Wiring Tests"; no pixel output (JUCE 8
  headless pixel tests would need SoftwareImageType — avoided by not painting).
- **Exit criteria**: all three existing shortcuts work identically (including while the busy
  overlay is up — blocked), menus show shortcut text, duplicated editor-side predicates deleted.
- **Verification**:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_editor_ui_tests
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 2 — Persistence and restore — COMPLETE 2026-07-20

> **Execution record (2026-07-20):** landed as planned via `IEditorSettings::keymapXml()` /
> `setKeymapXml()` (a fresh opaque-blob pattern — the audioDeviceState precedent this plan
> cited had since moved to the AudioConfigStore) and a new
> `rock-hero-editor/ui/src/keybinds/editor_keymap_persistence.{h,cpp}` unit owned by the
> `Editor` composition wrapper (constructed after the view, satisfying the
> register-before-restore contract). One strengthening beyond plan: the pre-restore filter
> drops **non-rebindable** entries as well as unknown ids, so a hand-edited blob cannot
> override the fixed core trio. Saves are equality-gated against the stored blob (no writes
> from the restore's own broadcast). `NullEditorSettings` un-finaled so recording fakes can
> subclass it. Tests: settings round-trip (core), rebind→diff save + clear-on-reset, restart
> restore, unrestorable-entry filtering under debug asserts, corrupt-blob tolerance (ui). All
> suites green.

- **Scope**: persist user overrides as `createXml(true)` diff XML (shipped default changes merge
  under overrides). Extend `IEditorSettings` with the established opaque-blob pattern
  (`keymapXml() const -> std::optional<std::string>`,
  `setKeymapXml(std::optional<std::string>)`, mirroring `audioDeviceState`,
  `i_editor_settings.h:66-74`). Restore order is a hard constraint: register every command first,
  then `restoreFromXml`. Pre-filter `<MAPPING>`/`<UNMAPPING>` entries whose command id is unknown
  before calling `restoreFromXml` — release JUCE ignores them but debug builds hit
  `jassertfalse` (juce_KeyPressMappingSet.cpp:101), and a downgraded editor reading a newer keymap
  must not trip it. Save on every mapping change via the set's `ChangeBroadcaster`.
- **Files/modules**:
  `rock-hero-editor/core/include/rock_hero/editor/core/settings/i_editor_settings.h`,
  `rock-hero-editor/core/src/settings/editor_settings.cpp`,
  `rock-hero-editor/core/tests/include/rock_hero/editor/core/testing/null_editor_settings.h`
  (and any other fakes implementing the port), `rock-hero-editor/ui/src/keybinds/` (restore/save
  glue).
- **Public-header impact**: `i_editor_settings.h` gains two pure virtuals — a port change; every
  implementer/fake updates in the same commit.
- **Testing**: `rock-hero-editor/core/tests/test_editor_settings.cpp` round-trips the blob;
  UI-side tests prove rebind → serialized diff contains the mapping, restart-simulation (fresh
  manager + registry + restore) reproduces it, and a blob referencing a retired id restores
  cleanly with the unknown entry dropped.
- **Exit criteria**: rebinds survive an editor restart; defaults-only state stores no mappings
  (pure diff); stale-id blobs are tolerated in debug builds.
- **Verification**:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_editor_core_tests
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 3 — Keyboard Shortcuts configuration UI (stock component, themed — decided 2026-07-20) — CODE COMPLETE 2026-07-20, pending the user's in-action review

> **Execution record (2026-07-20):** landed as scoped — `KeyboardShortcutsWindow`
> (`ui/src/keybinds/keyboard_shortcuts_window.{h,cpp}`) hosts the stock component in a
> non-modal, survive-close tool window; the window-local LookAndFeel delegates
> `createAlertWindow` to `LookAndFeel_V2` (exact stock button/modal-result semantics, no V4
> padding); `getCommandInfo` derives `readOnlyInKeyEditor` from the registry's rebindable
> flag; `ShowKeyboardShortcuts` (0x1103, menu-only) joins the registry and the Edit menu;
> `juce_gui_extra` wired. Tests: read-only flags for every registry row, window
> hosting/survive-close, command-item + perform wiring; locked-table extended. **One scope
> revision recorded:** the `MainWindow::keyPressed` manual forwarding is NOT removed — the
> original removal assumed every key becomes a command, but with the grammar keys staying in
> the view's decoder (Phase 1 refinement) the forwarding is the only grammar-key path when
> native focus sits on the window shell; commands never reach it (the mapping-set listener
> runs first). The remaining exit criterion is the user witnessing the themed stock dialog in
> the running editor — the recorded custom-rebuild trigger.
>
> **TRIGGER FIRED 2026-07-20 (same day):** the user judged the themed stock dialog "definitely
> a bit ugly" in live use — the recorded off-product trigger — and directed the custom rebuild
> on branch `custom-keybind-menu`. `KeymapEditorView` (`ui/src/keybinds/keymap_editor_view.*`)
> replaces the stock hosting: EditorTheme-painted category headers and command rows, chip
> buttons per binding (change/remove via a deletion-checked popup; `+` opens a themed
> press-a-key capture dialog with live owner preview), overwrite-and-clear through a themed
> confirm plus the public remove-then-add dance, reset-all behind a themed confirm, rows
> rebuilt on the set's async change broadcasts, and `applyBindingChange` refusing
> non-rebindable commands outright (no dialog path can alias the core trio). The stock
> component, its window-local LookAndFeel, and the `juce_gui_extra` link are gone; the
> `readOnlyInKeyEditor` flag stays as correct command metadata. Per-command reset remains the
> recorded uncommitted future enhancement — now trivially addable since the component is ours.
> Tests cover row structure, one-owner semantics, non-rebindable refusal, live refresh, and
> the window wiring. Merging this branch completes the phase's in-action review.

- **Scope**: one menu entry ("Edit > Keyboard Shortcuts...") opening a themed window that hosts
  the stock `juce::KeyMappingEditorComponent` over the editor's mapping set: commands grouped by
  the registry's categories, chips per binding (multiple alternatives first-class, stock displays
  up to 3 per command), press-to-capture with live conflict display, per-binding change/remove,
  and reset-all-with-confirm — all stock behavior. Additions on our side: (i) a small
  component-local LookAndFeel overriding `createAlertWindow` so the two factory-path confirm
  popups follow the editor's direct-construction dialog pattern instead of the padded factory
  (see `themed_message_box.h`); (ii) `getCommandInfo` sets
  `ApplicationCommandInfo::readOnlyInKeyEditor` from the registry's `rebindable` flag so the
  non-rebindable core trio renders as fixed rows; (iii) `EditorTheme` colours via
  `setColours` plus the `drawKeymapChangeButton`/TreeView LnF hooks as needed; (iv) the
  `juce_gui_extra` module wired into the editor UI target (not linked today). Remove the
  `MainWindow::keyPressed` manual forwarding once dialog-era regression tests prove KPMS parity.

  **Decision record (settled with the user 2026-07-20 after a full vendored-source analysis;
  supersedes the original custom-component decision):** adopt stock and judge it in action
  before building anything custom. The analysis that grounds this: the original objections to
  stock were retracted — its conflict flow IS the required confirm-then-remove-then-add naming
  the current owner (juce_KeyMappingEditorComponent.cpp:159-192), and its popups match the
  product's real dialog look, because the editor's "themed" boxes are stock-V4-painted
  (`themed_message_box.cpp` sets no LookAndFeel; `AlertWindow` uses its associated component
  for positioning only, juce_AlertWindow.cpp:467) while the factory-path confirms resolve
  their LookAndFeel from the associated component (juce_AlertWindowHelpers.h:80-92), which
  stock sets to itself — reachable by a component-local LnF, no global default needed. What
  stock cannot do (the private-class wall): per-command reset-to-default inside rows — kept as
  a **possible future enhancement, not committed** (global reset + per-binding remove cover
  today's need; building it requires the custom key-mapping component) — more than 3 displayed
  aliases per command (private `maxNumAssignments`, juce_KeyMappingEditorComponent.cpp:287),
  and future per-row surfaces (search, reserved-grammar rows with custom copy). **Custom-rebuild
  triggers, recorded:** committing to per-command reset or any of the above, or the themed
  stock dialog reading as off-product in live use. The swap stays cheap because the
  registry/persistence substrate is dialog-agnostic; the interaction logic is public-API code
  our tests already drive.
- **Files/modules**: `rock-hero-editor/ui/src/keybinds/` (window shell + LnF subclass),
  `editor_view.cpp` (menu entry), `rock-hero-editor/ui/src/main_window/main_window.cpp` (delete
  forwarding), `rock-hero-editor/ui/CMakeLists.txt` + the JUCE module wiring for
  `juce_gui_extra`.
- **Public-header impact**: none.
- **Testing**: headless wiring tests against the mapping set (no pixel assertions): conflict
  remove-then-add leaves exactly one owner; per-binding remove empties a command's list;
  reset-all restores the full default set; every non-rebindable registry row carries
  `readOnlyInKeyEditor` in its command info; the persistence round-trip from Phase 2 already
  covers rebind-survives-restart. The stock component's internals stay framework-tested.
- **Exit criteria**: a user can view defaults, rebind, resolve a conflict via
  overwrite-and-clear, clear a binding, and reset — all persisted across restart; the
  non-rebindable trio is visibly fixed; manual forwarding gone; the themed result witnessed in
  the running editor (the recorded trigger for a custom rebuild is it reading as off-product).
- **Verification**:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_editor_ui_tests
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 4 — Plugin-window shortcut injection seam (RESCOPED 2026-07-20)

> **RESCOPED by the non-rebindable-core-commands decision:** with Undo/Redo/Play-Pause fixed,
> no injected-bindings seam, port setter, or editor-side push is needed — the hardcoded trio
> stays correct by construction. What remains of this phase: (i) add `Ctrl+Shift+Z` as a Redo
> alternative to the plugin-window matching (both the JUCE `keyPressed` predicates and the
> VK-code hook copy), matching the registry's default map; (ii) optionally collapse the three
> hand-synced predicate copies into one shared pure helper unit consumed by both paths — kept
> from the original scope because the duplication is a real maintenance trap
> (docs/developer/keyboard-input.md documents it). The original full-seam design below is
> preserved for the day the trio ever becomes rebindable; do not execute it before then.

- **Scope**: replace the hardcoded binding copies in `rock-hero-common/audio` with injected data,
  keeping common free of editor dependencies (constraint (a)). New public value type in
  common/audio, e.g. `PluginWindowShortcutChord { char32_t character; bool ctrl; bool shift; }`
  (Space modeled explicitly; Alt intentionally unrepresentable, matching today's rejection at
  `plugin_window.cpp:299`) and `PluginWindowShortcutBindings` holding chord lists for Undo, Redo,
  and PlayPause. New port setter `IPluginHost::setPluginWindowShortcuts(...)` beside
  `setPluginWindowCommandObserver` (`i_plugin_host.h:315`), forwarded by `Engine` into shared
  state that `PluginWindow` reads (same wiring shape as the dispatcher,
  `rock-hero-common/audio/src/engine/engine.cpp:84`). Matching is centralized in one pure helper
  unit consumed by both paths: the JUCE `keyPressed` path (compare against `juce::KeyPress` with
  letter-case normalization) and the Win32 hook path (chord → VK: uppercase ASCII for letters and
  digits, `VK_SPACE` for Space; chords outside that set simply do not mirror — per Q3(b), the
  config UI shows the "not active in plugin windows" note on affected rows). When no bindings
  have been injected, the built-in trio (Ctrl+Z / Ctrl+Y / Space) applies, keeping common/audio
  behavior-identical without an editor. The editor pushes bindings at startup after keymap
  restore and again on every mapping change. `disposeCommandKey`'s Space-yield policy is
  untouched. Delete the predicate copies at `plugin_window.cpp:17-47` and the hardcoded matching
  in `commandForWindowsKeyMessage` (277-315) in favor of the shared helper.
- **Files/modules**: new
  `rock-hero-common/audio/include/rock_hero/common/audio/plugin/plugin_window_shortcuts.h`;
  `rock-hero-common/audio/include/rock_hero/common/audio/plugin/i_plugin_host.h`;
  `rock-hero-common/audio/include/rock_hero/common/audio/engine/engine.h`;
  `rock-hero-common/audio/src/engine/engine_plugin_host.cpp`, `src/engine/engine_impl.h`,
  `src/engine/engine.cpp`; `rock-hero-common/audio/src/tracktion/plugin_window.h/.cpp`;
  `rock-hero-common/audio/tests/include/rock_hero/common/audio/testing/recording_plugin_host.h`;
  editor side: `rock-hero-editor/core/src/controller/editor_controller.cpp` (or the UI keybinds
  glue — whichever owns observer wiring at execution time) pushes bindings.
- **Public-header impact**: one new public header (the value type) plus one pure virtual on
  `IPluginHost` and its `Engine` override — minimal, justified by the port pattern
  (docs/design/architectural-principles.md "Ports and Adapters"); Tracktion headers stay isolated
  to implementation files.
- **Testing**: `rock-hero-common/audio/tests` — pure helper tests for chord matching (case
  normalization, modifier exactness, Space, non-mirrorable chords rejected); fake plugin host
  (`recording_plugin_host.h`) gains the setter; a test proving a non-default injected chord
  matches and the built-in trio applies when nothing is injected. Editor-side test proving
  bindings are pushed on rebind. The native-hook path cannot run in CI (needs a real message
  pump and a focused native plugin window): manual verification with the same two plugins the
  completed routing plan used (Archetype Nolly, Gateway) — rebind undo, confirm it fires from a
  focused plugin window and that typing in plugin text fields still works.
- **Exit criteria**: zero hardcoded key knowledge remains in common/audio (the built-in trio
  exists only as the uninjected default of the shared helper); rebinding undo in the editor
  changes what a focused plugin window forwards.
- **Verification**:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_common_audio_tests
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 5 — Default keymap application and growth conventions — COMPLETE 2026-07-20 (with Phase 1)

- **Scope**: apply the approved Appendix tier A defaults to the registry (any newly-bound
  file-menu commands become real command items with shortcut display). Record the growth
  convention in the registry header: (i) new commands append new explicit id values, never
  renumber or reuse; (ii) plans 40/41/44 register commands here with defaults reviewed at their
  own gates; (iii) plain letters are reserved for chart-editing tools (tier B) — global
  destructive actions always carry a modifier; (iv) one key never maps to two command ids.
- **Files/modules**: `rock-hero-editor/ui/src/keybinds/editor_command_registry.cpp`,
  `editor_command_id.h`, `editor_view.cpp` (menu items).
- **Public-header impact**: none.
- **Testing**: the locked-table test from Phase 1 extends to the approved defaults (default map
  equality test — any accidental default change fails CI).
- **Exit criteria**: shipped defaults match the approved Appendix; conventions documented in the
  registry header.
- **Verification**:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_editor_ui_tests
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

## Final acceptance phase

Run the sanctioned bundle as separate invocations, then pre-commit, per constraint (h):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Manual acceptance: rebind undo to a custom chord, restart, confirm persistence; trigger it from
the main window, from a focused plugin editor window (Nolly/Gateway), and confirm the busy
overlay still blocks it; verify menu shortcut text updates after the rebind.

## Rollback/abort notes

- **Phase 1** is behavior-preserving by construction; the manual `MainWindow::keyPressed`
  forwarding is deliberately kept until Phase 3, so a KPMS routing surprise (focus edge cases,
  modal interactions) degrades to today's behavior rather than dead keys. Rollback = detach the
  key listener and keep the forwarding.
- **Phase 2**: a corrupt or stale keymap blob must never brick startup — restore failures fall
  back to defaults and overwrite the blob on next save. Rollback = clear the settings key.
- **Phase 4 is the risky one** (Win32 hook + VST3 focus behavior, historically fragile per
  docs/plans/completed/plugin-window-transport-key-routing-plan.md). The uninjected built-in-trio
  default is the rollback: reverting the editor-side push restores exactly today's plugin-window
  behavior without touching common again. Do not ship Phase 4 without the manual Nolly/Gateway
  check.
- **Abort line**: Phases 1–3 deliver the full user-facing feature (central registry, config UI,
  persistence) even if Phase 4 is deferred; the only loss is plugin windows honoring rebinds of
  the three mirrored commands.

## Appendix — proposed default keymap (SUPERSEDED as default-map authority, 2026-07-20)

> The signed docs/plans/in-progress/keymap-matrix.md is now the authoritative default map (Q1);
> this Appendix stays as the file-menu-standards record and the tier B reservations. Note from
> the Q3 resolution: Undo (`Ctrl+Z`), Redo (`Ctrl+Y`, `Ctrl+Shift+Z`), and Play/Pause (`Space`)
> ship as **non-rebindable** rows.

Tier A — commands existing at baseline (or in-flight, marked):

| Category  | Command               | Proposed default     | Notes                                    |
|-----------|-----------------------|----------------------|------------------------------------------|
| File      | Open...               | Ctrl+O               | menu-only today                          |
| File      | Import...             | Ctrl+Shift+O         | avoids Ctrl+I italic muscle memory       |
| File      | Save                  | Ctrl+S               | menu-only today                          |
| File      | Save As...            | Ctrl+Shift+S         |                                          |
| File      | Publish...            | Ctrl+Shift+P         |                                          |
| File      | Close Project         | Ctrl+W               |                                          |
| File      | Exit                  | (none)               | OS-level Alt+F4; not registered          |
| Edit      | Undo                  | Ctrl+Z               | current behavior                         |
| Edit      | Redo                  | Ctrl+Y, Ctrl+Shift+Z | alternative keypresses are first-class   |
| Edit      | Keyboard Shortcuts... | (none)               | menu-only                                |
| Transport | Play/Pause            | Space                | current behavior; plugin-window mirrored |
| View      | Toggle Waveform       | (none)               | menu-only; low frequency                 |
| Tone      | Insert Tone Change    | Ctrl+T               | in-flight tone work; re-verify at exec   |

Tier B — reservations for future registration (non-binding; settled when
docs/plans/roadmap/40-chart-editing.md and docs/plans/roadmap/41-tempo-map-authoring.md register the commands):
plain `L` is reserved for link/slide per the settled decision in
docs/plans/in-progress/note-format-and-tablature-plan.md; plain letters and digits generally reserve
for chart-editing tools and fret entry; the tap-tempo key and anchor-nudge keys from plan 41
register here with their own review. Charter's shortcut list is a comparison input for those
reviews, not a default source.
