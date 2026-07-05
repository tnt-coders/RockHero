# Remaining God-Object Decomposition Plan

Status: future work. This broad audio/editor decomposition is not part of the current tempo-map or
tone-system slice. Re-read the current code before implementing it.

**Update 2026-07-04:** Part A (the engine seam split) is now scheduled as Phase 4 of
`docs/in-progress/project-structure-cleanup-plan.md`; execute it from there against current code.
Note also that this document's "never split `editor_controller.cpp`" framing was narrowed with
user sign-off: the controller **object** stays a single root facade, but its member-function
definitions distribute across per-feature translation units (see cleanup-plan §2.4 and Phase 2;
that rule is provisional until the Phase 2 pilot slice proves it, and is codified into
`docs/design/architectural-principles.md` only at the Phase 2 close-out).

## Scope

This plan came out of a fresh design-and-folder analysis. That analysis confirmed the codebase is
structurally healthy: ports-and-adapters is real, Tracktion is genuinely confined to
`common/audio`, no layering violations were found, and "one primary type per header" holds. The
directory pressure in `editor/core` (33 headers) is file *count*, not bloated files.

The analysis also confirmed that the two largest source files are **not** equal targets:

- `editor_controller.cpp` (3,552 lines) is the **deliberate root-facade end state** of the
  completed `docs/completed/editor-runtime-extraction-plan.md`. Its policy clusters were already
  extracted into `BusyOperationState`, `DeferredProjectActionState`, `PluginCatalogWorkflow`,
  `InputCalibrationWorkflow`, and (later) `SignalChainWorkflow`. That plan's Non-Goals explicitly
  forbid splitting `EditorController` for file shape or line count. **This plan does not reopen
  that.**
- `engine.cpp` (4,177 lines, one `Engine` class implementing 8 ports) is the genuine remaining
  god object. The deferred register already sanctions splitting it, but only as *real private
  implementation-seam extraction*, not a wholesale move of the monolith into a folder.

This plan therefore covers:

- **Part A** — Engine implementation-seam decomposition in `common/audio`. Activates the
  `common/audio` items currently parked in `docs/todo/editor-structure-deferred-work.md`
  ("Private Tracktion Grouping", and enables "Public Ports And Values" grouping afterward).
- **Part B** — `IEditorController` interface segmentation (ISP) in `editor/core`.

Both parts must hold behavior constant. Neither touches the public audio-port contracts or the set
of editor intents; they reshape *where code lives* and *how the controller contract is sliced*, not
*what the system does*.

## Guiding Principles

Inherited from `docs/design/architectural-principles.md`, `docs/design/coding-conventions.md`, and
the completed editor extraction plan:

- **Behavior-preserving.** These are taxonomy/structure moves. No feature, error code, or threading
  behavior changes. Verify with `rg` for stale paths/names and the full test suite each slice.
- **Measure success by what gets simpler**, not by line count. A split that only adds forwarding
  has failed — back it out.
- **One owner per state.** Splitting `Engine` across translation units must not split its *state*:
  there is one Tracktion `Edit` backing every port. This is a translation-unit split behind one
  private impl header, not a class split.
- **Framework stays at the edge.** Tracktion headers remain confined to `common/audio`
  implementation files and private implementation headers. ISP work adds no JUCE/Tracktion to
  `editor/core` contracts.
- **Folders before namespaces.** Group files into subfolders for visibility; do not introduce role
  sub-namespaces (`ports/`, `tracktion/`) just because a folder exists. The one existing precedent
  for a subfolder-paired sub-namespace is `testing/`; do not extend it here.

---

# Part A — Engine Implementation-Seam Decomposition

## Current shape (evidence)

`engine.cpp` is one translation unit with three bands:

1. **Lines ~1–1353: standalone anonymous-namespace helpers** with clear, separable clusters:
   - path/encoding utilities (`pathToUtf8String`, `normalizedPathKey`, `vst3DisplayPath`,
     `normalizedPluginPathKey`, `toJuceString`, `toHexString`);
   - plugin-scan support (`PluginScanTimeout`, `PluginIdentity`, `PluginRecord`, scan logic);
   - tone-document support (`ToneDocument`, `resolvePackageFile`, `generatedToneDocumentPath`);
   - live-rig load operation state (`LiveRigLoadOperation`).
2. **Line 1354: `struct Engine::Impl`** (declared inline in the .cpp; the single shared state
   object — Tracktion engine + `Edit` + device manager + listeners — used by every port). Some
   `Impl` methods are defined out-of-line later (`insertPluginCandidateToTrack`,
   `beginNextPluginStep`, `executePluginStep`, `yieldThenContinue`, `abortLiveRigLoad`).
