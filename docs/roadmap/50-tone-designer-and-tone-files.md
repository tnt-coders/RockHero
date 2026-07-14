# Plan 50 — Tone Designer and standalone tone files

Status: **Phases 1–5 complete 2026-07-13** — final acceptance pending user-triggered clang-tidy
and the witnessed manual checklist (Phase 4's real-plugin/real-guitar items) | authored
2026-07-13, all decisions user-settled (no open questions) | baseline
`work-in-progress @ 8cf9d9af`

## Goal

Make tones authorable and portable independent of any project:

1. **Tone Designer** — when no project is open, the editor's resting state is a live rig: the
   signal chain is active, the guitar is audible through it, and the user authors tones directly.
   This mirrors the game's future between-songs state (a session that is nothing but a live rig)
   and makes "plug in, hear guitar" the floor the whole app stands on. The timeline viewport keeps
   its existing "No Project Loaded" message for now — this plan activates the signal chain panel,
   not a new fullscreen designer layout.
2. **Standalone tone files (`.tone`)** — a portable single-file container carrying exactly one
   rig (plugin chain + full plugin state + output gain), no automation, no identity. The designer
   opens/saves them as documents; a project imports/exports them as copies.

## Non-goals

- Any change to the timeline viewport's no-project presentation (the "No Project Loaded" message
  stays; a richer designer layout is a later, separate decision).
- Tone-file *automation*: automation is project-specific, lives on the arrangement
  (`Arrangement::tone_automation`), and never travels in a tone file.
- The game's consumption of tone files (between-songs noodle rig, default-tone fallback for
  21-Q1's pinned enhancement, 26-Q5 starter assets). The shared container lands in `common/audio`
  so those can follow, but no game code changes here. See Forward notes.
- A tone *library/browser* UI. v1 is file choosers; a browsing surface can layer on later.
- Crash-autosave of designer scratch state (explicitly accepted limitation).
- Any change to how tones persist *inside* packages (`tones/<uuid>/tone.json` + sidecars is
  untouched; the tone file reuses that document format in a new container).

## Constraints

Applicable subset of the roadmap's non-negotiable constraints (docs/roadmap/00-roadmap.md):

- (a) **Layering**: the container + engine surface live in `rock-hero-common/audio` (Tracktion
  stays isolated to its implementation files); document/mode policy in `rock-hero-editor/core`;
  presentation in `rock-hero-editor/ui`. No game linkage.
- (b) **Ports and adapters**: the editor drives everything through the existing project-owned
  ports (`ILiveRig`, `IPluginHost`, `ILiveInput`) plus the narrow tone-file additions below; no
  Tracktion types cross a public boundary.
- (d) **Error channels**: all new fallible operations return `std::expected<…, Error>` with a
  domain-owned error type, per docs/design/coding-conventions.md. As built (Phase 1): the
  tone-file operations extend the existing `LiveRigError` domain with `InvalidToneFile` /
  `CouldNotReadToneFile` / `CouldNotWriteToneFile` rather than minting a parallel `ToneFileError`
  type — the `ILiveRig` boundary owns one failure vocabulary, and the tone file is the same
  feature's persistence.
- (h) **Builds**: all build/test commands go through `.agents/rockhero-build.ps1`; intermediate
  phases run only the checks their changes warrant; the final acceptance phase runs the
  sanctioned bundle as separate invocations.
- (i) **Real guitar input**: the designer is precisely the two-hands-on-guitar surface; nothing in
  it requires interacting with a dialog while playing (prompts appear only at explicit document
  boundaries the user initiates).

Design-doc anchors: docs/design/architectural-principles.md "Sum Types vs Interfaces" (undo
entries are the `IEdit` interface family — "tone/automation edits next" is named there as the
growth axis; this plan adds those cases), "Separate State From Side Effects" (designer document
state is pure editor-core state; engine mutations are the side effects), "Async Choreography"
(chain replacement rides idiom 3, the yielding continuation chain the rig loader already uses).

## The settled interaction model (authoritative decision record)

User-settled 2026-07-13. Three safety nets, each with exactly one job, under one organizing
principle: **prompts follow the document; undo follows the session.**

1. **Undo protects everything within a session, in both modes.** Every tone mutation — param
   gesture, chain edit, and the open/import operation itself — is one undoable `IEdit`. An
   open/import edit's memento captures the full prior chain state, the prior file association +
   dirty checkpoint (designer), and any dropped automation (project), so undo restores all of it.
   Histories attach to sessions and are never reset by loads: opening a tone file in the designer
   is an *entry in* the designer history, not a new history.
2. **Document prompts (Save / Discard / Cancel) guard the designer's file-backed document** at
   every replacement boundary: Open-over-dirty, New-over-dirty, opening/importing a project,
   exiting the app. The designer chain is a document; a project tone is not (it is project
   content, protected by project dirty + undo), so project mode has no document prompts at all.
