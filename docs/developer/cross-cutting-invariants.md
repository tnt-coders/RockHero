\page guide_invariants Cross-Cutting Invariants

*Applies to: Repo-wide unless marked Editor-only.*

Each entry is a "when you do X, you must Y" rule whose violation compiles fine, often works in
light testing, and fails in production. The rules themselves are owned by
\ref design_architectural_principles; this page is the reminder list.

**Deferred callbacks need liveness guards.** Any callback that can fire after its owner is
destroyed must check a guard first: `juce::Component::SafePointer` for JUCE components,
`std::weak_ptr` tokens for project-owned non-component objects (the editor controller wraps this
as `safeCallback`; the engine uses a `shared_ptr<bool>` alive flag). These guards prevent
use-after-free only — they are not thread synchronization. See "Keep Threading at the Boundary"
in \ref design_architectural_principles.

**Busy completions must validate their token.** *(Editor-only.)* Any completion for work started
under a busy operation checks `isCurrentToken` against the token captured at submission and
returns silently if stale — a superseding operation may have made the work meaningless.
`runWorkerThreadBusyOperation` composes the liveness guard and the token check; prefer it over
hand-rolling. The overlay the user sees is `editor/ui/src/busy/busy_overlay.cpp`, rendering
`BusyViewState`. See "Async Choreography" in \ref design_architectural_principles.

**Pick one of the three async idioms.** *(Editor-only.)* Worker offload with tokened completion;
paint-fenced message-thread work (`runAfterBusyPresentationReady` — posted messages can starve
paints on Windows, so blocking work waits for the overlay to actually paint); or a yielding
message-thread continuation chain. A fourth idiom needs a constraint none of these satisfies —
and a design conversation. See "Async Choreography" in \ref design_architectural_principles.

**Audio ports are message-thread-only unless documented otherwise.** The exceptions are declared
per-method in the port's Doxygen (the plugin-scan methods in `engine.h` are the exemplar). Never
call an undocumented port method from a worker thread because it "seemed fine".

**Undo capture is two-phase.** *(Editor-only.)* Capture the before-state *before* mutating;
commit the edit only after side effects succeed, abort otherwise. One user gesture produces
exactly one undo entry. Edits that recreate plugins must report it via
`IEdit::instantiatesPlugin` so replay routes through the busy fence.

**Copy dispatched state by value before a re-entrant dispatch.** *(Editor-only.)* A controller
intent that reads the live selection or marker variant and then runs an action dispatch must
copy the alternative it read **by value** first — the dispatch can replace the very variant the
reference borrowed from, leaving it dangling mid-call (`onSelectionMoveRequested` and its
siblings in `editor_controller.cpp` are the exemplars). Generally: never pass a reference into
variant-held state across a call that may reassign the variant.

**A render memo must key every input.** *(Editor-only.)* Vblank-cadence derivations that
early-out on an unchanged key (`RulerCursorKey` in `ui/src/timeline/track_viewport.h` is the
exemplar) must include *every* input of the derived value as a key field. A missing field
freezes the output, and the symptom is a lingering — not one-frame — paint glitch. Corollary:
a sibling component's geometry must be *pushed* into the memo's key (the caret-mask channel in
\ref guide_2d_views), never polled from inside the gated derivation.

**Recoverable failures cross boundaries as typed errors.** Domain-owned error values, not
framework strings; callers branch on the type, never parse message text. Side-effect failures
that can affect user-visible state must surface — silently folding them into refreshed state is a
defect. See "Typed Boundary Errors" in \ref design_architectural_principles.

**Time is a dependency.** Core logic never reads wall clocks, timers, or render cadence; it
receives transport positions, frame deltas, and sample rates as inputs. If a test cannot control
the time your code sees, the boundary is misplaced. See "Time Must Be a Dependency" in
\ref design_architectural_principles.

**Log through the project facade.** Logging goes through the project-owned logger
(`common/core/shared/logger.h`, a Quill-backed facade): call sites use
`RH_LOG_*("category", "message {}", value)`; producers on any thread — including the audio
thread, via the realtime handle — enqueue without blocking, and one worker thread formats and
performs file IO. Never use Quill's own macros directly, and never do blocking IO from realtime
code.

**Naming and documentation are enforced late, not never.** clang-tidy (naming, treated as errors)
runs on demand, not in pre-commit — write to the convention table in `CLAUDE.md` so the eventual
run stays clean. Public headers carry Doxygen per \ref design_documentation_conventions, wrapped
manually to 100 columns (clang-format does not rewrap comments).