3. **Lines ~2739–4177: public `Engine::` port methods**, already cleanly grouped by port:
   transport, song-audio, plugin-host, live-rig, live-input, audio-device-configuration,
   audio-meter, thumbnail-factory.

The public 8-port surface (29 headers in `include/.../common/audio`) is already well-factored. The
problem is purely the implementation monolith.

## Target shape

Keep `class Engine` and all 8 public ports exactly as they are. Move `Engine::Impl` into a private
implementation header and distribute definitions across focused translation units that all compile
into `rock_hero_common_audio`. **Start flat under `src/`** — do not pre-create a folder:

```
common/audio/src/
  engine_impl.h              - Engine::Impl declaration (shared private state + method decls)
  engine_impl.cpp            - Impl construction/teardown, listener plumbing, device binding
  engine_transport.cpp       - ITransport method definitions
  engine_song_audio.cpp      - ISongAudio method definitions
  engine_plugin_host.cpp     - IPluginHost + Impl::insertPluginCandidateToTrack
  engine_live_rig.cpp        - ILiveRig + Impl live-rig-load step machine
  engine_live_input.cpp      - ILiveInput method definitions
  engine_device_config.cpp   - IAudioDeviceConfiguration + IAudioMeterSource
  plugin_scan.{h,cpp}        - PluginScanTimeout/PluginIdentity/PluginRecord + scan logic
  tone_document.{h,cpp}      - ToneDocument + resolve/generate helpers
  audio_path_util.{h,cpp}    - path/encoding helpers (if shared across the above units)
engine.cpp                   - thin: just what cannot move (or remove if fully distributed)
```

`engine.h` stays at the public root as the composition-facing facade.

**Folder discipline (reconciled with the editor undo master plan, `../completed/editor-undo/editor-engine-undo-master-plan-v3.md`).** Introduce a
`src/tracktion/` subfolder only when a genuine private cluster has emerged and the flat folder is
demonstrably hard to scan — earn the folder, do not pre-create it. This matches the "folders before
namespaces" principle and the deferred register's warning against moving a monolith into a folder
and calling it cleaner. If/when it is earned, `tracktion_thumbnail.*` and
`tracktion_instrument_wave_device_mapping.*` (already private helpers) move with it, one step at a
time. The namespace stays `rock_hero::common::audio` regardless.

## Rollout (one seam at a time; build+test between each)

1. **Extract `engine_impl.h`.** Move the `struct Engine::Impl` declaration out of `engine.cpp` into
   a private header. `engine.cpp` includes it. No behavior change; this is the enabling step that
   lets other `.cpp` files define `Impl`/`Engine` methods. Update the CMake `target_sources` list
   and add the `src/` include reachability the test target already uses for private headers.
2. **Lift the standalone helper clusters** (`plugin_scan`, `tone_document`, path utils) into their
   own private units. These have the least coupling to `Impl` and give the largest immediate line
   reduction. Each becomes directly unit-testable in `common/audio/tests` if worthwhile (only add
   tests where the helper has real logic, e.g. plugin path normalization, tone-doc resolution).
3. **Split public port definitions** into per-port `.cpp` files (transport, song-audio,
   plugin-host, live-rig, live-input, device-config), moving the matching out-of-line `Impl`
   methods alongside the port they serve. One port per commit.
4. **Collapse `engine.cpp`** to whatever genuinely remains (likely the static command-line helpers
   and construction), or remove it if everything distributed cleanly.
5. **Optional, only after the above proves itself:** the deferred `common/audio` *public* header
   grouping (`ports/`, `values/`, `errors/`, `workflows/`) from the deferred register. Keep
   `engine.h` at the root. This is pure include-path churn; do it only if navigation is still a
   real cost after the implementation split.

## Part A exit criteria

- `class Engine` and all 8 public ports are byte-for-byte equivalent in contract; no public header
  moved (until optional step 5, which is a separate decision).
- No single `common/audio` implementation file approaches the old 4,000-line scale.
- Tracktion headers still appear only in `common/audio` private implementation files/headers.
- The full audio test suite passes unchanged; `rg` finds no stale includes.
- `Engine::Impl` is declared once in `engine_impl.h` and its single shared state is not duplicated.

---

# Part B — `IEditorController` Interface Segmentation (ISP)

## Honest scope finding (read before scheduling)

Source reading changed the premise. The naive case for ISP — "every view couples to a 35-method
interface" — is mostly **false**. The leaf views already apply ISP through their own narrow
`Listener` contracts:

- `SignalChainView::Listener`, `TransportControls::Listener`, `PluginBrowserWindow::Listener`,
  `AudioDeviceSettingsView::Listener` each expose only their own feature callbacks.
