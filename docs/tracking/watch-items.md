# Watch items

**Standing registry of accepted-for-now issues, each with a trigger.** A watch item is not work
to schedule — it is a tripwire: something deliberately left as-is *until* a named condition makes
it stop being acceptable, at which point the recorded remedy applies. This is the opposite of the
[backlog](backlog.md), which holds small fixes to *do* when there is time; a watch item you
*monitor*, a backlog item you *do*.

Maintenance: every entry names its **trigger** (the moment it graduates to action) and its
**remedy** (what to do then). When a trigger fires and the item is handled, move it to
*Retired* at the bottom rather than deleting it, so the history stays auditable. When you add an
item, give it both a trigger and a remedy or it belongs in the backlog instead.

Consolidated 2026-07-11 from `docs/plans/todo/game-render-watch-items.md` and
`docs/plans/todo/plugin-idle-churn-watch.md`, with every claim re-verified against the code that day
(paths updated where the highway renderer's promotion to `rock-hero-common/ui` moved them; one
item retired as resolved — see *Retired*).

---

## Render stack (game loop + bgfx)

### GameShell is a composition point in game/ui — trigger FIRED: plan 21 started 2026-07-11

`GameShell::run` constructs concrete adapters, chooses the backend, composes the resources root,
and owns the frame loop — several of architectural-principles.md § "UI Modules" move-to-app
triggers at once. Plan 20 Phase 1 sanctioned the placement at its original size, but the shell
has since grown by accretion (plan 20 Phase 4 diagnostics wiring, plan 25 Phase 4 texture-set
loading). **DECIDED 2026-07-11 (user): inject composed dependencies from `app/`** — main.cpp
composes the engine/session/renderer/resources and GameShell receives injected ports, owning
only the frame loop and input wiring, per architectural-principles.md's move-to-app rule. The
mechanical restructuring lands with plan 21 Phase 6 (the first phase that touches the shell);
retire this entry when that phase completes.

### NSIS and the empty resource directories — trigger: next installer inspection

`install(DIRECTORY DESTINATION ...)` creates empty `resources/{fonts,sfx,textures}` under
`cmake --install`, but whether the NSIS-packaged artifact preserves empty directories is
unverified. `GameResources::create` only checks the root today
(`rock-hero-game/core/src/resources/game_resources.cpp:136`), so nothing breaks either way —
verify once when inspecting a packaged installer (dovetails with the Windows CI installer work),
and re-check the moment a resolver method starts requiring one of those subdirectories.

### shaderc include tracking — trigger: first project-owned shared `.sh` shader include

