# Plan 52 — Time-Range Edit Operations (range copy/cut/paste over the loop selection)

## 1. Status

**OPEN DISCUSSION — decision-gated (G52-RANGE-EDIT). Nothing in this plan is executable yet.**
Authored 2026-07-16 at the user's request. Every open question below (52-Q1..Q8) carries a
recommendation, but **each individual decision requires explicit user sign-off** — the
recommendations are discussion inputs, not defaults that activate by silence. The gate closes
only when all eight questions are answered; partial answers unblock nothing.

## 2. Goal

The plan 47 time selection doubles as an **edit range**. With a time span selected, the charter
can copy **all chart content within the span** — notes, hand shapes / chord boxes / arpeggio
spans, their chord templates, and fret-hand positions — and paste it elsewhere, with paste
positioned at the cursor and (per the user's initial direction, subject to Q3) **overwriting
everything in the destination window**. Cut and delete-range follow from the same machinery.
The user's motivating scenario: duplicating a passage should carry the full posture layer, not
just the sounding notes.

## 3. Non-goals

- No second selection concept. The time selection IS plan 47's selection; this plan only gives
  it a second consumer (edit operations alongside looping). No mirrored range state anywhere.
- No second clipboard. docs/plans/roadmap/40-chart-editing.md Phase 9 already plans a
  note-selection clipboard (position-relative JSON fragment in the document grammar); this plan
  extends that one codec with range semantics. Whichever executes first lands the codec; the
  later plan extends — never two clipboard formats (same coordination shape as the plan 21/47
  loop-port rule).
- No ripple/insert paste in v1 (shifting all later content to make room). It interacts with the
  tempo map, sections, and tone changes, and its blast radius is a different order of magnitude
  — recorded as a possible future mode under Q3, never implied by this plan.
- No tempo-map or time-signature content inside the clipboard payload (Q6 scope). Copying a
  span never copies its tempo anchors or signature changes; docs/plans/roadmap/41-tempo-map-authoring.md
  owns those edits.
- No cross-product behavior. The game never edits; everything here is editor workflow.

## 4. Constraints

Applicable subset of the roadmap constraint block (docs/plans/roadmap/00-roadmap.md):

- (a) **Layering**: pure chart transforms may live in common/core (the plan 40 Phase 1
  grid-arithmetic precedent); clipboard policy, intents, and keys are editor-core/editor-ui.
- (c) **NAMING FIREWALL**: the commercial real-guitar game is never named; use "RS" or neutral
  phrasing.
- (f) **Undo**: RockHero-owned full-state mementos. Every range operation (paste-overwrite,
  cut, delete-range) is exactly one undo entry; a paste that deletes and inserts must never
  be two.
- (h) **Builds**: all verification through `.agents/rockhero-build.ps1`.

Binding design-doc rules: views present state and emit intents; chart mutation flows through
editor-core handlers into `Session::currentChart()` with revision-keyed projection invalidation
(the plan 40 Phase 2 pipeline).

## 5. Current state inventory

Verified against code 2026-07-16, branch work-in-progress:

- **Chart content streams** (rock-hero-common/core/include/rock_hero/common/core/chart/chart.h):
  exactly four — `notes` (ChartNote: position, string, fret, sustain-in-beats, attack/mute/
  harmonic/touch/vibrato/tremolo/accent, bend curve, slide waypoints), `shapes` (ChartShape:
  position, sustain-in-beats, index into the template table — one mechanism for chords, held
  shapes, and arpeggios), `templates` (ChordTemplate: name, per-string frets/fingers, referenced
  by index), and `fret_hand_positions` (FretHandPosition: position, fret, width — a step
  function: "where the hand sits from this point on"). Tuning is chart-level, not ranged.
- **Grid arithmetic exists** (plan 40 Phase 1, complete): `advanceGridPosition`, `beatDistance`
  (exact inverse pair on the global-beat axis, signature-aware), `sustainEndPosition`,
  `snapGridPosition` in rock-hero-common/core/.../chart/grid_arithmetic.h — the position math a
  rebase-and-relay clipboard needs is already landed and tested.
- **Mutable-chart pipeline exists** (plan 40 Phase 2, complete): `Session::currentChart()` with
  `chartRevision()` bump-on-acquisition, write-on-save, revision-keyed tab projection cache.
- **Selection model does not exist yet**: plan 40 Phase 3 (note selection/caret/marquee) and
  plan 47 Phases 2–3 (time selection state + ruler gesture) are both unstarted. This plan
  consumes both. Note the settled interaction grammar **already includes 2D-area
  multi-select**: the plan 40 Phase 3 marquee is a box over strings × time (empty-lane drag) —
  the REAPER-style area select the user raised on 2026-07-16 is that marquee, not a gap.
  (Shift+click was reassigned on 2026-07-17: it now creates this plan's time-range object —
  see the settled-gesture bullet below.) The two selections are complements, not competitors:
  the marquee picks *objects* (surgical, per-string), the time range picks *a span of the
  chart* (everything, all streams, including step-function context like FHPs that object
  selection cannot express) — the user's own read that whole-span capture suits a tablature
  format better than a DAW's object model is the recorded rationale for this plan existing
  alongside the marquee. Q6 sets the command-precedence rule between them.
- **Tone model** (for Q8): tones are a song-level UUID catalog; tone changes are the song's
  flat UUID-keyed list; tone parameter automation is musical-position truth in song.json with
  a derived Tracktion curve; VST plugin state lives in Tracktion-managed files, not song.json;
  plan 50 shipped portable `.tone` files (export/import of a tone incl. plugin state). Like
  fret-hand positions, a tone change is a **step function** — the tone governing a range start
  usually precedes the range.
- **Sections and tone changes are song-level**, not chart-level: sections live in song.json as
  the song's list; tone changes are the song's flat UUID-keyed `toneChanges` list. They are not
  part of the `Chart` value and would need separate machinery to participate in range copy
  (Q6).
- **Time signatures**: `GridPosition` beats are signature-denominator units (a "beat" in 7/8 is
  an eighth note); tempo is quarter-note-referenced and metronome-linear; the grid is
  note-value-authoritative via the measure walker. `ChartNote::sustain` and
  `ChartShape::sustain` are Fractions **in beats**. This is why cross-signature paste is not
  trivially "subtract and add positions" — see Q2.
- Related answered decisions this plan must stay consistent with: **40-Q2** (same-string sustain
  overlap on edit → auto-truncate inside the same undo entry), **40-Q3** (interaction grammar:
  glyph click selects, empty click seeks, empty drag marquees, Alt = insert quasimode), **41-Q1**
  (signature edits preserve global-beat positions), **47-Q1** (ruler band is the time-selection
  drag surface), **47-Q2 recommendation B** (selection auto-engages looping — interplay in Q7).
- **Settled 2026-07-17 — the content creation gesture** (from
  docs/plans/in-progress/chart-span-and-selection-model.md §7): Shift+click in the content
  area creates this plan's time-range selection object, Guitar Pro-style — one big timespan
  highlight, **replace** semantics, anchored at the last non-Shift selection action;
  Shift+clicks while held re-extend from that same anchor; with no prior anchor the first
  Shift+click acts as a plain click. This settles the creation gesture and display; what
  operations do with the range (capture, copy, paste, delete) remains this plan's open
  Q1..Q8 agenda. The ruler drag surface (47-Q1) remains the second creation gesture.

## 6. Dependencies

- docs/plans/roadmap/47-editor-loop-selection.md — Phases 2–3 must land first (the selection
  state and ruler gesture this plan reuses). This plan adds edit-range consumers to that
  selection; it changes nothing about loop engagement (except possibly Q7's answer).
- docs/plans/roadmap/40-chart-editing.md — Phases 3–4 must land first (selection model, mutation
  intents, undo mementos); Phase 9 is the clipboard-codec twin (single-codec coordination rule
  in Non-goals). Phase 9's transpose/duplicate/bulk-edit scope is untouched.
- docs/plans/roadmap/41-tempo-map-authoring.md — no code dependency, but Q2's position-basis
  decision should be made with 41-Q1's answered philosophy (global-beat preservation for TS
  *edits*) on the table, because this plan's recommendation deliberately differs for
  *relocation* (see Q2).
- docs/plans/roadmap/42-chart-validation.md — advisory lint is the natural home for post-paste
  residue reporting (orphaned templates, signature-mismatch notices) if Q4/Q2 choose the
  lenient options.

## 7. Proposed design (discussion draft — every load-bearing choice is a Q below)

One pure, headless transform pair, plus editor policy around it:

1. **Capture**: `captureRange(chart, signature_map, range) -> RangeClipboard`. The payload
   stores, for every captured event, its **offset from the range start** and its **duration**
   in the Q2-chosen basis, plus the chord templates referenced by captured shapes (by value),
   the source range's total length, the source tuning/string count, and a payload format
   version. Serialized in the document JSON grammar so the clipboard is inspectable and
   testable (and OS-clipboard portable if Q5 chooses that).
2. **Apply**: `applyRange(chart, signature_map, clipboard, destination_anchor, mode) -> chart'`
   — deletes the destination window per Q3's mode, re-lays every event by walking the
   destination signature map from the anchor, reconciles templates per Q4, and re-sorts the
   four streams. Pure function → directly unit-testable against multi-signature fixtures
   without any UI.
3. **Editor policy** (editor-core handlers + keys): Ctrl+C/Ctrl+X/Ctrl+V/Delete over the
   selection-precedence rule (Q6), one undo memento per operation, snap-consistent anchor =
   the timeline cursor.

Suggested placement: capture/apply beside `grid_arithmetic` in common/core chart/ (pure chart
transforms over common types, heavy test surface, mirrors the Phase 1 precedent); the clipboard
*policy* (what Ctrl+C acts on, OS-clipboard transport, undo) in editor-core per plan 40 Phase
9's placement. If the discussion prefers keeping everything editor-core until a second consumer
exists, nothing in the design changes — placement is a reviewable line item at execution time,
not a Q.

## 8. Open questions for the user — ALL require sign-off (gate G52-RANGE-EDIT)

Mirror all of these into docs/plans/roadmap/00-roadmap.md (Decisions needed). None of the
recommendations below take effect without an explicit answer.

1. **52-Q1 — Capture boundary policy.** What is "within the span"?
   - Proposed baseline: an event is captured when its **onset** lies in `[start, end)`.
   - (a) Sustains that ring past the range end: carry the **whole** sustain (paste may then
     ring past the pasted window) vs (b) truncate at the range end.
   - Straddling-in content (starts before `start`, rings into the range): (c) exclude entirely
     vs (d) materialize a clipped copy starting at `start`.
   - **Fret-hand positions are special**: the FHP governing the range start usually *precedes*
     it (step function). (e) materialize the active-at-start FHP as a captured entry at offset
     zero — otherwise a pasted passage arrives with no posture and inherits whatever hand
     position happens to precede the destination; vs (f) capture only FHPs with onsets inside.
     The same materialize-at-start question applies to a shape span straddling `start`.
   - **R (discussion input): onset-in-range + (a) carry whole sustains + (c) exclude
     straddle-in notes/shapes + (e) materialize the active FHP at offset zero.** Whole sustains
     because clipping rewrites content the user selected by eye; excluding straddle-in notes
     because half a note is not a note; materializing the FHP because posture is exactly the
     "ALL content" the user asked to carry. Weakest link: (c) vs (d) for shapes is genuinely
     debatable — a shape is notation over notes, and if its in-range notes came along, a
     clipped shape might serve them.

2. **52-Q2 — Clipboard position basis (the time-signature question).** How are offsets and
   durations stored so paste works when source and destination signatures differ?
   - (A) **Beat-distance** on the global-beat axis (`beatDistance`/`advanceGridPosition`
     directly). Cheap and exact; consistent with 41-Q1's stance for TS edits. But beats are
     denominator units: a 4/4 riff (quarter-note beats) pasted into a 7/8 section becomes
     eighth-note-beat spacing — it plays **twice as fast** at the same tempo. Sustains
     (stored in beats) need no adjustment, which is precisely the problem: their musical
     meaning silently changes.
   - (B) **Note-value distance** (whole-note fractions, the axis the grid walker already treats
     as authoritative): capture integrates each offset/duration through the **source** signature
     map into note-value units; apply walks them back out through the **destination** map into
     measure:beat+fraction positions and beat-denominated sustains. A riff of eighth notes stays
     eighth notes — "what you copied is what you hear," regardless of destination signatures.
     Cost: the two signature-map walks, and positions can land on awkward (but exactly
     representable) sub-beat fractions when signatures differ; a sustain spanning a signature
     change inside the *destination* needs the walk to convert piecewise.
   - Either way: same-signature copy/paste (the overwhelmingly common case) produces identical
     results, and a **paste-time notice** should surface when the destination signature span
     differs from the source (advisory, not blocking — pairs with plan 42).
   - **R (discussion input): B.** Preserving note values matches musical intent and the
     grid's note-value-authoritative design; A optimizes the implementation, not the outcome.
     This deliberately differs from 41-Q1 (A there) because editing a signature *under* content
     and *relocating* content across signatures are different operations with different
     least-surprise answers — worth confirming the user agrees with that distinction.

3. **52-Q3 — Paste mode.** The user's initial direction: paste at the cursor and overwrite
   everything in the range.
   - (A) **Replace-range**: delete all captured-stream content whose onset falls in
     `[cursor, cursor + clipboard_length)` (length in the Q2 basis), then insert. Predictable,
     idempotent-feeling, matches the user's stated expectation. Sub-question: a sustain ringing
     *into* the destination window from before it — (i) auto-truncate at the window start
     (40-Q2's answered precedent: every commit point stays valid, one undo entry) vs (ii) leave
     it overlapping and let 40-Q2 handling fire per-string only on actual collisions.
   - (B) **Merge/overlay**: insert without deleting; per-string collisions resolved by 40-Q2
     truncation. More surgical, more surprising.
   - (C) Ripple insert — **excluded from v1** (Non-goals), listed only so the exclusion is a
     signed decision rather than an omission.
   - **R (discussion input): A with sub-answer (ii)**, and B recorded as a possible later
     modifier-paste (e.g. Ctrl+Shift+V) rather than a v1 mode. (ii) over (i) because deleting
     only what the paste actually collides with keeps the operation minimal; (i) is defensible
     if the user prefers a strictly clean window.

4. **52-Q4 — Chord-template reconciliation on paste.** The clipboard carries templates by
   value; destination charts have their own table (index-referenced).
   - (A) **Dedupe-by-value**: for each carried template, reuse a value-equal destination entry,
     else append; remap shape indices. No garbage collection of templates orphaned by the
     overwrite-delete (harmless residue; plan 42 lint may report unreferenced templates).
   - (B) Always append (duplicates accumulate).
   - (C) Dedupe + garbage-collect orphans in the same operation.
   - **R (discussion input): A.** B pollutes the table; C makes paste mutate content the user
     never touched (a template still referenced elsewhere must never be collected, so GC needs
     whole-chart reference counting inside every paste — cost without user-visible benefit).

5. **52-Q5 — Clipboard transport and cross-chart paste.**
   - (A) **In-memory editor clipboard only** (session-scoped object).
   - (B) **OS clipboard carrying the versioned JSON payload**: survives across projects and
     editor restarts, inspectable, and free interop with plan 40 Phase 9's codec. Requires the
     payload to carry tuning/string count and a paste-time compatibility check.
   - Cross-arrangement/cross-project paste policy (applies under either transport): follow plan
     40 Phase 9's answered stance — **allowed when string counts are compatible, refused with a
     message otherwise**; tuning differences paste literally by fret (charts are fret-truth)
     with an advisory notice when tunings differ.
   - **R (discussion input): B.** The codec is required anyway (Phase 9), so OS transport is
     nearly free and makes "copy a riff from that other song" work — a real charting workflow.

6. **52-Q6 — Scope of "ALL content" and command precedence.**
   - Content scope: notes + shapes + templates + FHPs (the four chart streams) is the proposed
     v1 payload. **Sections and tone changes are song-level, not chart-level** — including them
     would give copy/paste write access to song-scoped data from a chart operation. (A) chart
     streams only in v1; (B) also carry tone changes in-range — now elevated to its own
     question, **Q8**, at the user's direction; (C) also carry section boundaries. **R: A for
     the chart half**; tones/automation are decided under Q8; C stays a recorded follow-up.
   - Command precedence: once both a **note selection** (plan 40 Phase 3) and a **time
     selection** (plan 47) exist, what do Ctrl+C/Ctrl+X/Delete act on? (A) non-empty note
     selection wins; otherwise the time selection; the two are never both "active" for
     commands. (B) time selection always wins when present. **R: A** — the note selection is
     the more deliberate, more recent gesture in the 40-Q3 grammar (creating it required
     clicking notes), and it keeps today's Delete semantics unchanged.

7. **52-Q7 — Interplay with 47-Q2 auto-loop.** Once the selection also serves copying, a
   charter who drags a range merely to copy it would change what Play does under 47-Q2's
   original auto-loop recommendation.
   - (A) Keep 47-Q2 = B (auto-loop): the selection only affects playback when playing; accept
     the occasional surprise loop.
   - (B) Flip 47-Q2 to an explicit arm so looping is always deliberate.
   - **User leaning recorded 2026-07-16: (B), specifically 47-Q2's new option C — a persistent
     Loop toggle button in the transport strip (the REAPER Repeat model).** The selection is
     then always a plain time range; looping engages only while the button is on. This
     dissolves the dual-use tension entirely (copying never touches playback behavior), at the
     cost 47-Q2-B was avoiding (one more transport control). Supersedes this plan's earlier
     (A) recommendation. **Confirm at sign-off jointly with 47-Q2** — one answer, recorded in
     both plans.

8. **52-Q8 — Tone and automation-lane copy semantics** (added 2026-07-16: the user wants good
   copy/paste semantics for tones **including automation lanes** designed, not defaulted).
   Everything here is song-level data (see §5 tone-model inventory), so this is deliberately a
   separate question from Q6's chart streams — a chart-range operation writing song-scoped tone
   data is a real ownership boundary to cross knowingly.
   - What a tone-aware range copy would carry: tone changes with onsets in-range, automation
     points in-range per lane, and — the FHP symmetry — optionally a **materialized entry for
     the tone governing the range start** (tone changes are a step function exactly like
     fret-hand positions; Q1's materialize-at-start reasoning applies verbatim).
   - **Within-song paste** is the easy half: tone UUIDs resolve against the song's own catalog;
     tone changes and automation points re-lay on the Q2 position basis; the overwrite window
     (Q3) deletes in-range tone changes/points the same way it deletes notes.
   - **Cross-song paste** is the hard half: the destination song's catalog lacks the UUIDs, so
     the payload must carry the tone definitions themselves — including VST plugin state, which
     lives in Tracktion-managed files, not song.json. The natural vehicle already exists:
     plan 50's portable `.tone` container. A cross-song tone paste would effectively perform a
     .tone import per carried tone (with the existing dedupe/naming semantics) before wiring
     the pasted changes. Automation lane identity across songs (plugin_id bindings) needs the
     same durable-binding treatment the tone catalog model settled.
   - Options: (A) v1 pastes chart streams only (Q6-A); tone/automation range copy is a signed
     follow-up phase in THIS plan, designed now, built after the chart half proves out;
     (B) tone-aware within-song copy in v1, cross-song deferred; (C) full tone-aware copy
     including cross-song .tone transport in v1.
   - **R (discussion input): A, with the follow-up phase sketched before the gate closes** so
     the payload format reserves room for tone sections from day one (a format-version bump is
     cheap, but designing the chart payload knowing tones are coming avoids a second schema).
     (B) is the fallback if charting practice needs tone copy sooner than expected.

## 9. Phased implementation (sketch — BLOCKED until G52-RANGE-EDIT closes)

Phase boundaries will be finalized after sign-off; the expected shape:

### Phase 0 — Discussion and sign-off (the gate)

Walk 52-Q1..Q8 with the user (52-Q7 jointly with 47-Q2). Record every answer in this file and
in docs/plans/roadmap/00-roadmap.md. Re-verify the §5 inventory against current code first —
plans 40/47 will have moved by then.

### Phase 1 — Pure range transforms + payload codec (headless)

`captureRange`/`applyRange` + the versioned JSON payload per the signed answers; exhaustive
tests: multi-signature round trips (4/4↔7/8↔3/4), straddling sustains per Q1, FHP
materialization, template dedupe/remap per Q4, overwrite windows per Q3, string-count refusal
per Q5 — all against fixture signature maps, no UI. Coordinate with plan 40 Phase 9: single
codec, whichever lands first.

### Phase 2 — Editor intents, undo, keys, and transport

Controller handlers (copy/cut/paste/delete-range) over the Q6 precedence rule; one memento per
operation; OS-clipboard transport if Q5 = B; keys land scattered per the plan 46 interim rule
and migrate under G46-KEYMAP. Paste-time advisory notices (signature mismatch, tuning
difference) through the editor's existing message surface.

### Phase 3 — Selection-surface affordances

Whatever the signed answers imply visually: the time selection rendering plan 47 Phase 3 ships
is reused as-is; add only paste-destination preview feedback if the discussion asks for it
(the 40-Q3 grammar's ghost-preview precedent applies).

## 10. Rollback/abort notes

- Everything is additive editor behavior behind new intents plus pure functions with no other
  callers; each phase reverts as a unit.
- The payload format version is present from the first byte written to the OS clipboard, so a
  later format change never has to parse unversioned payloads (and per the project's
  no-legacy stance, a version bump may simply refuse old payloads).
- If Q3's overwrite semantics prove wrong in practice, the paste *mode* is a policy parameter
  of `applyRange` — swapping the default is a one-line editor-core change, not a codec change.