- `EditorView` is the **aggregating hub**: it implements those leaf listeners and forwards them to
  `IEditorController` (≈30 forwarding call sites). `EditorView` genuinely needs nearly the whole
  surface, so segmenting will *not* meaningfully narrow `EditorView`.

Direct `IEditorController` consumers are therefore only:

1. `EditorView` — needs ~all of it (the hub).
2. `InputCalibrationWindow` — holds `IEditorController&` but uses only the ~6 calibration intents.
3. The `recording_editor_controller` test fake — must currently stub all 35 methods.

So the genuine wins are narrow: decouple `InputCalibrationWindow`, shrink the fake, and stop the
interface from being an unbounded append target. **This is future-proofing and one real consumer
decoupling, not a broad coupling fix.** It is worth doing only if kept cheap.

## Target shape (minimal, aggregate-preserving)

Split `IEditorController` into feature sub-interfaces grouped by the intent domains that already
exist in the code, and have `IEditorController` inherit from them so existing consumers and the
aggregate stay source-compatible:

```cpp
class IProjectIntents { /* open/import/save/saveAs/publish/saveAsCancelled/close/exit/
                           unsavedChangesDecision/restoreInterruptedDecision */ };
class ITransportIntents { /* playPause/stop/waveformClicked */ };
class ISignalChainIntents { /* insertSlotSelected/remove/move/placementChanged/
                               displayTypeOverride/openPlugin */ };
class IPluginBrowserIntents { /* browserRequested/browserClosed/catalogScan/selectedInsert */ };
class IInputCalibrationIntents { /* the 6 calibration methods */ };
class IAudioDeviceIntents { /* deviceChangeRequested/settingsOpenRequested/settingsClosed */ };
class IBusyIntents { /* busyCancelRequested */ };

class IEditorController : public IProjectIntents, public ITransportIntents,
                          public ISignalChainIntents, public IPluginBrowserIntents,
                          public IInputCalibrationIntents, public IAudioDeviceIntents,
                          public IBusyIntents { /* aggregate; no new methods */ };
```

`EditorController` still implements the aggregate `IEditorController` and stays the only root
controller — consistent with the completed plan's "only root controller for the editor view"
position. This is interface segmentation, **not** one-controller-per-view (which remains a
Non-Goal).

## Rollout

1. **Define the sub-interfaces** and re-express `IEditorController` as their aggregate. No method
   bodies change; `EditorController` already implements every method. Build should be green with no
   other edits.
2. **Narrow `InputCalibrationWindow`** to take `IInputCalibrationIntents&` instead of
   `IEditorController&`. This is the one real consumer decoupling. Verify it still compiles and the
   calibration flow tests pass.
3. **Shrink the fake (optional).** Let `recording_editor_controller` compose per-domain recording
   bases, or split it so tests that only exercise one domain implement only that sub-interface.
   Only do this if it actually reduces test boilerplate; if the aggregate fake stays simpler, stop.
4. **Stop.** Do not segment `EditorView`'s dependency — it is the hub and needs the aggregate.

## Part B exit criteria

- `IEditorController` is the same intent set, expressed as an aggregate of feature sub-interfaces.
- `InputCalibrationWindow` depends only on `IInputCalibrationIntents`.
- No new controller implementation type is introduced; `EditorController` remains the root facade.
- Tests pass unchanged. If steps 2–3 do not visibly reduce coupling or fake size, Part B is judged
  not worth completing and is recorded as such rather than carried as half-done churn.

---

# Cross-Cutting Non-Goals

- Do not split `EditorController` (settled by the completed extraction plan).
- Do not split `Engine`'s *state*; one Tracktion `Edit` backs all ports.
- Do not create per-view controllers, an action router, or a broad `ProjectWorkflow`.
- Do not introduce role sub-namespaces alongside the new folders.
- Do not change any public audio-port contract or any editor intent's behavior.
- Do not update `docs/design/` durable docs from this plan until the shapes prove themselves and
  the user confirms the design is durable (per `CLAUDE.md` documentation-maintenance rules).

# Definition of Done

- Part A: `engine.cpp` is decomposed into focused implementation units behind `engine_impl.h`, no
  file near 4,000 lines, public ports untouched, audio tests green.
- Part B: `IEditorController` is an aggregate of feature sub-interfaces and
  `InputCalibrationWindow` consumes only the calibration slice — or Part B is explicitly recorded
  as not worth finishing.
- The folder-grouping follow-ups in `docs/todo/editor-structure-deferred-work.md` for `common/audio`
  are either done (as Part A step 5) or left deferred with this plan noted as their enabler.