`rock_hero_add_compiled_shader` tracks the `.sc` source and `varying.def.sc` but not includes;
today's shaders (`rock-hero-common/ui/shaders/`) carry no project-owned `.sh` include, and the
only include dir is immutable Conan package content, so rebuilds are correct. When a shared
project-owned `.sh` include appears under that directory, switch the custom command to shaderc's
`--depends` output via `DEPFILE` (bgfx's own `bgfxToolUtils.cmake` demonstrates the parse).

### Stale files in the deployed resources tree — trigger: resource renames become common

`copy_directory` (build tree) and `install(DIRECTORY)` (install tree) are additive: a renamed or
removed staged asset lingers beside the executable across incremental builds. This now applies to
both products (the editor preview deploys its own shaders + textures beside its exe, like the
game). A clean build resets it. Acceptable until resource renames become routine; then make the
deploy prune (copy fresh into a scratch dir + `copy_directory_if_different`, or
delete-before-copy).

### CMakeConfigDeps generator — trigger: next Conan provider revision

The classic CMakeDeps generator declares no executable targets, which is why shaderc is located
via `find_program`. Conan's newer CMakeConfigDeps generator honors the recipe's `.exe` component
metadata and would make `bgfx::shaderc` a real executable target (and bgfx's packaged
`bgfxToolUtils.cmake` usable). Weigh migrating when next revising the cmake-conan provider — a
provider decision, not a game-CMake one.

### Interactive drag-resize stalls rendering — trigger: user-visible complaint

Win32 runs a modal loop inside `DefWindowProc` during title-bar drag/resize, freezing the polled
frame loop for the drag's duration (last frame stretches; the resize lands on release).
Fundamental to polled loops on Windows; every SDL game without a message-hook workaround behaves
this way. Escape hatch if it ever matters: `SDL_SetWindowsMessageHook` / an SDL event watcher
that redraws from inside the modal loop.

### BX_CONFIG_DEBUG re-export — trigger: relying on bgfx asserts from project TUs

bgfx compiles its own debug asserts per config, but whether the Conan CMakeDeps package
re-exports `BX_CONFIG_DEBUG` to RockHero TUs (affecting header-inline `BX_ASSERT`s compiled into
project code) is untraced. Irrelevant today — project code calls no asserting inline bx code —
but verify before designing anything that expects bgfx debug asserts to fire from our TUs.

### Minimized/occluded window may spin the loop — trigger: pacing log shows it

bgfx has zero `DXGI_STATUS_OCCLUDED` handling (success-status codes fall through its error-only
`isLost()` check), so if Windows stops throttling Present for a minimized/occluded FLIP_DISCARD
swapchain, the L2 game loop spins uncapped on one core. The Phase 3 pacing log detects this for
free (frame delta collapsing toward zero while minimized). If observed: shell-side throttle —
skip `submitFrame()` and sleep ~one refresh period on `SDL_EVENT_WINDOW_MINIMIZED`/`HIDDEN`,
resume on `RESTORED`. (The editor preview already sidesteps this by suspending its vblank ticks
on hide.)

### Silent Noop-renderer fallback on device loss — trigger: pacing silently disappears

`Context::flip()` replaces the renderer with Noop on device removal; Noop never blocks, so vsync
pacing vanishes without an error. The collapsing frame delta in the pacing log is the tell. If it
ever bites: log/assert `bgfx::getRendererType()` periodically in dev builds, and decide a recovery
policy (recreate the device vs. exit with a message).

### bgfx cannot re-initialize in-process — trigger: needing multiple init/shutdown cycles

`bgfx::renderFrame`'s single-thread pin (`s_renderFrameCalled`) survives `bgfx::shutdown()`, so a
second `renderFrame`-before-`init` cycle trips an internal assert (a `__debugbreak` in the debug
Conan package). The game is unaffected — one init per process — and the editor 3D preview works
around it with a suspend/resume lifecycle (the device lives from first open to window
destruction; hiding only stops vblank ticks). If a future need for genuine re-init appears
(multiple independent bgfx surfaces, teardown/rebuild), the remedy is a recipe-shadow patch
resetting `s_renderFrameCalled` in `shutdown`, or per-window framebuffers under one device.

## Shared scene models

### Tab and highway scene models stay un-unified — trigger: a third consumer or a padding-semantics bug

`common::core::TabViewState` (promoted from editor/core by plan 30 Phase 1, 2026-07-16) and
`HighwayViewState` deliberately keep separate note-view semantics: `HighwayNoteView` bakes
display padding into `note.string` at projection time while the tab core pads at draw time via
`extra_lanes` — merging them is a real refactor across two shipped views with no payoff today
(plan 30 §7, decided 2026-07-11). **Trigger**: a third consumer needs a shared note-view shape,
or a bug traces to the padding-semantics divergence. **Remedy**: design one note-view semantic
(probably draw-time padding), migrate both projections behind their tests, and retire this item.

## Chart editing (tab lane)

### Sustain tail-drag resize is deliberately not implemented — trigger: charters reach for the tail

Decided with the user 2026-07-16 (interaction-model amendment record): resizing a note's sustain
is covered precisely by Alt+wheel and Shift+Alt+Left/Right, and a draggable tail end is a small
target (the tail strip is ~13–19px tall, its end zone a few pixels) that would compete for grab
space with drag-move on the same note and get fiddly at low zoom. Direct manipulation loses to
the wheel here — probably. **Trigger**: watching real charting shows users grabbing sustain
tails expecting a resize and failing (or asking for it). **Remedy**: implement tail-end drag as
the standard edge-resize verb with a generous grab zone (hit-test via the shared layout
manifest's tail rectangle), live preview, Esc cancel, single undo entry.

### Pointer drag-move of notes is deliberately not implemented — trigger: charters reach for the drag

Decided with the user 2026-07-18 (parked alongside tail-drag below, same reasoning family): moving
notes is covered precisely by Alt+arrows (grid step horizontally, string step vertically,
refused-not-clamped), and long-distance moves are Phase 9 copy/paste territory. On a grid-native
lane a drag would only be a mouse-operated discrete stepper, while a correct implementation still
owes a live preview honoring §10 margin clamps and collision refusals plus edge auto-scroll once
the drag leaves the viewport — real machinery for a verb with a keyboard equivalent. The
consistency argument (automation points plain-drag-move) was weighed and rejected: automation
lanes are free-time surfaces where drag is the primary verb with no keyboard equivalent; the
chart lane is grid-native where the keyboard verb is primary. "Drag where time is continuous,
keys where time is discrete" is the coherent rule. This retires plan 40's "Phase 4 remainder"
(drag-move + Esc drag-cancel) from the execution queue. **Trigger**: real charting hours show
note repositioning happening often enough that Alt+arrows feels laborious, or users instinctively
drag notes expecting a move (or ask for it). **Remedy**: implement plain drag-move of the
selection (horizontal with grid snap, vertical across strings) with live §10/collision preview,
edge auto-scroll, Esc cancel, single undo entry — per the parked plan 40 Phase 4 remainder spec.

### Min-distance span exemption vs. 40-Q2-B same-string truncation — trigger: span slice 3 builds

Found by the 2026-07-18 grid-native simplification audit: `planAdjustSustain`'s §10 margin
clamp exempts ANY-string span siblings from blocking a growing tail (span members are
implied-held, §5), but `finalizePlan`'s 40-Q2-B normalization then truncates SAME-string
overlaps unconditionally — clawing back for same-string chug siblings exactly what the
exemption granted (cross-string siblings work as intended). Today this only matters for
imported `chart.shapes`. **Trigger**: §5's slice 3 builds member-tail adjustment for real.
**Remedy**: teach one of the two rules about spans — most likely `normalizeSustainOverlaps`
learns the same span-sibling exemption, since §5 says member tails may legally ring past
sibling onsets inside a shared span.

### Marker dissolution seeks the paused transport — trigger: anything follows the paused transport

The two-state marker (settlement §9a, 2026-07-18) implements "the cursor takes the caret's
place" on Ctrl+click / double-click / marquee as a paused transport seek, keeping the passive
invariant (marker time ≡ transport position) with zero position plumbing. Harmless today — the
paused transport drives only the cursor line. **Trigger**: any surface starts following the
paused transport position (plan 44's 3D preview frame, a revival of plan 51's parked
cursor-locked posture display): a marquee would visibly jump that surface. **Remedy**: either
accept the jump as the position honestly moving, or split the dissolution rest from the
transport (give ChartCursor its own stored time) — a contained refactor of the marker's
passive state recorded here so it is a decision, not a surprise (2026-07-18 fold-in audit).

## Editor 3D preview

### JUCE peer-recreation paths are unreachable today — trigger: any path recreates the peer

`PreviewSurface` assumes its native child window outlives the render device (the device holds the
child HWND). The `renderFrame` guard tolerates a lost child once (`m_reported_lost_child`), but
the surface does not rebuild the device if JUCE ever destroys and recreates the top-level peer
(style-flag changes, `removeFromDesktop`/`addToDesktop`, some full-screen transitions). None of
those paths is reachable in the current editor. If one becomes reachable, the surface must detect
peer replacement and bring the render stack back up against the new peer.

### editor/ui tests would need the common::ui link — trigger: a test includes preview headers

`rock_hero_editor_ui_tests` does not link `rock_hero::common::ui` today because no test includes
the preview headers (which pull the renderer). The first test that includes
`preview_surface.h`/`preview_window.h` (or anything transitively pulling the highway renderer)
must add that link, or it will fail to resolve at link time.

### Render child must never hold keyboard focus — trigger: porting the preview off Windows

Invariant learned the hard way on Windows (2026-07-18): clicking the embedded render child
handed it OS keyboard focus, and its message loop silently swallowed every keystroke — space/F3
never reached `PreviewWindow::keyPressed`. The Windows fix bounces `WM_SETFOCUS` back to the
JUCE peer (`previewChildWindowProc` in `preview_surface.cpp`). The preview ships Windows-only
today, so nothing else is affected. **Trigger**: porting the embedded preview to macOS or
Linux. **Remedy**: enforce the same invariant per platform — macOS: a plain `NSView` already
refuses first responder, but verify whatever view the render backend supplies keeps it that
way; Linux: a raw X child window never takes input focus by itself, but an SDL-managed window
would claim it exactly like Windows did, so prefer a focus-inert embedding or replicate the
bounce.

## Cross-platform packaging

### macOS bundle resource layout — trigger: macOS packaging of either product

The exe-relative resource resolution (`currentExecutableFile.getParentDirectory()/resources`)
matches the Windows/Linux deploy layout. macOS app bundles put resources under
`Contents/Resources`, not beside the binary in `Contents/MacOS`. Builds compile cross-platform,
but neither product is *packaged* for macOS yet. When macOS packaging happens, the resource-root
resolver needs a bundle-aware branch.

## Audio / plugin state

### Play with default tones — trigger: a default-tone mechanism exists

21-Q1 settled missing-plugin handling as **refuse-to-start**: the game currently refuses to start a
song whose tones cannot load, listing the missing plugins, with no partial or substitute tones. The
pinned future enhancement (21-Q1 close-out; dovetails with 26-Q5's starter asset) is an opt-in "play
with default tones" path — but it stays inert until there is a standard default tone to fall back to.
**Trigger:** a default-tone mechanism exists (per plan 26-Q5's starter asset), so a chart with
missing or absent plugin tones could still be played with a standard default tone. **Remedy:** expose
the opt-in "play with default tones" path on the 21-Q1 refuse-to-start point, so the player can
choose to proceed with default tones instead of being blocked — a PINNED opt-in on the refusal,
never an automatic substitution. (Referenced from `docs/tracking/backlog.md`'s standard-tones item
and `docs/plans/roadmap/00-roadmap.md`'s 21-Q1 answer.)

### Plugin-state idle churn — trigger: repeating no-intent settle log lines at idle

Suspected but **never observed**: an amp-sim VST3 (Archetype Cory Wong X) re-serializing a
drifting state chunk ~1/sec at idle. The real defect was narrower — an asynchronous
instantiation/restore re-announce settling as a phantom "Edit <plugin>" undo entry — and is fixed
structurally: a settled plugin-state transaction is emitted only when it carried a parameter
gesture (`rock-hero-common/audio/src/tracktion/plugin_dirty_tracking.{h,cpp}`); everything else
folds into the baseline. (Closed out of `docs/plans/in-progress/` on 2026-07-08 with commit
`edb485bd`, later simplified to a gesture-only gate.)

- **True idle churn** would now surface as repeating `Folded plugin state change (no user intent)`
  log lines at idle. It can no longer pollute undo, but each folded settle still runs a full state
  capture (`flushPluginStateToValueTree` → `suspendProcessing` → `getStateInformation`) once per
  750 ms debounce cycle — a performance concern, not correctness. Remedies (verified against
  Tracktion/JUCE source 2026-07-08): skip the capture entirely on a no-intent settle (intent is
  known before capturing, and the stale baseline is harmless — a later real edit's `before`
  restores pre-drift volatile state); and coalesce the plugin-edit-path `updateView()` calls
  through the `IMessageThreadScheduler` port (production impl must use `callAfterDelay(1, ...)`
  semantics, never `callAsync` — PostMessage starves `WM_PAINT` on Windows).
- **Missing undo for in-plugin preset loads** — the accepted cost of the gesture-only gate. A
  gesture-less action inside the plugin folds silently (state still persists with the tone). If a
  user reports "I loaded a preset in the plugin and can't undo it", the retired plan's window-open
  signal (visibility plus a `juce::AudioProcessorListener` `ChangeDetails` heuristic — JUCE
  collapses `restartComponent` to `{programChanged, parameterInfoChanged}`) is the starting point.
- **Automation points that "move by themselves"** (tangential) — Tracktion's `setParameterValue`
  non-automation branch moves a single-point automation curve to follow a plugin-initiated value
  change while the transport is idle (`tracktion_AutomatableParameter.cpp:1439-1441`). Unlikely
  (meter params are not the ones users automate) but adjacent to this subsystem.

## Logging (Windows paths)

### Quill narrows log paths to the active code page — trigger: a log path needs non-ACP characters

RockHero now builds its log-file paths losslessly as UTF-16 (`editorLogFile`/`gameLogFile` go
through `common::core::pathFromJuceFile`), but Quill only accepts a log filename as a `std::string`
sink name (`Frontend::create_or_get_sink(std::string const& sink_name, …)`), so `Logger::init`
narrows it with `.string()` (`rock-hero-common/core/src/shared/logger.cpp`); Quill then reconstructs
an `fs::path` from that string and opens it with `::_fsopen(filename.string().data(), …)` in
`FileSink::open_file` (`quill/sinks/FileSink.h`). Both the API boundary and the open call narrow to
the active code page. A log path is therefore
written correctly only where its characters are ACP-representable; a path with characters outside
the active code page (for example a CJK Windows username on a Latin-1 install) still fails to open.
Only the **log file** is affected — the session workspace and game audio-config paths flow through
`juce::File`/`std::filesystem` wide APIs and are correct for all Unicode. **Trigger:** a real
non-ACP log path is hit (a bug report, or CI on a non-Latin locale), or a decision to guarantee
fully-Unicode log paths. **Remedy:** shadow-patch Quill's `FileSink::open_file` to open via a wide
call (`_wfsopen` / the path's native `wchar_t` representation), or drop in a custom sink that opens
with `_wfopen`/`CreateFileW`; a lightweight stopgap is to redirect logs to an ASCII-safe directory.
Recorded 2026-07-15 alongside the JUCE→`std::filesystem::path` conversion fix at the app roots.

---

## Retired

Items whose trigger fired and were handled. Kept for auditability.

- **bgfx handle ownership at scale** (was: trigger plan 25 Phase 3) — **resolved 2026-07-11.**
  Phase 3/4 multiplied live handles (programs, uniforms, note/inlay/fingering textures, transient
  and retained buffers) and every one is wrapped in `UniqueBgfxHandle`
  (`rock-hero-common/ui/src/highway/bgfx_handle.h`). The destructor-ordering trap is structurally
  avoided: all handles live in `HighwayRenderer::Impl`, a separate object from `RenderDevice`
  (whose destructor calls `bgfx::shutdown()`); consumers declare the device before the renderer so
  the renderer — and its handles — destroy first (`preview_surface.h:109-110`). The project rule
  (never pass `destroyShaders=true`/`destroyTextures=true`) remains in force in `bgfx_program`.