3. **One targeted confirm guards remote destruction in project mode**: importing over a tone that
   has automation entries drops those curves — work that may live in off-screen tone regions —
   so it confirms first, stating what drops. Import over an automation-free tone is silent.
   In-use-by-N-regions alone does not prompt (changing a shared tone's sound everywhere is
   inherent to the catalog model and already prompt-free for param edits).

**Vocabulary carries the semantics — "load" is banned from the UI.**

- Designer mode: **New / Open… / Save / Save As…** — document semantics. Open repoints the file
  association; Save overwrites the associated file; Save As forks it; New clears to an empty
  untitled chain. The signal-chain header shows the document name ("Untitled" or the file's
  basename) with a `*` dirty marker.
- Project mode: **Import Tone… / Export Tone…** — copy semantics. Import replaces the *active*
  tone's chain contents (the tone keeps its catalog UUID and name; song.json references and
  toneChanges stay stable); Export writes the active tone's current rig to a file. No file
  association ever exists in project mode; the header keeps showing the catalog tone name.

**Remaining behavioral decisions (all settled):**

- **Dirty (designer)** = undo-history cursor ≠ the designer's checkpoint. The checkpoint moves on
  Open/Save/Save As/New; the open-edit memento restores the prior checkpoint, so "save A, open B,
  undo" correctly shows *clean* (chain is byte-identical to A's file). Saves are never undo
  entries — they only move the checkpoint.
- **Clean slate**: the designer starts empty (no association, clean) at app launch and after
  project close. No last-tone restore, no scratch persistence — the boundary prompts guarantee
  nothing unsaved is ever silently lost at those edges.
- **Tone file contents**: the rig payload only. No embedded name (the filename is the name), no
  tone UUID (a file can never smuggle catalog identity into a project), no automation (already
  guaranteed — every capture path runs `stripAutomationCurves`), no adopted `stable_id`s
  (importers always mint fresh durable plugin ids; file-carried ids are never trusted).
- **Transactional open/import**: the file is fully read and validated — container structure,
  document parse, sidecar containment — before the live chain is touched. Corrupt file → typed
  error dialog, chain untouched.
- **Missing plugins** → collect-all-then-refuse with the full list, chain untouched — the exact
  `LiveRigErrorCode::MissingPlugins` policy the rig loader already implements (21-Q1(A) strict
  fidelity). No placeholders, no partial imports.
- **Prompt wording**: Save / **Discard** / Cancel, matching the existing project unsaved-changes
  prompt (editor_view.cpp:1194), not "Don't Save".
