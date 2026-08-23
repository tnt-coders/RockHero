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

### Note-atlas density vs display resolution — trigger: highway art reads fuzzy at the hit line, especially at 4K

The note atlas stores 64-texel cells while the head quad draws about 123 px wide at 1080p near
the hit line — every note at the moment of judgment renders at ~2x magnification, ~4x at 4K,
and that magnification is the hard ceiling on crispness no in-cell bake can break. Accepted for
now (2026-08-17): at current sizes nothing has read as fuzzy, and the atlas uploads without
mips (`highway_atlas.cpp` `createTexture2D`, no mip chain) precisely because the art is almost
always magnified, never minified.

Remedy, pre-scoped and cheap since the art became analytic: ONE full rebake at 256-texel cells
(4K-true; atlas 1024x1280, ~5 MB) through the recovered-construction pipeline — same geometry,
higher density, provable by the same 1:1 controls — paired with two contained code changes:
generate a mip chain at atlas upload (a true-size atlas gets minified on approach and would
shimmer without one; sampler is already bilinear+clamp) and derive the texel conversion's
`/63` from the measured cell size (`cell - 1`) instead of the hardcoded constant. The
load-time silhouette measurement (`head_art_profile`) adapts by itself. Skip any
1080p-only intermediate density so the question never returns.

`install(DIRECTORY DESTINATION ...)` creates empty `resources/{fonts,sfx,textures}` under
`cmake --install`, but whether the NSIS-packaged artifact preserves empty directories is
unverified. `GameResources::create` only checks the root today (a single `is_directory` probe in
`rock-hero-game/core/src/resources/game_resources.cpp` — cited by name, since a line citation here
went stale within one branch), so nothing breaks either way —
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

### The lane's two chart forms align by index only by construction — trigger: either form published without the other, or the debug assert fires

`EditorViewState::tab` and `EditorViewState::tab_actual` are two whole `ChartViewState`s built by
two `makeChartViewState` calls under one memo key (`editor_controller.cpp`, arrangement id + chart
revision). Since the per-note form pick shipped (2026-08-23) the 2D lane reads
`m_actual->notes[index]` at a PRESENTED note's index on the paint path, so the two note vectors
have to align element for element. They do — `chartResolutions` derives the presented stream from
the saved one, one note per note in the same order — but nothing downstream can say so in a type:
what stands in for it is a debug `assert` on the two sizes in `TabView::setState` plus the prose
contract that the pair is rebuilt and published together. Accepted 2026-08-23: one producer, one
memo key, one consumer, and the assert sits at the only seam a mismatch could enter through.

**Trigger**: anything publishes one form without the other — a second view, a lazily built actual
form, a test double — or the assert ever fires. **Remedy**: the shape the controller's own comment
names, one producer emitting BOTH forms from a single pass over one `chartResolutions` result,
which the projection is already positioned for (the forms differ in `notes` and in nothing else).
It makes the alignment a fact of construction, drops the second projection per chart revision (a
sustain gesture bumps the revision on every wheel notch and projects the chart three times), and
deletes `tab_actual`, the second `shared_ptr` in `TabView`, the assert, and the published-together
contract that currently substitutes for the type.

### ~~Tab and highway scene models stay un-unified~~ — RETIRED 2026-08-21 (W9-B shipped)

**Retired 2026-08-21.** The W9-B fold shipped: one `ChartViewState` (`chart/chart_view_state.h`)
holding `NoteViewState`, `SlideViewState`, `BendPointViewState`, `ShapeViewState`,
`ShapeStringViewState` and `FhpViewState`, produced once by `makeChartViewState`
(`chart/chart_projection.cpp`); the 2D lane renders it directly and `HighwayViewState` composes it
as `chart` beside the board-only structure. The padding-semantics divergence this item named went
with it: the scene carries CHART strings on both surfaces, and each surface maps them onto displayed
lanes per frame through `displayedStringCount` / `displayedLane` (`shared/displayed_strings.h`).
The 2D `linked` field became a READ (`linkedWaypoint`), as D18 ruled. The history below stands as
the record of why.

`common::core::TabViewState` (promoted from editor/core by plan 30 Phase 1, 2026-07-16) and
`HighwayViewState` deliberately keep separate note-view semantics: `HighwayNoteView` bakes
display padding into `note.string` at projection time while the tab core pads at draw time via
`extra_lanes` — merging them is a real refactor across two shipped views with no payoff today
(plan 30 §7, decided 2026-07-11). **Trigger**: a third consumer needs a shared note-view shape,
or a bug traces to the padding-semantics divergence. **Remedy**: design one note-view semantic
(probably draw-time padding), migrate both projections behind their tests, and retire this item.

**The trigger has effectively fired (2026-08-10).** A review found that the separation is the ROOT
CAUSE of a family of defects rather than merely a deferred cleanup: because the two note-view types
are distinct, no shared helper can read a projected note, so every derivation over one is copied for
the other or omitted. Three such were fixed that day (where a note sounds, the visible-note range,
the arrival rule's struck flag) and the fix each time was to hoist the rule out of the surfaces. The
two note views are field-for-field identical, the bend views identical, and the slide views differ by
one field. **Trigger**: fired (2026-08-10), as above. **Remedy**: ruled FOLD 2026-08-13 (W9-B)
and built 2026-08-21 — see the retirement note above.

### ~~A span-held strum's hold ends earlier in 3D than in 2D~~ — RETIRED 2026-08-22 (D1)

**The other half of the comparison is gone.** The note-sustain model's D1 commit moved the 2D lane
onto each note's PRESENTED tail (`NoteViewState::end_seconds`) for drawing, layout, hit testing and
culling, so it draws no hold ribbon at all: the chord box over the strum already states how long the
posture is fretted, and a ribbon under every chug restated it in the one mark that means "this
string is still ringing". With no 2D hold to compare, the board's clamp cannot end earlier than one.
`display_hold_ends` is now the board's field alone — documented as such on `ChartViewState`, which
is where the division is stated once — and `hold_cap_seconds` is board-only presentation by
construction rather than by acceptance, so the remedy's question ("fact about the chart, or
board-only presentation?") is answered by the ruling rather than left open.

The record below is kept for the audit trail; every defect it names was real and is fixed.

(W9-B's fold shipped 2026-08-21 and did not touch this: the hold END is one shared datum, but how
each painter draws up to it is still its own pass.)

The span-implied hold itself is unified: both view states carry `display_hold_ends` resolved from
`chartHolds`, which is what closed the W9-A divergence on 2026-08-11.

**This entry originally understated its own subject and was rewritten on 2026-08-11 after an
independent review.** As first shipped the lane's hold was not merely *longer* than the board's, it
was **unbounded**: the lane drew a ribbon straight to `display_hold_ends[i]` with no cap at all, so a
sustainless chord under a four-beat span with the same string restruck at beats 2 and 3 drew string
1's tail through both later heads and out the far side, tremolo teeth and vibrato sine riding the
whole length — a picture 40-Q2-B guarantees no *stored* sustain can produce. The claim that "no chart
data disagrees" was the wrong test; the right one was that the derived hold had no bound the stored one
has. Paint and hit testing had also come apart: `tabNoteLayout` built its tail rectangle from
`note.end_seconds`, so every span-extended ribbon was drawn and unclickable. Both are fixed —
the hold authority caps the span-implied hold at the next onset on the note's own string (the one
authority both surfaces read; since 2026-08-22 the note's own ACTUAL ring caps it too, which
subsumes that bound in any normalized chart, and `predecessorHoldReaches` reads the stored ring
rather than the hold at all), and the layout manifest takes the same hold end the paint pass draws
to.

**What remained, until D1, was the original entry's subject, correctly scoped**: the *clamp* on top
of the shared hold is board-only. The renderer draws a sustainless span member as a head pinned at
the hit line and ends that pin at `std::min(display_hold_ends[i], group.hold_cap_seconds)`
(`highway_renderer.cpp`, the head-anchor block) — the cap being the next note-showing strum's onset,
because a re-shown chord takes over the pinned display. That strum can land before the string is
restruck, and there the board's pin ended while the lane's ribbon continued. The lane no longer
draws that ribbon, so nothing is left to reconcile.

## 3D highway camera

### Maximally-smooth camera may trail on busy charts — trigger: playtesting shows lag, or a reference-footage comparison diverges