- **File dialog defaults**: choosers start at the persisted last-used tone directory (new
  app-wide `IEditorSettings` accessor pair), falling back to the user home directory like every
  existing chooser; save choosers prefill `<tone name>.tone` (designer: document name or
  "Untitled"; project: the active tone's catalog name) and warn on overwrite.
- **Extension**: `.tone` (plain and self-describing; originally `.rocktone`, simplified
  2026-07-13 by user decision — the file is identified by its contents, not its extension, and
  no legacy files exist to honor). Container: ZIP with the
  in-package tone layout (`tone.json` + `state/*.tracktion-plugin`) so one document format
  serves packages and files. `tone.json`'s existing formatVersion field rides along; no
  back-compat machinery (project policy: formats just change).

**Accepted limitations (stated, not papered over):**

- Work abandoned past a declined prompt (or replaced mid-project) is recoverable only while the
  session's undo history lives — inherent to linear undo; same as any DAW.
- Plugins that mutate internal state without emitting parameter notifications cannot be
  dirty-tracked or undone — industry-standard gap; the gesture-settle flush covers everything a
  plugin announces.
- No crash autosave of the designer document.

## Current state inventory

Verified against code 2026-07-13, `work-in-progress @ 8cf9d9af`, via three source-cited
exploration passes (session/engine lifecycle, tone persistence, undo/dirty/UI).

**Engine — the rig-only session already exists at the engine layer:**

- The Tracktion `Edit`, "Backing"/"Instrument" tracks, structural gain/meter plugins, and the
  monitoring route are created **once at `Engine` construction** and never swapped
  (`rock-hero-common/audio/src/engine/engine.cpp:88-106`, `Engine::Impl::createEdit` `:15`).
  There is no per-project engine session to stand up.
- `Engine::loadLiveRig` with an **empty `tone_document_refs` list** builds a single passthrough
  "placeholder" branch and **skips the song-directory requirement**
  (`engine_live_rig.cpp:890-905` vs `:908-912`); the placeholder ref is never persisted
  (`:646-649`). Plugin insert/move/remove all operate on the audible rack branch once a rack
  exists (`engine_plugin_host.cpp:456,924-1023`). Monitoring works project-free
  (`engine_live_input.cpp:139-221`, `Engine::setLiveInputMonitoringEnabled` `:351`).
- "Signal chain disabled with no project" is therefore **editor-side gating only**, three layers:
  every signal-chain handler early-returns on `!hasLoadedArrangement()`
  (`rock-hero-editor/core/src/signal_chain/signal_chain_handlers.cpp:255,268,288,319,536,635,677,700,724`;
  output gain `:137`), the monitoring context sets
  `arrangement_loaded = m_project_audio_ready && hasLoadedArrangement()`
  (`input_calibration_handlers.cpp:19-22` → `LiveInputMonitoringDisabledReason::SessionNotReady`),
  and `SignalChainViewState`'s enable flags default false
  (`signal_chain_view_state.h:35-57`). `hasLoadedArrangement()` =
  `session().currentArrangement() != nullptr` (`editor_controller.cpp:2415`).

**Project lifecycle chokepoints (where the designer enters/leaves):**

- All project-replacing actions funnel through `requestProjectAction(EditorAction::ProjectAction)`
  (`rock-hero-editor/core/src/project/project_handlers.cpp:695`) — the single unsaved-changes
  gate with `DeferredProjectActionState` defer/replay
  (`controller/deferred_project_action_state.h`, resolution sum type `:51`). Commit seam:
  `loadSessionSong` (`project_handlers.cpp:1409`); teardown seam: `closeProject()` (`:785`).
  Startup restore: `restoreLastOpenProject()` (`:1303`). **There is no "New Project" action** —
  import (`ImportSong`) is the new-project path.
- The unsaved-changes prompt surfaces as `EditorViewState::unsaved_changes_prompt`
  (`editor_view_state.h:415`) and renders through
  `showThemedQuestionBox(…, {"Save","Discard","Cancel"}, …)`
  (`rock-hero-editor/ui/src/main_window/editor_view.cpp:1174-1222`); the themed message-box
  module supports arbitrary async N-button prompts (`ui/src/shared/themed_message_box.h:77`).

**Tone persistence (the payload the file carries):**

- `audio::ToneDocument` = `{ chain: vector<PluginRecord>, output_gain }`
  (`rock-hero-common/audio/src/live_rig/tone_document.h:63-70`); `PluginRecord` carries `id`,
  `identity`, `tracktion_state_ref`, editor-opaque `block_index` / `display_type_override`, and
  the durable `stable_id` automation key (`:25-60`). On-disk layout per tone:
  `tones/<uuid>/tone.json` + `state/plugin-N.tracktion-plugin` XML sidecars
  (`tone_document.cpp:20-21,53-65`); reader validates sidecar containment
  (`isCanonicalPluginStateRef`, `:68-77`). Read/write:
  `readToneDocument`/`writeToneDocument`/`makeToneDocumentJson` (`:144-354`); per-plugin state
  XML: `readPluginStateTree`/`makePluginStateXml` (`:357-397`).
- Capture already produces exactly this shape: `captureActiveRig`
  (`engine_live_rig.cpp:~590-810`) flushes plugin state, `createCopy` +
  `stripAutomationCurves`/`stripTempoRemapFlag` (`:735-736`), writes sidecars, appends records.
  Automation therefore **never** enters persisted plugin state
  (`tracktion/plugin_state_hygiene.{h,cpp}`; also applied at `engine_plugin_host.cpp:1261` and
  on load `engine_live_rig.cpp:1312`).
- Missing plugins on rig load: collected into `missing_plugin_names` and refused once with
  `LiveRigErrorCode::MissingPlugins` listing all of them (`engine_live_rig.cpp:1242-1359`,
  `finalizeLiveRigLoad` `:1070-1094`; `live_rig_error.h:63-69`). No placeholder substitution.
- Restore machinery for mementos exists per-plugin: `capturePluginState`
  (`engine_plugin_host.cpp:1228-1263`), `setPluginState` (`:1406-1468`),
  `recreatePluginStatePreservingId` (`:1266-1403`), id-preserving in-place copy
  `copyPluginStatePreservingInstanceId` (`:96-151`, preserves live `AUTOMATIONCURVE` children and
  the original `itemID`). Gesture-settle flush: `Engine::flushPendingPluginEdits`
  (`:1471-1479` → `plugin_dirty_tracking.cpp:137-141`), already called **before every action
  dispatch** (`editor_controller.cpp:1643`), so saves see settled state for free.
- Meter reads ride stable structural `LevelMeterPlugin`s (`engine_impl.h:137-138,642-646`), so
  chain replacement cannot re-trigger the old preset-restore meter freeze; no special handling
  needed.

**Undo / dirty / UI:**

- Product undo = RockHero-owned `EditorUndoHistory` (bounded stack of `std::unique_ptr<IEdit>`,
  clean-marker mechanism `markClean`/`hasUnsavedEdits`;
  `rock-hero-editor/core/src/controller/editor_undo_history.h:100,302,340`). Undo entries are a
  **virtual `IEdit` hierarchy carrying before/after mementos** (exemplars in
  `signal_chain/signal_chain_edits.h` — `PluginRemoveEdit` `:77` bundles plugin state +
  placement + visual state + `removed_automation` and restores them atomically); compound
  actions are modeled as **one `IEdit` carrying the whole delta**, not a transaction group.
  Apply context: `EditorEditContext` (`editor_undo_history.h:78`), assembled at
  `editor_controller.cpp:1808`. History is reset at project boundaries via `resetUndoHistory`
  (`:1871`; call sites in `project_handlers.cpp:303,439,807,834,850,926`).
- Project dirty = clean-marker delta + untracked-changes latch + save-needs-destination
  (`hasUnsavedChanges()`, `editor_controller.cpp:2480`), gated on `m_project.has_value()` — a
  designer-dirty concept has no representation today.
- User intents are `EditorAction::Action` variant cases dispatched via `std::visit` →
  `performActionImpl` overloads (`editor_action.h:543`, `editor_controller.cpp:1679`); new
  designer/file operations are new cases + `EditorActionId` entries + overloads.
- Signal-chain UI: `SignalChainPanel`/`SignalChainView` already render a header tone name
  (`setToneName`, `signal_chain_view.h:152-155`) bound to the **active tone**
  (`editor_view.cpp:511-521`; active = selected region ?? cursor tone per
  docs/in-progress/tone-active-vs-selected.md, and the audible tone mirrors it,
  `i_live_rig.h:126-128`). The panel is always visible; its controls are gated by view-state
  flags. File choosers follow an async `juce::FileChooser` + `SafePointer` pattern with
  `m_file_chooser` ownership (`editor_view.cpp:1059-1155`); none persist a directory yet.
- `IEditorSettings`/`EditorSettings` (properties-file backed) is the home for the last-used tone
  directory accessor pair, beside precedents like `waveformVisible`
  (`settings/editor_settings.h:33,96-104`).
- The no-project message is `TrackViewport::Content::paint` → `"No Project Loaded"`
  (`ui/src/timeline/track_viewport.cpp:189,216`), driven by `EditorViewState::project_loaded`
  (`editor_view_state.h:286`) — untouched by this plan.

## Phased implementation

### Phase 1 — `.tone` container in common/audio (headless)

**Status: COMPLETE 2026-07-13.** As built: `src/live_rig/tone_file.{h,cpp}` (private header — no
new public surface yet, per the smaller-surface bias below); `parseToneDocumentJson` and
`pluginStateTreeFromXmlText` extracted from the tone-document module so the workspace reader and
the archive reader share one parse/validation path; errors are new `LiveRigErrorCode` values
(`InvalidToneFile`/`CouldNotReadToneFile`/`CouldNotWriteToneFile`), not a separate type (see
constraint (d)); nine-case `test_tone_file.cpp` suite green.

Scope: the portable container and its typed errors; pure file/format work, no engine or editor
changes.

- New feature files `rock-hero-common/audio/src/live_rig/tone_file.{h,cpp}` beside
  `tone_document.{h,cpp}` (same feature folder; the tone file is the tone document in a portable
  container):
  - Container: ZIP (JUCE ZIP utilities per the `.rock`/`.rhp` precedent) holding `tone.json` at
    the archive root plus `state/*.tracktion-plugin` sidecar entries — byte-identical document
    format to the in-package layout.
  - `writeToneFile(path, document, sidecar_payloads)` and
    `readToneFile(path) -> std::expected<ToneFilePayload, ToneFileError>` where
    `ToneFilePayload` bundles the parsed `ToneDocument` plus per-record state XML. The reader is
    **fully transactional**: archive open, document parse (`readToneDocument` reuse), sidecar
    containment (`isCanonicalPluginStateRef` reuse), and per-record sidecar presence are all
    validated before returning; any failure yields a typed error and no partial payload.
  - Defensive hygiene on read: strip `AUTOMATIONCURVE` children and the tempo-remap flag from
    every state tree (capture already guarantees their absence on write; the reader re-asserts
    it so a hand-edited file cannot smuggle curves in).
  - `stable_id` policy at this seam: file-carried stable ids are never surfaced to importers as
    trustworthy — the payload exposes records with ids cleared/regenerated so every import mints
    fresh durable ids. (If `readToneDocument` requires the field structurally, the writer emits
    fresh UUIDs purely for schema uniformity; the reader discards them either way.)
  - New error domain `ToneFileErrorCode`/`ToneFileError` per coding conventions (codes for:
    not-a-tone-file/unreadable archive, malformed document, missing/escaping sidecar ref,
    unwritable destination).
  - Extension constant (`.tone`) exposed alongside the read/write functions for chooser
    filters.

Public-header impact: one new public header
`include/rock_hero/common/audio/live_rig/tone_file.h` if the editor composes read/write directly;
if Phase 2 ends up owning all file IO behind the engine surface, this header stays private —
decide at Phase 2 by whichever leaves the smaller public surface (bias: smaller).

Testing: new `rock-hero-common/audio/tests/test_tone_file.cpp` — round-trip (document + sidecars
in, identical payload out), transactional-failure matrix (truncated zip, missing tone.json,
malformed JSON, sidecar ref escaping `state/`, missing sidecar entry), automation-strip
assertion (a state tree with an injected `AUTOMATIONCURVE` child comes back without it), stable-id
non-adoption.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets rock_hero_common_audio_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

(`-Configure` is warranted: new source files change the build graph.)

### Phase 2 — Engine surface: export, transactional chain replace, chain-state memento

**Status: COMPLETE 2026-07-13.** As built: `ILiveRig` gained `exportAudibleTone` /
`captureAudibleToneState` / `replaceAudibleToneFromFile` / `restoreAudibleToneState` plus the
`ToneFileExportRequest` / `AudibleToneState` types. The file replace runs cooperatively via
`ToneChainReplaceOperation` (engine_impl.h) mirroring the loader — candidates instantiate
free-floating with collect-all missing refusal — while the memento restore is **synchronous**,
because `IEdit::undo/redo` apply inline behind the busy presentation (the `instantiatesPlugin()`
precedent); both paths converge on one `swapAudibleChainPlugins` helper
(`mutateAndReroutePluginChain` + best-effort rollback + `commitPluginRemoval` hygiene for the
outgoing chain). Export and capture are pure reads — deliberately **no transport stop** (unlike
`captureActiveRig`), so a designer Save never interrupts monitoring. `pluginStateTreeFromMemento` promoted to
`src/plugin/plugin_state_memento.{h,cpp}` per the shared-helper rule. All three `ILiveRig` fakes
extended; six real-engine adapter tests green (export/capture round trips, empty-chain replace,
unreadable-file and missing-plugin refusals with the previous chain intact, memento restore).

Scope: extend the `ILiveRig` port (tone files are the same feature as the rig it serializes) with
the three operations the editor needs; all Tracktion work stays in `engine_live_rig.cpp` /
adapter units.

- **Export** — `exportAudibleTone(path) -> std::expected<void, ToneFileError>`: message-thread
  capture of the audible branch only (flush → copy → `stripAutomationCurves`/
  `stripTempoRemapFlag`, the exact `captureActiveRig` per-plugin recipe at
  `engine_live_rig.cpp:729-769`, minus the all-branches walk and the workspace write), plus the
  branch's output gain, serialized through Phase 1's writer. No song directory involved.
- **Replace** — `replaceAudibleToneFromFile(path, completion)`: read + validate via Phase 1
  (nothing mutated on failure), then rebuild the audible branch's user chain from the payload
  through the existing cooperative per-plugin loader (async choreography idiom 3;
  `executePluginStep` machinery), applying the document's output gain and placement fields.
  Missing plugins follow the loader's collect-all-then-refuse policy verbatim
  (`LiveRigErrorCode::MissingPlugins` shape) — and the refusal must leave the **previous chain
  intact**: resolve/validate every plugin candidate *before* removing the old branch contents.
- **Chain-state memento** — `captureAudibleToneState()` /
  `restoreAudibleToneState(state, completion)`: an opaque whole-branch snapshot (per-plugin
  `PluginInstanceState` blobs + records + output gain) for the editor's undo edits, built on the
  existing per-plugin capture/restore surfaces (`capturePluginState`, `setPluginState`,
  `recreatePluginStatePreservingId`). Restoring recreates the prior chain with prior instance
  ids where the machinery preserves them.
- Transport/playback posture during replace mirrors whatever the existing branch loader requires;
  no new policy.

Public-header impact: `i_live_rig.h` gains the new operations (+ the memento value type);
`engine.h` implements. `ToneFileError` becomes the shared failure vocabulary for both phases.

Testing: adapter tests in `rock-hero-common/audio/tests` to the extent the existing engine test
harness allows (export→read round-trip against a real engine chain where the suite already
instantiates one; otherwise exercise the payload plumbing with the Phase 1 fakes and leave
graph-mutation coverage to Phase 3's controller tests + the manual checklist).

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 3 — Tone Designer mode in editor/core (headless)

**Status: COMPLETE 2026-07-13.** As built, matching the design notes below: `ToneDesignerState` +
`ToneDocumentReplaceEdit` in the new `tone_designer/` feature folder; `EditorEditContext` gained
the designer-state hook; `hasActiveSignalChain()` replaced the eleven signal-chain gates (the
output-gain gate keeps its stricter project-audio-ready arm); the monitoring context treats the
designer as session-ready; one deferral machine serves both modes with designer-aware
save-requires-destination and SaveThenReplay routing; `closeProject()` leaves-then-re-enters the
designer (skipped on exit), `loadSessionSong()` leaves at commit, and the startup-restore plus
all four open/import failure paths re-enter — the resting state is self-healing at every
project-less seam. Four actions (`NewToneDocument`/`OpenToneFile`/`SaveToneFile`/`SaveToneFileAs`)
with availability rows and public `IEditorController` entry points; `toneFileDirectory` settings
accessor pair; `ToneDesignerViewState` slice. One addition beyond the plan text: the
designer-enter completion cap-guards the snapshot like every other apply site. Fourteen dedicated
harness tests cover the enter/leave matrix, dirty/save/checkpoint semantics (including
save-A/open-B/undo→clean-on-A and redo→clean-on-B), prompt deferral for project and tone actions,
Save As routing for untitled documents, and chooser/prompt cancellation; all suites green.

**Execution design notes (settled 2026-07-13 against the code, before implementation):**

- **One deferral machine.** `DeferredProjectActionState` is domain-agnostic; the designer reuses
  the controller's existing instance (designer and project modes are mutually exclusive, so only
  one deferral can exist). `requestProjectAction` gains a second gate: project-dirty defers with
  project semantics (unchanged); else designer-dirty defers the same `ProjectAction` with tone
  semantics. The prompt's subject (project vs tone document) is derived in the view from
  `project_loaded` — no new prompt state. `SaveThenReplay` in designer mode saves the tone
  (routing through the tone Save As chooser when no association exists, mirroring
  `SaveProjectAs`'s `saveAsPathChosen()` pattern); `DiscardAndReplay` in designer mode replays
  directly (no project teardown needed).
- **Checkpoint = the history's clean marker**, free in designer mode (no project save competes
  for it). The undo-past-open scenario needs only a `before_was_clean` bool in the replace
  edit's memento: after committing an **undo** of a `ToneDocumentReplaceEdit`, the controller
  calls `markClean()` iff the pre-open state was clean; after committing a **redo**, always
  (post-open state is clean by definition). The edit signals this through a designer-document
  hook on `EditorEditContext` (a pending-reconcile value the controller consumes post-commit) —
  no history re-entrancy from inside an edit.
- **Edit mementos are engine mementos, not file paths.** `ToneDocumentReplaceEdit` carries
  before/after `AudibleToneState` (captured at push time, after the async open completes),
  before/after file association, and `before_was_clean`. Redo never re-reads the tone file — a
  file changed on disk after the open must not change what redo restores. Undo/redo apply via
  the synchronous `restoreAudibleToneState`, with `instantiatesPlugin() == true` so both
  directions run behind the plugin-loading busy presentation.
- **Enter-designer sits at the final-no-project seams, not inside `closeProject()`** (open flows
  call `closeProject()` mid-flight before loading the next project): the `CloseProject` action
  tail (when no displaced project reopens), the startup restore path when no project restores,
  and open/import failure completions that end project-less. `ExitApplication` never enters the
  designer. Leave-designer happens at project open/import commit; `resetUndoHistory` already
  runs at every one of these boundaries.
- **Gate rename**: the nine signal-chain handler gates and the output-gain gate move from
  `hasLoadedArrangement()` to a `hasActiveSignalChain()` predicate (arrangement loaded OR
  designer active); the monitoring context's session-ready flag derives from the same predicate.
  Project-only intents (tone regions, automation lanes, transport) keep their existing gates.

Scope: the designer as an editor-core state, the document model (association + checkpoint +
dirty), the gate rename, the boundary prompts, and the designer's undoable open/new edits. No UI
yet.

- **Mode state** — new feature folder `rock-hero-editor/core/src/tone_designer/` holding
  `ToneDesignerState`: `{ active, std::optional<std::filesystem::path> document_path,
  checkpoint token }`. Pure state; all engine calls remain controller side effects.
- **Enter/leave at the existing chokepoints** (no new funnels):
  - Startup: when `restoreLastOpenProject()` resolves to no project → enter designer (empty-refs
    `loadLiveRig`, monitoring refresh, gates on).
  - `closeProject()` (`project_handlers.cpp:785`) → after teardown, enter designer (clean slate:
    empty chain, no association, clean history).
  - Project open/import commit → leave designer (reset designer state; the project rig load
    already replaces the rack; `resetUndoHistory` already runs at these boundaries).
- **Gate rename to what it means**: replace the signal-chain handlers' `hasLoadedArrangement()`
  checks and the monitoring context's `arrangement_loaded` derivation with a
  `hasActiveSignalChain()`-style predicate true for *project-with-arrangement or designer
  active*. The plugin browser, insert/move/remove, placement, display-type, open-plugin, and
  output-gain paths all follow; project-only intents (tone regions, automation lanes, transport)
  keep their existing project gates.
- **Actions**: new `EditorAction` cases + `EditorActionId`s + `performActionImpl` overloads for
  `NewToneDocument`, `OpenToneFile{path}`, `SaveToneFile`, `SaveToneFileAs{path}` (availability:
  designer active; Save additionally requires an association or degrades to Save As).
- **Undoable document replacement** — new `IEdit` in `tone_designer/`: `ToneDocumentReplaceEdit`
  carrying before/after of (chain memento via Phase 2, file association, checkpoint token). One
  edit class serves Open (after = file payload) and New (after = empty chain); undo/redo restores
  chain + association + checkpoint atomically, so "save A → open B → undo" lands on A,
  associated to A, **clean**. `EditorEditContext` gains the narrow designer-document hook the
  edit needs.
- **Dirty** = history cursor vs the designer checkpoint, reusing the clean-marker reachability
  semantics `EditorUndoHistory` already implements (`markClean`/`hasUnsavedEdits`), with the
  checkpoint token capturable/restorable so the edit memento can carry it. Save/Save As move the
  checkpoint only (never push an edit). `hasUnsavedChanges()` stays project-only; a parallel
  `toneDesignerHasUnsavedChanges()` feeds the prompts.
- **Boundary prompts**: extend the `requestProjectAction` gate (`project_handlers.cpp:695`) —
  when no project is open, designer-dirty defers the `ProjectAction` behind a tone-flavored
  Save/Discard/Cancel prompt and replays it on resolution, reusing the
  `DeferredProjectActionState` pattern (a parallel deferred-tone state or a widened resolution
  sum — pick whichever keeps illegal states unrepresentable, per the action/state modeling
  precedent). Open-over-dirty and New-over-dirty defer the same way. **Save-then-replay with no
  association** routes through the Save As chooser first; chooser cancel cancels the whole
  deferred action.
- **Settings**: `IEditorSettings::toneFileDirectory()` / `setToneFileDirectory()` app-wide
  accessor pair (properties-file backed, `NullEditorSettings` + fakes updated).
- View-state additions (consumed by Phase 4): designer document header text + dirty flag +
  which button set the chain panel shows (`Hidden` before rig ready / `Designer` / `Project`),
  and the tone prompt slice beside `unsaved_changes_prompt`.

Testing (editor-core, fake-driven): designer enter/leave at all three chokepoints; gate matrix
(chain ops enabled in designer, project-only ops still gated); dirty scenarios — tweak→dirty,
save→clean, undo-to-checkpoint→clean, **save A/open B/undo→clean-on-A**, redo→clean-on-B;
prompt matrix (open/new/project-open/exit over dirty vs clean; Save with/without association;
chooser-cancel aborts replay); Save never pushes an undo entry; clean-slate invariants at launch
and post-close.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets rock_hero_editor_core_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 4 — Designer UI in editor/ui

**Status: COMPLETE 2026-07-13.** As built: the signal-chain header shows
"Tone Designer - <document>[*]" with a right-aligned New/Open…/Save/Save As… strip visible only
in designer mode (`SignalChainView::setToneDesignerState`, forwarded through the panel);
`EditorView` implements the four strip intents, owns the `*.tone` choosers (open, and save
prefilled with the document name + overwrite warning, starting at the persisted tone directory
carried in `ToneDesignerViewState::chooser_directory`), routes untitled Save through Save As,
gives the unsaved-changes prompt tone-flavored copy keyed on the designer slice, and routes the
deferred-save chooser to the tone dialog in designer mode. Import/Export buttons deliberately
arrive with Phase 5 (their actions), rather than landing dead controls now. UI wiring test
covers strip visibility + intent routing; all suites green.

Scope: presentation only over Phase 3's view state.

- **Signal-chain header controls**: the panel header (which already renders the tone name via
  `setToneName`) gains the document title ("Untitled" / file basename + `*` when dirty) and the
  **New / Open… / Save / Save As…** strip in designer mode; buttons emit the Phase 3 intents.
  Project mode shows **Import Tone… / Export Tone…** instead (wired in Phase 5; the buttons land
  here behind the view-state mode so the layout is done once).
- **Choosers**: async `juce::FileChooser` per the established `m_file_chooser` + `SafePointer`
  pattern, filter `*.tone`, initial directory from the settings accessor (fallback user
  home), save prefill per the decision record, `warnAboutOverwriting` on saves; chosen
  directories persist back through the settings accessor.
- **Prompts/dialogs**: tone Save/Discard/Cancel via `showThemedQuestionBox` (same wording and
  keyboard rules as the project prompt); typed-error dialogs through the themed module —
  corrupt/invalid file, missing-plugins list (render the full collected list), unwritable
  destination.
- Menu bar: no new menus in v1 — the panel strip is the discoverable home beside the tone name;
  Edit ▸ Undo/Redo already covers the designer history.

Testing: view-state → header/button mode resolution where the existing UI test seams allow;
everything behavioral is already covered headlessly in Phase 3. Manual checklist: launch with no
project → guitar audible through an empty chain, insert/tweak/undo works; New/Open/Save/Save As
round-trip incl. dirty markers and prompts; open project from dirty designer → prompt → both
resolutions; close project → clean designer; exit-from-dirty-designer prompt; corrupt file and
missing-plugin dialogs leave the chain untouched.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 5 — Project-mode Import / Export

**Status: COMPLETE 2026-07-13.** As built: `ImportToneFile` / `ExportToneFile` /
`ResolveToneImportPrompt` actions (Import shares the chain-mutation gates; Export is a pure read
gated on the arrangement only); the automation-drop confirm rides a `ToneImportPrompt` view-state
slice + pending-import controller state (cleared at project close), with the count derived from
the active chain's durable ids at request time and everything re-derived fresh on accept;
`ToneImportEdit` generalizes the `PluginRemoveEdit` recipe to a whole chain — engine chain
mementos on both sides (shared `ToneChainSnapshot` with the designer edit), extracted automation
entries carrying their pre-import instance ids for the undo-side derived-curve rebuild, model
restore via `applyToneAutomationModel` + `rewriteDerivedToneCurve`. Identity maps stay
upsert-only: import mints fresh durable ids for the new chain and never erases the old entries,
so undo's revived instance ids find their associations intact. Catalog ref, tone name, and
regions never change. UI: Import Tone…/Export Tone… in the panel header (project mode only,
never alongside the designer strip), `*.tone` choosers (export prefilled with the active
tone's catalog name), themed Import/Cancel confirmation naming the dropped parameter count.
Three controller tests (pure-read export; promptless automation-free import + undo; confirm
flow with Cancel and Import over a calibrated project harness); all suites green.

Scope: the copy-semantics pair over the active tone, with the automation confirm and a
whole-delta undo edit.

- **Export Tone…** — `ExportToneFile{path}` action: `exportAudibleTone(path)` on the active
  (audible) tone. No project state changes, no dirty, no undo entry (it is a pure read).
- **Import Tone…** — `ImportToneFile{path}` action targeting the active tone:
  1. Read/validate via the engine surface (transactional; typed errors as in the designer).
  2. **Automation confirm**: collect the arrangement's `tone_automation` entries whose
     `plugin_id` matches the target chain's durable ids (the `stable_id`↔automation seam;
     generalizing the RemovePlugin extraction at `signal_chain_handlers.cpp:590-627`). If any
     exist, surface the confirm ("importing replaces this tone's rig and removes automation on
     N parameters") before mutating; zero entries → no prompt.
  3. Replace the chain (Phase 2), mint fresh durable ids for the imported plugins and rebind
     (`tone_handlers.cpp` identity maps, adoption precedent at `:875-919`), erase the dropped
     automation entries and any open lanes for them.
  4. Push **one** `IEdit` (`ToneImportEdit`) whose memento carries the prior chain state
     (Phase 2 memento), the prior identity bindings, and the extracted automation entries —
     undo restores chain, bindings, lanes, and curves verbatim (the `PluginRemoveEdit` pattern,
     widened to a chain).
  The tone's catalog entry (`tone_document_ref` UUID + name) and every tone region are
  untouched; project dirty flows through the normal undo-entry pipeline.
- Availability: project mode with a loaded arrangement; buttons from Phase 4 activate.

Testing (editor-core): import keeps the catalog ref/name and region schedule byte-stable in
song.json terms; automation extraction erases exactly the target tone's entries and stashes them;
undo restores curves + bindings + chain; redo re-drops; confirm requested only when entries
exist; fresh stable ids minted (never adopted from the file); export produces a file the designer
opens (cross-mode round-trip through the Phase 1 reader); project dirty set by import, not by
export.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

## Final acceptance phase

Per constraint (h), separate invocations from the repo root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

(clang-tidy at user trigger per standing policy.)

Acceptance: with no project open the editor is a live rig — guitar audible, chain editable,
undoable; tones round-trip through `.tone` files with document semantics in the designer
(New/Open/Save/Save As, dirty tracking, boundary prompts) and copy semantics in projects
(Import/Export over the active tone, catalog identity stable, automation dropped only behind the
confirm and restored by undo); opens/imports are transactional with collect-all missing-plugin
refusal; no automation and no identity ever travel in a tone file; the timeline viewport still
shows "No Project Loaded".

## Forward notes (recorded, not scoped)

- **Game reuse**: the container + `ILiveRig` surface land in `common/audio`, so the game's
  between-songs noodle rig, 21-Q1's pinned "play with default tones" enhancement, and 26-Q5's
  starter asset can consume `.tone` files without new format work.
- **Designer layout**: the "whole window becomes a tone-designer surface" idea is deliberately
  deferred; this plan keeps the viewport message. If/when a designer layout lands, the document
  model here carries over unchanged.
- **"Import into current designer document" (open B, pull in A's rig, keep editing B)** is
  deliberately excluded: `Open A → Save As over B` reaches the same outcome, and a second
  subtly-different verb would reintroduce the load-vs-open ambiguity this design eliminates.
- **Tone library/browser** over the tones directory: additive later; the settings accessor
  already remembers the user's tone folder.

## Rollback/abort notes

- Phases 1–2 are additive `common/audio` surface with no behavior change for existing flows;
  reverting them removes the container and port additions cleanly.
- Phase 3 is the substantive editor change (gates + mode). Its riskiest edit is the gate rename;
  reverting restores project-only gating and the app returns to "chain dead without a project"
  with no data implications. Designer state degrades to unused.
- Phase 4 is presentation-only; Phase 5 is additive actions over Phase 2's surface. Each reverts
  independently.
- Tone files written before a revert remain plain ZIPs of the unchanged in-package tone-document
  format — readable by any future re-land without migration (and per project policy, no
  back-compat machinery is owed to them).