The highway camera motion settled 2026-07-29 (commit `b7c7e650`) on the *maximally smooth* end of
its smoother: a single-knob third-order critically-damped filter (three coincident poles at
`HighwayMetrics::focus_spring_per_second` = 1.3), framing a stepped target quantized to derived
two-measure zones (`camera_zone_starts`). The user chose the slow, languid hover deliberately — it
"gets the 3D camera out of the way and puts the focus on the notation," and reads as the slow
hover the camera research describes. Two things are accepted-for-now rather than settled: (1) at this slow rate the
camera **trails the action slightly on the busiest / fastest, position-dense charts** (it is still
easing toward zone N's framing when zone N+1 already wants it elsewhere) — documented in the
`focus_spring_per_second` doc comment as the accepted cost; and (2) whether maximal smoothness
**actually matches the reference footage** is unconfirmed (the user's eye says yes; no
frame-by-frame comparison was done). **Trigger**: playtesting on a fast, high-travel chart shows
the camera visibly lagging or framing behind the notes, or a direct side-by-side against
reference footage shows the motion clearly diverging. **Remedy**: it is one number first — raise
`focus_spring_per_second` (faster, still jolt-free because the C² onset holds at any rate); if a
crisper *body* is wanted without losing the gentle onset, the asymmetric split-pole form (a faster
onset pole over a slower body double-pole) was built and removed this session and can be restored
from history; or shorten the framing zones via `g_camera_zone_measures` in `highway_projection.cpp`
(so the target refreshes more often). Separately, the reference footage's zoom-out reads "more
vertical" than ours (height-vs-pullback balance, possibly a span-coupled pitch) — a distinct axis
from motion timing, still open.

## Guitar Pro import

### A payload-driven sustain extension can manufacture a deliberate hold — trigger: a real chart at 64th-note density where a slide-bearing note reads as held across the next onset

The hold exemption (rule 1, now `presentedChartNotes` in `chart_presentation.h`) asks whether the
stored ring runs past the next onset. The importer's `resolveSlideIns` and slide-out synthesis can
**extend** a stored ring, floored at `g_minimum_slide_window`. If that extension is what carries the
ring past the next onset, importer-fabricated geometry witnesses a "deliberate hold" the source
never notated.

Accepted for now because it needs sub-1/8-beat spacing (64ths) to reach, and no corpus case has been
seen. The fix is not local: it means threading the source's pre-resolution notated ring through to
the hold check, which touches the slide, arpeggio-arrival and FHP paths that all read that ring.
Found 2026-08-06 while narrowing the trim's payload floor; deliberately left alone in that change.
Re-aimed 2026-08-22 when the tail rules moved to the read side (stage A2): the item survives the
move intact, and the grace-lead protection it used to cite is gone — binding is now decided on the
sounding position, which the note-sustain-model plan lists as an accepted deviation.

Two smaller relatives of the same family, also left: `hasSustainTechnique` tests
`!note.slides.empty()`, so a trailing equal-fret hold still exempts a note from the *drop* rule when
the trim does not fire; and below-margin crowding still leaves a chord's bent string with a stub its
unbent partner does not get, which is the minimum-distance rule behaving as specified rather than a
defect.

### Any score using repeats or jump directions is refused outright — trigger: a user reports a real song that will not import

A deliberate design limit, recorded here on 2026-08-11 after a corpus re-import measured it: the score
parser refuses a file whose bars carry repeat bars or jump directions (`gp_score_parser.cpp`, "score
uses repeats or jump directions") rather than expanding the playback order into a flat timeline.
Refusing is the right call while that expansion is unbuilt — a chart that silently dropped a repeated
section would be worse than one that does not import — but it is not a niche shape: it costs 1 of 115
files in the local corpus, and repeat notation is ordinary in published tab. **Trigger**: a user
reports a song that will not import for this reason, or the corpus rate rises above a file or two.
**Remedy**: expand the playback order at parse time, resolving repeats and jumps into the linear bar
sequence the chart model already assumes — never by teaching the chart model about repeats, which would
put a second timeline concept into every consumer.

## Chart editing (tab lane)

### Bracket clipping gaps a scrape's travel diagonals — trigger: seen in real material

Technique marks riding a tail — slide diagonals, bend curves, the vibrato sine — clip against
every arpeggio bracket's columns on their string (2026-08-14, the posture-display work), so a
posture mark is never cut through by a diagonal. The accepted edge: a pick scrape, or a slid
right-hand tap, whose path crosses a *later* span's bracket has its travel diagonals gapped there
— arguably the note's own body rather than a technique on it. Judged EXTREMELY rare with the user
and deliberately left undecided. **Trigger**: the gap shows up in real imported or authored
material and reads wrong. **Remedy**: decide then whether a scrape's diagonals are body (exempt
them from the clip, as the ribbon and tremolo teeth already are) or technique (keep the gap), and
record the ruling in the walkthrough.

### Gesture-deferred pushes have no lifecycle reset — trigger: a report of a frozen tone row

`ToneTrackView` and `ToneAutomationLanesView` defer state pushes while a view-owned drag holds
indices into the frozen state, adopting the pending push on `finishGesture`. Every reachable path
clears the gesture (mouse-up, Escape), but a LOST mouse-up — the component hidden mid-press, mouse
capture stolen — would leave `gestureActive()` true and every later push deferred forever: the row
renders permanently stale. The old design self-healed (every push reset the drag) at the cost of
the mid-drag correctness the deferral was built for, and this failure mode has never been
observed, so it is accepted as-is. **Trigger**: any report of a tone row or automation lane that
stopped updating until clicked. **Remedy**: clear the gesture (adopting the pending push) in
`visibilityChanged()`/`parentHierarchyChanged()` on both deferring views.

### Sustain tail-drag resize is deliberately not implemented — trigger: charters reach for the tail

Decided with the user 2026-07-16 (interaction-model amendment record): resizing a note's sustain
is covered precisely by Alt+wheel and Shift+Alt+Left/Right, and a draggable tail end is a small
target (the tail strip is ~13–19px tall, its end zone a few pixels) that would compete for grab
space with drag-move on the same note and get fiddly at low zoom. Direct manipulation loses to
the wheel here — probably. Re-affirmed 2026-07-20 by the keymap surface-parity triage
(docs/plans/in-progress/keymap-matrix.md, gap 1): edge-drag stays dropped as redundant with
Alt+wheel, and chart **drag-move** is now scheduled (docs/plans/todo/tab-pointer-drag-editing.md),
which will claim the same note grab space — raising the bar for ever adding a tail-drag beside it.
**Trigger**: watching real charting shows users grabbing sustain
tails expecting a resize and failing (or asking for it). **Remedy**: implement tail-end drag as
the standard edge-resize verb with a generous grab zone (hit-test via the shared layout
manifest's tail rectangle), live preview, Esc cancel, single undo entry.

### ~~Min-distance span exemption vs. 40-Q2-B same-string truncation~~ — RETIRED 2026-08-22

Found by the 2026-07-18 grid-native simplification audit: `planAdjustSustain`'s margin clamp
exempted ANY-string span siblings from blocking a growing tail, but `finalizePlan`'s 40-Q2-B
normalization then truncated SAME-string overlaps unconditionally — clawing back for same-string
chug siblings exactly what the exemption granted. **Both halves of the contradiction are gone**:
the note-sustain model's stage A4 deleted the margin clamp and the span exemption with it, so the
growth verb now grows to exact adjacency with the next onset on the note's OWN string —
`sustainBoundOf`, the same answer `normalizeSustainOverlaps` truncates to. One rule, one authority,
nothing to reconcile. (§5's member-tail question, if it ever builds, now asks whether a SPAN may
outlive a member's ring, which is a hold question rather than a truncation one.)

## Highway note art

### Directional lighting is BAKED into the mark art, and the renderer rotates the art — trigger: real highway lighting, or a flipped mark reading wrong

The technique marks and head bases carry a top-lit rim gradient painted into their pixels —
bright on top-facing edges, dark on bottom-facing, measured at roughly 100 counts on the palm
mute and 58 on the pinch harmonic. That is a statement about where the light is, frozen into art
the renderer then turns.

**Three transforms invalidate it, and the third is the one that matters.** The legato cell is
v-flipped to draw a pull-off, so its gradient renders upside down. The bend chevron takes a
180-degree rotation on bend-inverted lanes, with the same result. And `highway_renderer.cpp`
rolls every single non-chord, non-node note ninety degrees across its WHOLE approach
(`rotation = (pi / 2) * flip_remaining`), with technique markers inheriting the same
`cos_r`/`sin_r` — so a baked top-lit gradient points SIDEWAYS for most of a note's visible life
and only points up in the last quarter-second before the hit line. This is not an edge case at a
flip; it is continuously wrong for every ordinary note on screen.

**The right fix is architectural, and the user named it (2026-08-18):** bake the art with NO
lighting at all, and light the highway for real — one light direction in world space, shading
computed where the orientation is actually known. Baking a light direction into art that gets
rotated is the category error.

**Trigger**: real highway lighting is taken up, OR a pull-off, an inverted-lane bend chevron, or
a mid-approach mark reads visibly wrong-lit in practice. **Remedy**: strip the directional term
from every cell (an analytic re-bake through the recovered constructions, cheap now that they
are recorded) and add the light to the renderer. **Why it is not done now**: the gradient is
doing real work as a bevel, so flattening it today trades a known-good look for a correctness
win nobody can see until the light exists — the user ruled to take the free half instead (the
2026-08-18 left-right symmetry pass, which removes an authoring artifact without touching the
vertical axis) and file this. Recorded so the next person to notice a wrong-lit pull-off finds
the analysis rather than re-deriving it.

### Accent strength: the gain lever is SPENT — trigger: accents still read thin in play

The entry this replaces watched a reservation, and the reservation came true. The 2026-08-18
signing took `medium flat` at gain 1.0 with the user's *"it is a bit subtle but looks good"*.
Sighted 2026-08-20 against a four-rung toggle, the user took two rungs up for notes — *"I think I
like 2 steps up from what is currently shipped for note heads and tails"* — and left chord boxes
alone: *"chord boxes are already plenty accented as shipped"*.

**Shipped**: `g_accent_gain` 1.5 on a note (`highway_emphasis_styles.h`, mirroring `g_ghost_alpha`
0.5), `g_accent_gain_boxes` 1.0 on a chord box (`highway_renderer.cpp`). The sighting toggle is
deleted; nothing of it remains.

**Why this stays open at all.** The old entry named the gain as THE remedy, and the gain has now
been spent, so a future attempt must not simply reach for it again. The untried lever is REACH. A
note's halo is only about one to four pixels wide on approach (scaled from the measured 0.075 world
= 0.7-2.3 px figure for the highway frame, not measured directly at 0.12), so gain brightens a thin
band where reach would widen it. Past roughly gain 2.0 the extra radiance mostly grows the
white-hot core rather than adding width, so raising it further buys progressively less.

**Trigger**: accents still read as decoration rather than emphasis in real play. **Remedy**: sight
`g_accent_reach` (inline beside `g_accent_exponent` in `highway_renderer.cpp`), not the gain. Note
that the note/box radiance split would need re-sighting alongside it, since widening the halo
changes the lit-length imbalance the split exists to compensate for.

### Stacked node heads interpenetrate — trigger: simultaneous harmonics on adjacent strings read wrong

The edge-height diamond (D) was signed 2026-08-15 with its adjacent-string overlap accepted on
sight, not measured against the corpus. **Trigger**: stacked node heads read wrong in real
charts. **Remedy path**: first a corpus count of how often simultaneous harmonics land on
adjacent strings — evidence before any resize, since the diamond's proud tips are what make the
mark read (see the 2026-08-17 span signing in
`docs/plans/in-progress/highway-note-art-state.md`).

### The harmonic diamond's span costs await an in-app evaluation — trigger: the user's asked-for evaluation, or either cost reads wrong

SIGNED 2026-08-17 at today's span, deliberately past the pitch standard, with two measured
costs accepted pending evaluation: stacked diamonds on adjacent strings interpenetrate ~4.2
texels per side, and on the outer strings the tip extends ~4.2 texels (0.6–1.9 screen px) past
the string grid's edge line. The user asked to evaluate both in the app. **Trigger**: that
evaluation happens, or either cost reads wrong in practice. **Remedy**: sight in the app first;
the tips standing proud of the ring are the mark's legibility, so any change is a re-signing,
not a tune.

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

## Song packages (untrusted input)

### No decompression-size cap on package extraction — trigger: the app first fetches or accepts a package from a network source

`extractZipToWorkspace` (`rock-hero-common/core/src/package/rock_song_package_read.cpp`) caps neither
the total extracted size nor the expansion ratio, so a small `.rock` crafted to expand enormously
could fill the disk. **Accepted for now on the user's ruling (2026-08-10), and the reasoning is worth
keeping**: today every package is a file the user chose and imported by hand, so a cap would guard a
threat that does not exist, and inventing a ceiling now is exactly the speculative complexity this
project asks to be questioned. Nothing in the app downloads, receives, or auto-imports a package.

The trigger is the moment that stops being true — a song-sharing feature, an in-app download, a
watched folder that imports what lands in it, or accepting a package over any network transport.

Remedy: cap total extracted bytes and the expansion ratio in the extractor, which is the trust
boundary and the only place with the information. Sizing logic, so it need not be re-derived: a real
package is one FLAC per arrangement — tens of MB, a few hundred at the outside — so a 2 GB
total-extracted ceiling plus roughly a 200:1 ratio ceiling sits orders of magnitude above anything
legitimate and would never fire on real content. Refuse with a typed error naming the limit rather
than truncating, since a truncated extraction would then fail confusingly downstream.

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

## Note detection / gameplay feedback

### No early rough-pitch check on provisional hits — trigger: low-register confirm feels laggy

Per-gem feedback is two-staged by design: the onset event (p95 ≤ 15 ms, plan 22 budget) pins the
head and fires the provisional treatment, and pitch confirmation upgrades it (plan 25:
provisional → subdued spark, confirmed → full burst). Confirmation is physics-bound to ~2–3
fundamental periods, so the upgrade lags the spark by ~1 frame on mid/high notes but 50–115 ms
(3–7 frames at 60 fps) in the low registers (E2 → B0 rows of plan 22's signed latency budget).
Deliberately not mitigated at v1 — the stagger is likely imperceptible, and the plans are
measurement-first. **Trigger**: plan 23 latency measurements plus plan 25 Phase 5 playtesting
show the provisional→confirmed visual upgrade feels laggy on low-register notes. **Remedy**: an
early hypothesis-verification check (recorded 2026-07-26): query the already-selected whitened-STFT
+ Klapuri harmonic-summation salience path at the first hop or two after the onset with the
*expected* chart pitch as the single candidate — verification of a known hypothesis, not blind
estimation, so it costs microseconds on the existing FFT front-end — and emit the result as
low-confidence early pitch evidence into plan 24's Armed→Provisional hook ("disambiguated by
early pitch evidence when present"), which already consumes exactly that. Physics caveat baked
into the design: with ~15 ms of post-onset signal a low-register check discriminates only the
pitch *neighborhood* (gross wrong-note, wrong-octave-region), never adjacent frets — it may
accelerate the upgrade, never replace confirmation.

## Input bindings

### Editor and game binding systems stay parallel — trigger: the editor wants non-keyboard input

Decided 2026-07-20 (46-Q2 / 26-Q4, one answer): the editor's command registry builds on JUCE's
`ApplicationCommandManager`/`KeyPressMappingSet` while the game keeps its headless `MenuBindings`
resolver (`rock-hero-game/core/.../input/menu_bindings.h`); only the *conventions* are shared
(stable append-only action ids, overwrite-and-clear conflicts, diff-vs-defaults persistence). A
shared common concept was analyzed and rejected as a one-consumer-per-feature abstraction: JUCE
cannot bind gamepad/MIDI triggers, the game wants no modifier chords, and no action vocabulary
overlaps. **Trigger**: the editor gains a real need for non-keyboard input — the likely case is
MIDI-pedal transport control while charting with a guitar in hand. **Remedy**: extract
`MenuBindings` (small, headless, tested) to rock-hero-common as its own phase with tests, shaped
by the then-real second consumer, per the extraction rule in
docs/design/architectural-principles.md; the editor side feeds resolved actions into its command
manager rather than replacing it.

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

### ~~A muted-tail trim would flatten legato claims corpus-wide~~ — RETIRED 2026-08-20

**Trigger FIRED: W4/E25 shipped, with the ruled remedy.** Recorded by the legato ruling of
2026-08-11: a fully muted note's tail counted at its stored length for the connection hold test, so
the muted-tail trim would break every dependent claim corpus-wide, silently, on open. The remedy as
first recorded — run the trim as an editor plan operation so the flatten rode its undo entry — was
superseded by the load policy ruled 2026-08-20 (load normalizes and never refuses, so the trim had
to live in the normalizer), and the knock-on is handled the way that policy prescribes:
`normalizeChart` runs the settle sweep as its LAST stage over the trimmed stream, every flattened
claim is reported beside every trimmed tail in the one-shot open notice with its position, the
session opens dirty, and the file is untouched until the user saves. Nothing is silent, which was
this item's real complaint; "reversible" is answered by the untouched file rather than by undo.
Pinned by the normalizer's own test ("the whole chart normalizes in one call, with the settle
sweep last"). Design: `docs/plans/in-progress/e25-muted-tail-implementation.md` §6.5 and §6.7.

**Reversed at the root on 2026-08-22 (note-sustain model, stage A3):** E25 is a PRESENTATION rule
now, so nothing trims a stored ring at all — a dead note carries the duration of its damped stroke,
which is exactly the datum this item was worried about losing, and only the drawn tail goes. The
knock-on the entry describes therefore cannot arise: measured over the 113-song local corpus, the
whole move (E25 out of the stored form plus the strict-adjacency hold test) changed ZERO resolved
legato motions.

### ~~Defaulted `operator==` over floating-point scene fields~~ — RETIRED 2026-08-10

Ten scene view types were converted — three in the then `tab_view_state.h`, seven in
`highway_view_state.h` (the shared element types now live in `chart_view_state.h`) — using the
exact-float idiom `HighwayHandWindow` and the tap-light types
already used (two more in `tab_view_state.h` were hand-written before this pass). The first
retirement here overclaimed "nothing left to watch": a same-day verification sweep then found
eight more instances of the shape elsewhere in the tree, and a second pass later that day resolved
all eight — five comparisons deleted where nothing compares them (`HighwayCameraTarget`,
`HighwayCameraPose`, `ToneGainPoint`, `AudioDeviceStatus`, `EditorViewState`), three hand-written
where a gate is the natural next use (`InputCalibrationPrompt`, `InputCalibrationViewState`,
`SongSectionViewState`), and one routed through a value type (`SignalChainViewState`'s gain). A
whole-tree scan for the shape now returns nothing, so the retirement finally stands on a
mechanism rather than a claim. The remaining defaults are correct and deliberate: `ChartViewState`,
`HighwayViewState`, `HighwayDisplayOptions` and `ShapeStringViewState` carry no floating member OF
THEIR OWN, and hand-writing the first two would only create a member list to keep in sync by hand.

One fact this settled, now recorded in `CLAUDE.md`'s blind-spot list: only a struct's own float
member is diagnosed. A float compare reached through `std::optional<double>` or `std::vector<double>`
is instantiated inside a standard-library header and is NOT — which is why `ChartNote`'s defaulted
comparison over `std::optional<double> harmonic_node` has been odr-used and green on CI all along.


Items whose trigger fired and were handled. Kept for auditability.

- **GameShell is a composition point in game/ui** (was: trigger "plan 21 started 2026-07-11") —
  **RESOLVED 2026-07-12.** Plan 49 (`docs/plans/roadmap/49-game-app-architecture-symmetry.md`)
  deleted the hollow `GameShell` and split it into `RockHeroGame` (composition root owning
  window/device/resources), `Game` (content object), and an `SDL3Application` loop base;
  composition and dependency injection now live in `main()` (`rock-hero-game/app/main.cpp`),
  exactly the decided move-to-app remedy. `GameShell` no longer exists in code.

- **Marker dissolution seeks the paused transport** (was: trigger "anything follows the paused
  transport") — **trigger FIRED, resolved 2026-07-27.** Plan 44's 3D preview is now a real
  follower of paused time (`preview_surface.cpp` renderFrame → PreviewTimeModel), so the trigger
  condition holds. Decided the **accept** branch: the preview shows the armed caret first and
  falls back to the transport position only while passive, and marker dissolution is an in-place
  seek — `dissolveChartCaretInPlace` seeks the transport to the caret's own time — so the paused
  target moves continuously and the preview's exponential glide absorbs any residual step. No
  visible jump; no ChartCursor split needed. If plan 51's parked cursor-locked posture display is
  ever revived without caret-following, re-open the question then.

- **bgfx handle ownership at scale** (was: trigger plan 25 Phase 3) — **resolved 2026-07-11.**
  Phase 3/4 multiplied live handles (programs, uniforms, note/inlay/fingering textures, transient
  and retained buffers) and every one is wrapped in `UniqueBgfxHandle`
  (`rock-hero-common/ui/src/highway/bgfx_handle.h`). The destructor-ordering trap is structurally
  avoided: all handles live in `HighwayRenderer::Impl`, a separate object from `RenderDevice`
  (whose destructor calls `bgfx::shutdown()`); consumers declare the device before the renderer so
  the renderer — and its handles — destroy first (`preview_surface.h:109-110`). The project rule
  (never pass `destroyShaders=true`/`destroyTextures=true`) remains in force in `bgfx_program`.

- **Light tap plate glare (accepted 2026-08-12).** The fretting hand's white plate throws ~4.8x
  the dark plate's luminous mass and a ~9 L* stronger halo against the near-black lane; no rim
  value can close it (measured across nine candidates). Accepted as shipped — the rare,
  information-bearing mark drawing the eye is arguably correct. **Trigger:** the light plate
  reads heavy/glary in real charting use. **The measured remedy, priced:** fill #F2F2F2 with the
  shared rim re-solved to #777777 cuts the halo by 2.4 L* and the mass ratio to 4.5x, at the
  price of splitting the fretting hand's whites — the legato triangles are pure white in the
  same slot, so the clean form moves both through one shared constant.

- **A separator between the note head and its tail (raised 2026-08-19).** The 2D lane's dark outer
  backing was dropped from both the head and the tail after five variants were sighted; the plain
  head with a trimmed glow won, and the user's stated reason for the runner-up is the thing to
  watch. The backing is the lane's own ground colour, so it never shows against bare lane; its one
  real job was where the head OVERLAPS its own tail, punching a ring that separated the two and
  made the head pop. Heads paint in a pass after all tails, so that seam exists on every sustained
  note. **Trigger:** the user says the head and tail need a slight separator, or heads start
  reading as merged into their sustains on dense charts. **The remedy, already measured and
  costed:** restore the backing on the HEAD only, leaving the tail plain — the runner-up variant,
  a close second on sighting. That is `fillHeadShape`'s layer stack gaining `layer(0.0f, backing)`
  back with the ring and fill inset as they already are, and nothing on the tail side; the head's
  visible ring does not move, since the backing occupies the outermost `border` that the head's
  box already reserves and leaves empty. Note the second-order effect if it is ever restored: the
  accent glow's radius is currently derived from the ring as the visible edge (`accentGlowSize`),
  and a restored backing would hide the inner `border` of it again, so the glow would need to grow
  back by that much or it will read tight.

- **The selection ring is hard to read on the blue string (accepted 2026-08-20).** Worst on blue,
  worse again when the note is accented. Two rounds of constructions were built, sighted and
  REJECTED — the shipped ring reads better than every alternative — so this is accepted as shipped
  rather than solved, and the reasons are worth keeping because they bound what a future attempt
  may try.

  **Root cause, measured.** `EditorTheme::accent` is `0x87cefa`; the blue string's bright head ring
  is `0x29caff`. That pair measures **6.89 dE00**, against **>= 42.55** for every other string, so
  blue is 6.2x closer to the ring than the next-worst. It is a light blue line drawn on light blue.
  The accent then makes it worse structurally: `huePreservingAccentColor` caps its gain at 1.0 on
  all six strings, so **the accent glow's ink IS the head-ring ink**, and the ring sits entirely
  inside the glow (stroke r 11.70-14.30; glow full-alpha to 13.17, zero at 15.64). On a plain note
  the ring's outer neighbour is the lane at 69.68 — its one good side — and the accent replaces
  that with more of the ink it already cannot beat. Blue measures **35.00 -> 5.76 dL\***, a
  **-83.5%** collapse.

  **What is already ruled out, with numbers, so nobody repeats it.** (1) A different single ink
  cannot work: the neighbour inks' L\* values are packed tightly enough that **no ink anywhere in
  sRGB can exceed a 6.30 dL\* floor** against all of them, where relocating the ring gets 72.98 —
  an 11x difference, proved structurally rather than by search. (2) Anything drawn INSIDE the
  glow's annulus that is strong enough to beat the glow also covers it: every clearer construction
  measured dropped accent retention to 7-11% where the shipped ring retains 20%. The user's words:
  *"all the ones that look CLEARER effectively KILL the accent ring."* (3) A standoff ring outside
  the glow measures far better on both objectives (blue accented 5.76 -> 42.45) and was still
  rejected on sight, as was a version fitted to the note's own silhouette.

  **Trigger:** the user says the ring is a real obstacle in charting use, rather than a noted
  annoyance. **Where a future attempt should start:** not the ring. The root cause is upstream —
  the editor's selection accent and the string palette collide, and the glow's gain cap makes the
  collision total on an accented note. Changing `EditorTheme::accent` away from the blue family, or
  giving the glow an ink of its own, are the levers that have not been tried. Both change the
  shipped look and need sign-off.


- **A dead note that is also a legato connection draws two marks over each other (accepted
  2026-08-20).** Sighted after the draw order was folded into one authority: the X on top reads
  *better* than the connection cell on top, but the pair still visibly conflicts. Accepted as-is,
  because both techniques must remain chartable together and the combination is rare enough that a
  slightly ugly rendering beats refusing to show one of them.

  **Why it is not an ordering bug.** Every technique mark on a 3D head is pushed at the SAME centre
  and the SAME family size — `push_marker(x, head_y, z, ...)` takes identical coordinates for all
  of them — so any two marks a head wears overlap by construction. Draw order cannot separate them;
  it only chooses which one survives intact. `highwayHeadMarks` makes that choice once, and the
  choice is right (the X is the mark whose meaning breaks when it is cut). The residue is a
  *sizing and placement* question about the marks themselves, not an order question.

  **Trigger, and it is SEQUENCED (user 2026-08-20).** This class of conflict is hard to avoid and may
  well need a real solution, but it is queued BEHIND head-atlas mipmapping
  (`docs/plans/roadmap/56-head-atlas-mipmapping.md`) rather than picked up on its own. The marks are
  sampled today with no mip chain, so how two overlapping marks actually read cannot be judged
  fairly until they read correctly at distance — any fix chosen now would be tuned against a
  sampling artifact.

  Plan 56 already carries the same constraint from its own side, which is why the two belong in one
  pass rather than in sequence with a gap: it records that mipmapping and any distance-legibility
  work on the marks "should be judged together", because mips alone make every annotated note
  relatively LESS conspicuous at the horizon (at the far edge an ordinary note gains 72.7% from
  correct filtering against a mark's 21.8%). Fixing this conflict before that lands would mean
  re-judging it immediately afterwards.

  Until then, only one thing should pull this forward early: a charted song making the pair common
  enough that it is misread in real use.

  **Where a future attempt should start.** Not the draw order. The levers are the marks: giving the
  connection cell a satellite slot the way the 2D lane does (the lane has no conflict precisely
  because its satellite sits beside the head rather than on it), or shrinking one mark so the pair
  nests instead of crossing. Both are art changes and need a texture pass plus sign-off.

  **Related, and larger:** the whole marker order is due a re-evaluation against the note
  compatibility matrix (`docs/plans/in-progress/technique-compatibility-and-hardening.md`) rather
  than against the pairs that happened to be sighted. The order shipped today is derived from how
  much of the note's identity each mark overrides, which is a sound rule, but it has never been
  checked pair-by-pair against every combination the matrix says is legal. That is a task in its
  own right, not a watch item.
- **The 3D accent reads bolder on a tremolo tail than on a straight one (accepted 2026-08-20).**
  The user, sighting the highway: *"In 3D the accent DEFINITELY looks like it stands out way more on
  tremolo than on straight tails ... It also looks like the glow spikes a bit brighter at each
  exterior angle and dips out a bit at each interior angle on the tremolo tail."* Two rounds of
  alternative constructions were built and sighted, and ALL were rejected — *"shipped is the only
  one that looks consistent across all tail types"* — so this is accepted as shipped rather than
  solved. The 2D half of the same complaint WAS fixed (the 2D halo now traces the band's centreline,
  commit 5ef89e26); only the 3D half remains.

  **Root cause.** The tail's glow is emitted per segment along the path, and the operator is
  additive (signed 2026-08-18). Where the path turns outward, consecutive segments' halos overlap
  and their contributions ADD, so the halo beads bright; where it turns inward they leave a gap and
  it thins. A straight tail never turns, so it never shows either. A tremolo path turns at every
  tooth, which is why the same light reads bolder there. It is a property of the operator meeting a
  turning path, not of the tremolo art.

  **What was ruled out, so it is not rebuilt.** Every rejected candidate partitioned the path into
  per-segment pieces and then tried to fix the joints — offsetting them to overlap makes the beading
  worse under an adding operator, and abutting them exactly seams dark instead, because two
  antialiased edges meeting do not sum to one opaque edge. The user recognised this on sight:
  *"Did we not try that already? Isn't that the method that left breaks at each joint?"* The
  candidates also read as glitchy while PAUSED and in the still sheets, which killed any explanation
  that depended on motion. Shipped survives because it never partitions the path at all, so its
  error is constant with the tooth density instead of scaling with the number of joints.

  **The one identified fix, not taken.** A MAX blend equation (`BGFX_STATE_BLEND_EQUATION_MAX`)
  removes the class outright rather than tuning it, because taking a maximum makes overlap
  idempotent — two halos covering the same fragment give the brighter one, not their sum, so a turn
  cannot bead. The cost is why it was not taken on the spot: it overturns the additive operator
  signed 2026-08-18, and the note heads, open-string bars and chord boxes share the accent batch and
  rely on accumulation, so they would need their own draw call first.

  **Trigger:** an accented tremolo reads wrong in real play, OR the accent batch is being
  restructured for another reason — the tail-glow vertex budget is in that same batch and would
  be the natural moment, since splitting the batch is most of the work MAX blending needs anyway.
  (The silent half of that bug closed 2026-08-21: the tail sampler now holds its cap as ONE
  budget, so a long teethed open tail can no longer reach twice the cap, and the oversized-batch
  drop reports under its own flag instead of sharing one with pool exhaustion. The batch split
  itself is still open.) **Where to start:** the blend equation, not the geometry. Every geometric approach has
  been tried and sighted.
