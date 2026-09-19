# Technique Review Walkthrough — the decision queue

Status: **ACTIVE — combed through one decision at a time.** This file is the record of that queue:
it survives any session, and each item closes here first, then flows into
`technique-compatibility-and-hardening.md` (the matrix authority), `legato-final-spec.md`, or code.

Working order: top to bottom unless redirected. Each open item carries the agent's recommendation so
the user can rule with full context in front of them.

> **Legato stores no direction.** `Hammer` and `Pull` are MOTIONS `resolveLegato` derives, never
> stored values: the stored claim is `Legato` ("this onset connects to its same-string predecessor")
> or `LeftTap` (the local statement). There is no repair engine; the relational rules are resolver
> clauses rather than validation rows, and what an unjustified claim gets is a settle sweep at
> commit points. Where an item below names a motion, read it that way — the physics each item
> settles is unchanged and still binding.

## Work queue (LIVE — the session task list mirrored here so a disconnect loses nothing)

Keep this list and the session task list in step.

> The highway's note-ART pass (harmonic heads, the accent light, the mark-sizing law, vibrato
> motion) runs on a separate track with its own live state at `highway-note-art-state.md`. Neither
> file restates the other; on a fresh session read both.

- [x] **W1 — The hold test reads the note's own STORED ring, and the span-implied hold is a display
  length that is deliberately not a rule input.** `predecessorHoldReaches` (`grid_arithmetic.h`)
  judges the predecessor's stored ring under strict adjacency; `chartHolds` (`chart_presentation.h`)
  answers the separate question of how far a hand-shape span pins a member's head, and its own
  contract states that nothing in it can change the hold test. The span-blind concern that opened
  this item is therefore dissolved rather than patched: every note carries its actual ring, so a
  span has nothing to supply the test. **The ruling that still binds:** ONE hold concept, both
  readers — do not split sounding-hold from fretted-hold. "A dead chug is not held" is a physical
  statement, not merely a display one, and it is stated per MEMBER: a dead member is choked rather
  than held, which is also what chokes an entirely dead group, so no unanimity rule is stated
  anywhere.
- [x] **W3 — Pending fret entry with invalid-red feedback.** Nothing commits mid-entry: the typed
  value is provisional, drawn on the head(s) in the accent-bordered box, RED when it cannot be
  applied, committed as one undo entry when the entry settles (second digit, the 750 ms window's
  scheduler wake with the injected clock as authority, or any action's settle prologue — at the
  action gate, the legato settle's head, and each direct chart verb head) and discarded when
  invalid. All mid-entry chart mutation is gone; both latent bugs (paused-seek widen, mid-window
  save split) are unrepresentable; the controller test harness carries the deferring scheduler and
  its injected clock.
  - **The rules the entry obeys.** (1) An INVALID value is STICKY — it outlives its window and
    persists red until a further digit extends it or Esc / any other intent discards it, for
    immediate digits too, because a refusal display that vanishes on a timer, or never appears, is
    no display. (2) The box's plate FLIPS with validity — valid rides the lane's near-black in the
    digit's white, invalid flips to a white plate with the theme's red, so the polarity flip itself
    is the glance signal (the mute plate-flip mechanism) and red-on-white carries the error idiom at
    full contrast. (3) The window wake is TOKEN-ONLY: a clock re-check could strand a
    marginally-early wake as a pending entry nothing would settle, and a correctness check must not
    be able to create a stuck state.
  - **The digit split follows from the fret cap.** The predicate is *"could a second digit reach a
    value this digit alone cannot?"* — `value >= 1 && value * 10 <= g_max_fret` — which at
    `g_max_fret` 24 (`chart_rules.h`) gives provisional `{1, 2}` and immediate `{0, 4..9}` plus `3`.
    `0` is immediate deliberately: arming the window for a leading-zero path nobody types would make
    the open string, the commonest value on the instrument, wait out the window. Keep the predicate
    general so a raised cap needs no rework. The cap itself is 24 because the drawn board lays out
    24 frets and silently clamped anything above them onto the last fret; `g_highway_fret_count`
    (`highway_metrics.h`) DERIVES from it, and raising it is one edit gated on a way to STATE
    positions above the board.
  - **An ENTRY BOX for the whole pending state, red text inside it for invalid** — better than an
    invalid-only chip. The box is drawn for as long as the value is provisional — valid or not — and
    disappears when the entry settles, reusing the plate the mute heads already draw permanently.
    Three things fall out that an invalid-only chip does not give: the provisional state itself
    becomes visible (a chip leaves a VALID pending value looking exactly like a committed one, which
    is a lie about whether the value landed); the box supplies a known background, so red text is
    readable over any string-lane colour and the red-on-a-red-lane hazard is structurally gone; and
    red versus white text inside it differ in LUMINANCE as well as hue (~0.21 against 1.0), so the
    valid/invalid signal survives protan and deutan vision without a second shape. A muted head
    already draws a plate permanently, so the pending box differs from it by wearing the editor
    accent as a border — pending is an editor state and accent is the editor's active colour — which
    also keeps shape-plus-colour redundancy on muted heads.
  - **The ENTRY case: the box and the live projection.** For an entry begun at the caret's slot the
    box is the half the drawn preview does not take. A valid value's plan is projected into the
    published chart at once, so what it would create — a head, a point, a slide-out — draws as an
    ordinary mark under the box while the stored chart and history stay untouched, and everything
    else states itself in the box: a refused value in red, a value the selection retypes in the
    box's ordinary form. Complementary at the publisher, so one value is never drawn twice in one
    column. Warrant: the dissolve law's visibly-pending requirement (see
    `docs/plans/todo/arpeggio-authoring.md`). *(Written when the preview was the insert ghost
    carrying the pending fret; the ghost is retired 2026-09-11 and the projection is what draws.)*
  - **Provisional drawing is editor chrome, not the shared paint core** (the core's contract is that
    both products produce identical notation pixels, and the game has no keyboard entry) — but
    export ONE primitive, a head painter with a text/colour substitution, so the digit's typography
    and placement cannot drift from the committed head the way the retired insert ghost once did.
    Unbuilt: the shipped head primitives are `tabNoteHeadText(note, fret_at_head)` and
    `strokeTabNoteHeadOutline` (`tab_paint_core.h`).
  - **The window rides `IMessageThreadScheduler::callAfterDelay`** with `safeCallback`, and the
    injected clock is the authority so a stale or duplicated wake is a no-op. Trap for anyone
    extending it: `ImmediateMessageThreadScheduler` runs the work SYNCHRONOUSLY, so a controller
    test needs the deferring scheduler plus a settable clock.
  - **Refusal is a typed channel, not silence.** The planners return `std::expected<ChartEditPlan,
    ChartPlanRefusal>` with `{NoChange, Invalid}`, `finalizePlan` owning the classification and
    per-planner early-outs classified where they occur; `planSettleChart`'s distinct emptiness and
    `planSetLegato`'s typed report are untouched, and the refusal kinds are pinned by tests. Without
    it a valid no-op and a refusal were the same `nullopt`, so no caller could paint one red.
  - **`EditorUndoHistory::replaceTop` survives** — the legato settle sweep folds its flatten into
    the burst's own chart-notes entry through it. Only the multi-digit widen's use of it went away
    with the pending model. W7's own need is a different verb, `dropTop()`, which drops the entry
    rather than rewriting it.
  - **Undo is NOT special, and that deletes work.** Nothing is committed until the entry settles, so
    undo has nothing of its own to cancel: it is just another action, and the uniform prologue —
    *every action and intent settles the pending entry first, commit if valid, discard if invalid* —
    already covers it. That drops any `undo_available` / `undo_label` special-casing, i.e. two
    lying-affordance risks, and leaves ONE settle rule instead of a rule plus an exception.
    Consequence accepted: `Ctrl+Z` pressed inside the window on a VALID pending value commits it and
    then undoes it, so the value appears and is removed with a redo entry left behind — honest,
    because the value really was a valid edit.
  - **Esc's rung is claimed by an INVALID pending value only.** Esc cancels the *problem*, so an
    invalid value discards and the caret survives for an immediate retype, while a VALID pending
    value is not a cancellable thing — it falls through to the caret rung and commits on the way
    through the uniform settle. That matches the ladder's own semantics (one press takes the topmost
    APPLICABLE rung) instead of adding an Esc special case, and it means a valid typed value is a
    value you meant.
  - **Chord semantics: red marks EVERY selected head.** `validateChartNotes` returns the first
    violated rule with no note identity, and relational refusals are properties of a PAIR, so
    per-note attribution is both unavailable and often ill-defined. "Which note" belongs in W5's
    feedback channel, not in the colour.
  - **Deferral does not make the digit inert.** `finalizePlan` runs in FULL every keystroke (repair
    and gate included) and the digit visibly does something — only the *apply* waits. Do not preview
    the repaired neighbour in the overlay: that would re-import the mid-entry flicker as chrome.
  - **Still follow-on, not part of this ship:** the W5 counted-skip and W6 locked-tail payloads need
    their own notice surface. The pending box carries a typed value, not a report; their entries
    below stay open for exactly that surface.
- [x] **W4 — E25's muted-tail rules (D16 below).** E25 is a PRESENTATION rule: it is rule 4 of
  `presentedChartNotes`, so the drawn tail goes and the STORED ring stays. Nothing trims a stored
  ring, and a dead note therefore keeps the duration of its damped stroke — which is the timing a
  legato claim after the cluck reads. The load policy is the other half: load NORMALIZES and never
  refuses, so repairs live in `normalizeChart` (the one normalizer every load path calls, with
  `sweepUnjustifiedLegato` as a late stage), the package read runs the same shed before validating
  and reports through its existing `conversions` channel, structural violations still refuse loudly,
  and the warning is a one-shot themed notice at open naming rule and positions, opening the session
  dirty with the file untouched until the user saves. Validation's droppable section is the fixpoint
  `note == normalizeChartNote(note)`. The display needed no branch at all: under E25 every surviving
  muted tail already carries teeth or a diagonal. The full design record is
  `docs/plans/completed/e25-muted-tail-implementation.md`, and the principle is in
  `architectural-principles.md`.
- [ ] **W5 — `L` eligible-subset fix and counted feedback: the data ships, the surface does not.**
  The eligible-subset half: the controller asks `planSetLegato` itself whether applying would change
  anything (the oracle, never a restated predicate), and only when it would not does the press clear
  — targeting just the legato subset, so a rider `Tap` or `Pinch` keeps its own attack. A second
  press always undoes the first; pinned by the mixed-selection round-trip test. The counted feedback
  is the verb's own typed return — `ChartLegatoPlan{plan, skipped, reason}` with `ChartLegatoSkip`
  naming the four refusal classes, every class pinned by `test_chart_edits.cpp`. **Nothing is
  shown**, because the view's only reporting seam is a modal `showThemedWarningBox` titled "Could
  not complete request", which would pop a dialog on the commonest press in charting (a phrase's
  first note) to announce that nothing had failed. `L` is silent when it applies nothing, at parity
  with the left-tap and pick-slide toggles. **Open: the non-modal notice surface** — the per-note
  information the planner already computes is built and waiting for it.
- [ ] **W6 — Tail lock + locked-tail feedback (40-Q5), SCOPED TO SLIDES ONLY.** A connection claim
  stores no direction, so shrinking its predecessor's tail drops the mark live, regrowing restores
  it inside the burst, and the settle sweep flattens what is left as one folded batch — nothing to
  lock and nothing to break. Slides keep both, because keyframes are real data. The break verb is
  dissolved into W10: `Shift+L`'s apply-or-clear toggle severs an existing link, so no verb or
  binding of its own is needed. The feedback is **editor-only** (not visible in 3D). **Still blocked
  on the notice surface**, which the locked-tail feedback and W5's count both wait on.
- [x] **W7 — The legato assist and the technique toggle window.** The assist lives inside
  `planSetLegato`: when the hold test is the only blocker, the plan grows the predecessor's tail to
  the margin point and re-asks `resolveLegato` under the grown tail — the same only-blocker test,
  one authority instead of two — deriving the direction in the same entry, pre-checked against
  `sustainBoundOf` so the assist never authors what a manual drag could not, groups included, with a
  blocked note skipped whole. It skips gesture-carrying predecessors (a scrape, or any slide-out),
  whose tail is authored geometry rather than slack. The window is `{keys, history_position}` plus
  the applied plan, validity being the fret window's own proof, reversal via the plan's inverse and
  `EditorUndoHistory::dropTop` (which mirrors `replaceTop`'s guards). It stores only the keys,
  because the burst's plan already lives once in `m_chart_notes_top`, which the settle sweep reads
  too, so the two verbs cannot disagree about what the burst did. One ruled addition: when the entry
  the reversal would drop is the reachable clean state, it pushes the exact inverse as a NEW entry
  instead of dying, so the grown tail still comes back. Both toggle halves arm it, so a double press
  restores an authored mix the clear would flatten.
- [x] **W8 — Import converts junk hopo flags.** A Guitar Pro hopo destination with no fret motion —
  equal or absent same-string predecessor — lands as a plain `Pick` with a counted conversion note,
  through the shared settle sweep the importer runs at build completion, i.e. the same code every
  load path uses. Import must not create what the verb would refuse.
- [ ] **W9 — Rulings the deep review needs.** Twelve questions, in the section below. **Open: W9-F,
  to be ruled together with W9-D's open glyph choice — both are the one question of how 2D says
  *pitched* versus *falls away* — and W9-G, which waits on the bend study with it.** Everything else
  from that review was fixed in place.
- [x] **W10 — The tie/slide-link verb (`Shift+L`) and the split-tail law.** BUILT whole 2026-09-12 as
  "Split or Join at Selection" (`planToggleJunctions`): the join is the split's exact inverse, and
  the tie falls out of the commit law rather than being built. The pending-intent mechanism and the
  `LegatoMotion::Continuation` amendment are SUPERSEDED — see the dated amendment at the top of the
  W10 section below, which also carries the one item still open (the importer's own tie merge, in
  `docs/tracking/backlog.md`). Absorbs W6's break verb. Refusal feedback lands with W3's channel.
  The technique-letter amendment that opened it (legato `H`→`L`, left tap `Ctrl+H`→`Shift+T`, `H`
  freed for harmonics) is recorded in `keymap-matrix.md`.
- [x] **W11 — Slide-out ends the note, and the stored offset is gone.** Nothing rings after a
  slide-out — the unpitched exit IS the note's end; a pitched path that finishes mid-sustain simply
  ends at its last keyframe with no slide-out, and the sustain rings on. So the exit's moment is the
  ring's end by definition, and a stored copy could only ever drift from it. `slide_out` is
  `std::optional<int>` — a fret and nothing else (`chart.h`). The payload rule is "every keyframe
  strictly before the ring's end when a slide-out exists", and the scrape re-termination keeps only
  its fret-compression half (`clipPayloadsToSustain`).
- [x] **W12 — The right-hand chord box.** Two or more taps struck together derive a tapped chord box
  in 3D through the same chord-membership logic as every box: `makeHighwayTapOnsets` publishes
  `HighwayTapOnsetViewState`, and the renderer's box builder raises one wherever `count` is two or
  more, spanning the taps' own fret extent instead of the fretting hand's; new tap positions get
  their own orange floor numbers. The hand-partition law (W9-C) therefore RECOGNIZES shipped design
  rather than ordering work: posture notation is fretting-hand-only, the tapping hand has its own
  parallel family, and no right-hand arpeggio notation exists. 2D shows tapped simultaneity by the
  vertical alignment of the T-plate heads; whether it wants more is open only if live use says so.
- [ ] **W13 — Techniques on a point inside a gesture.** The defect that opened it: every technique a
  note could carry was a field on `ChartNote`, so it applied to the WHOLE gesture, while a slide
  keyframe carried only `{offset, fret}`. In the user's words: *"An unpicked slide waypoint can
  ABSOLUTELY have its own techniques so I think the original design was broken if it couldn't
  represent this"*, and *"The landing MUST be able to carry its own techniques."* Slide 8→4 legato
  and vibrato the 4: a note-level flag also claims the 8, and the chart could not say "vibrato from
  the arrival onward".
  - **Settled: vibrato is interval state, never whole-note.** A tail may carry MULTIPLE vibrato
    regions; vibrato DURING a slide is legal ("rare but real"); a delayed start mid-hold and an END
    mid-hold are both legal.
  - **Settled: the non-negotiables.** Keyframes/state points MUST be selectable; bends MUST be
    authorable anchored at a keyframe; `Shift+L` on a keyframe DISCONNECTS it from its note (joining
    W10's scope). The editor shows a selectable point at every state change, with the
    clear-then-linger-until-settle-then-dissolve behavior the user specified.
  - **Settled: the dissolve law generalizes** (superseding the path-only form in
    `2d-bend-waypoint-redesign.md`): a pending point dissolves at settle iff it changes NEITHER the
    path function NOR the state.
  - **Settled: the substrate is technique-bearing STOPS, named KEYFRAMES**, with bends as an
    interpolating channel and an optional fret — chosen over interval spans stored on the note by
    the coincident-anchor coupling argument. Split/merge is parity either way once the booleans die,
    and the per-stop validator rework is a cost rather than a wall. The decided design is
    `docs/plans/todo/unified-waypoint-model.md`; W13 closes into that plan, and the bend study
    shrinks to bend display and authoring on that substrate.
  - **BUILT: the substrate and selectability.** `Keyframe` (`chart.h`) is `{offset, optional<int>
    fret, optional<double> bend, optional<VibratoState> vibrato}`, so a point inside a gesture
    carries its own state; the whole-note vibrato bool is gone (`VibratoState { Off, Narrow, Wide
    }`), and the importer no longer smears it — `stateVibratoAt` writes the onset value at offset
    zero and a keyframe channel otherwise, and states nothing when the ring already reads that way.
    (`tremolo` is still OR-merged at the tie and junction sites, deliberately: it is not a channel.)
    `ChartSelectionKey` is the sum `variant<ChartNoteKey, ChartKeyframeKey>`, a keyframe identified
    by (note slot, offset) so sibling edits cannot re-point it; the lane's linked keyframe heads are
    clickable and marquee-selectable, wear the same accent ring every selectable wears, and take
    `Delete`, the vibrato channel's `V`, and `Shift+L`. A keyframe sits on the slot its offset
    reaches along the ring, so selecting one arms the caret there exactly as selecting a note does,
    and the arrows stop on it as on a note (2026-09-09 — it first shipped slotless, and every
    caret path then carried a branch for it; the P1 sighting found all three).
  - **Ruled and live.** `Alt+←/→` steps a selected keyframe's OFFSET by the placement quantum at its
    note's measure — a grid step with snap on, a tick with it off — through the arrow move's own
    planner, which now takes both selection kinds and moves each where it lives (a note by its slot,
    a keyframe by its offset) in one plan and one entry. `Alt+↑/↓` stays inert on a keyframe-only
    selection: a keyframe has no string, and a selected head carries its path across by
    construction. In a mixed selection the note moves and its own keyframes ride at unchanged
    offsets, since an offset is relative to the onset it hangs from. Every bound is the rule
    authority's, reached through the finalize gate — the onset below, the ring above, a neighbour
    beside, a later same-string onset, the capo floor — so a step across a neighbour REFUSES rather
    than swapping, which is the only reading a keyframe's identity allows. The step re-keys the
    selection (`select_exactly`), because that identity IS the offset.
  - **Still open, and untouched by the above:** a DISPLAY question — a keyframe stating no fret
    draws nothing today, so no pointer can reach it — the bend display study's to answer.
- [x] **W15 — The harmonic verbs and the node picker.** `H` states the fret-hand harmonic and
  `Shift+H` the pinch. `Shift+H` is a row of `chartTechniqueLaw` under the shared toggle contract;
  `H` was one too until 2026-09-16, when it stopped being a toggle and became a verb of its own (the
  last sub-bullet below). THE FRET
  YOU TYPE IS THE NODE: the set resolves each note's own fret against the stop its string speaks
  from and writes `fret = 0` plus the node at that stop, then lets `normalizeChartNote` strip what a
  touch cannot carry. Each HAND owns its own clear since 2026-09-16 — `planClearHarmonic` writes the
  fret-hand carriers and `planClearPinchHarmonic` the picking thumb's — and each inverts its own set
  exactly: a `Pinch` becomes the pick it was picked as, and an on-neck touch presses where it was
  touching. Both exist because each row's noun is a harmonic, so its clear must remove one; clearing
  the pinch through the raw attack row instead would leave `Pick + fret 5 + node 17`, an artificial
  harmonic nobody authored.
  - **The range rule is the LABEL WINDOW, not the ceil law** (RULED 2026-09-15, on the
    keymap-matrix `H` rows). `snapHarmonicNode` generalized into `harmonicNodeCandidates`, an
    enumerator of `(position, partial)` rows for a label, with the importer's function-local
    tolerance hoisted beside it as `g_max_node_label_error` — one authority for import and the verb,
    a copy deleted rather than a rule added. Partial is the LOWEST one with a node at that position,
    which is what sounds and the only stable name for a choice.
  - **The candidates are the WHOLE bound, lowest partial first** (RULED 2026-09-15).
    `chartHarmonicNodeCandidates` resolves the typed fret against `g_max_harmonic_partial` (16) —
    every partial validation accepts, not import's snapping cap of 8, which stays where it is
    because raising it would need the corpus re-measured — so most labels name several nodes: a
    typed 5 names the 4th partial's 4.98, the 13th's 4.54 and the 15th's 5.37, and 1, 11 and 13 are
    no longer dead keys. The order is a CONTRACT, not a display preference: the lowest partial is
    the harmonic a charter means by the label and the loudest the string gives, so it is what a
    press stating no choice writes and what the picker opens on, and the keyboard's default and the
    picker's first row cannot disagree. Import's `nearestHarmonicNode` is deliberately not used here
    — under this bound the node nearest a typed 3 is the 13th partial's 2.892, 0.108 from the label,
    while the harmonic a charter means by 3 is the 6th's 3.156, 0.156 away.
  - **The picker is a POPUP at the head the CONTROLLER asks for**, not the pending entry it was
    first built over and not a fork in the view (RULED 2026-09-15). `H` reaches the controller as a
    single intent, and the handler asks for the rows BELOW its settle prologue — then hands them to
    the view port
    (`IEditorView::showChartHarmonicNodePicker`) and returns with nothing written. The view-side
    fork the first build had sat upstream of both, which is what made it wrong: a second `H` after a
    clear opened a picker where it owed a reversal, and a live fret entry could be superseded under
    an open popup. The view opens a `juce::PopupMenu` anchored at the selected note head in the tab
    lane, rows reading `<node> · <ordinal> partial` and NUMBERED from 1 so the FIRST opens
    preselected — JUCE matches `withInitiallySelectedItem` against item IDs, so an unnumbered row
    could never be — giving `Return` the common case in two keystrokes while `Esc` dismisses with
    the note untouched: nothing was committed to reverse. A chosen row returns through
    `onChartHarmonicNodeRequested` and applies at once through the same `planSetHarmonic` a
    choiceless press runs: one plan, one undo entry. A press with only ONE change to make — a label
    naming exactly one node (7, 12, 19, 24), or a clear — skips the menu and settles in the
    keystroke. Nothing pends any more, so the settle prologue's one
    exemption is a digit continuing a live fret entry. One picker per press over a chord — the rows
    come from the member whose own label names the most nodes, a chosen partial binds every member
    whose label offers it, and the rest take their default. The right-click Note submenu's
    "Harmonic..." row runs the same verb the key does, so it reaches the picker down the same
    path and lists no harmonic rows of its own.
  - **RULED 2026-09-16: `H` is not a toggle.** `ChartTechnique::Harmonic` is DELETED and the verb
    left `chartTechniqueLaw` entirely — `H` raises its own action (`EditorAction::ChooseChartHarmonic`
    → `IEditorController::onChartHarmonicRequested()`, command id `EditorCommandId::ChartHarmonic`,
    menu label "Harmonic..."), and the law it runs under is **offer every CHANGE the selection
    allows, and ask only where there is more than one**. **Which rows CHANGE anything is the
    PLANNER's answer**, never a count kept beside it: the verb plans each node row (`planSetHarmonic`)
    and the clear (`planClearHarmonic`) over the live chart, and a `NoChange` plan is not a change —
    zero changes is an inert press, one applies in the keystroke, two or more ask. The node rows are
    those of the member
    whose label names the MOST nodes — a carrier's label being the fret its node lies at
    (`harmonicLabelFret`, the one authority the clear also presses back down), so a note touching
    4.98 is offered the 13th and 15th partials of a 5 — and EVERY one of them is shown, a ticked row
    that changes nothing included, while the **"No harmonic"** row LEADS them, ruled off by a
    separator, only where the clear itself changes something. **Amended 2026-09-17: the clear comes
    FIRST wherever it is offered, on every selection alike.** It sat last, and where it opens
    PRESELECTED — on a note already carrying a harmonic — `Up` from it landed on the highest partial,
    the least likely next choice. Leading the list, the row never MOVES between states, and `Down`
    from it walks the partials from the lowest. The view rules it off from the node rows on whichever
    side they lie, so the order stays the controller's. One change applies with no menu (a 12 writes
    its single node; a 12 already touching it clears); several open the popup, whose payload is
    `ChartHarmonicNodePicker{note, choices, preselected}` — `choices` a
    `std::vector<ChartHarmonicChoice>`, the variant of `ChartHarmonicNodeChoice{node, partial,
    current}` and `ChartHarmonicClearChoice`, the clear row FIRST when offered and the node rows
    ascending by partial after it, and `preselected` an index into that list so a clear row that was
    never offered cannot be preselected or chosen — answered by
    `onChartHarmonicNodeRequested(std::optional<int>)` → `SetChartHarmonicNode{partial}`, an absent
    partial being the clear. The TICKED row is the node the ANCHOR member is touching — the note the
    rows were read from, whose head the menu sits on — rather than a statement about the selection as
    a whole, and the PRESELECTED row is what the
    toggle would have done — "No harmonic" when every member carries a fret-hand harmonic
    (`carriesNeckHarmonic`: a node whose attack keeps it on the neck, a pinch excluded), else the
    lowest partial that CHANGES something —
    so `H` `Return` still clears a harmonic and still sets the lowest partial on a plain note. Labels
    naming nothing (an open string, a pinch) are inert. **Amended 2026-09-16: the clear SPLIT by
    hand** — `planClearHarmonic` writes only notes that `carriesNeckHarmonic` and the picking thumb's
    node is the new `planClearPinchHarmonic`'s, so a pinch selected beside a fret-hand carrier is left
    untouched by `H`. **The run FOLDS instead of reversing**: the
    verb joined the gesture family through `commitChartGestureStep`, with a new EMPTY
    `ChartHarmonicGesture` alternative in `ChartVerbWindowVerb` (five now, and three verbs running
    the fold shape), so consecutive choices on one selection REPLACE one entry and a choice back to
    the pre-run state RETIRES it — `H` `Return` `H` `Return` leaves no trace of a carrier the VERB
    itself produced, by the fold's retire rule rather than by a reversal, while an imported carrier
    whose payload (a bend, a shake) the set normalized away keeps the entry describing that strip, a
    real edit rather than a hole in the fold — and the run ends at the family's commit points (selection change, caret
    move, another verb's edit, undo/redo, save, a committing settle). Opening the picker ends NOTHING
    another verb staged, a menu being a question rather than an edit; the CHOICE's write does,
    through `applyChartEditPlan`'s disarm. There is no second-`H` reversal any more, so the exact
    restore of a node the label cannot name — an imported artificial 17.0 on a fret 5 — is `Ctrl+Z`
    only. **Why the toggle went**: with a multi-valued "on", "restore what the last press removed"
    and "set" diverge, and an invisible window picking the restore made `H` after a clear behave
    differently from `H` on any other plain note. `Shift+H`'s pinch stays a technique toggle, now
    clearing through its own `planClearPinchHarmonic` rather than sharing `planClearHarmonic`; the
    direction recorded (not built) is that it will carry a node/partial of
    its own and adopt the same "offer every change" law when it does.
  - **Scope: natural and pinch only.** The artificial and tap families were carved out the same day
    into `docs/plans/todo/artificial-harmonic-authoring.md`: a node measured from a PRESSED stop
    needs a verb that does not rewrite the fret, which is a different act from this one. **Since
    2026-09-18 (late) both families are DISABLED in the chart itself**: validation refuses a node
    over a pressed stop under any attack but the pinch, and a node under the tap attack, so no
    verb, import or file can produce one (`chart_rules.cpp`; `harmonic-display-followups.md` has
    the parked display items and the reopening recipe).
  - **Two retype defects fixed with it.** `planRetypeFrets` now REFUSES the sounding stop of a
    fret-hand harmonic (the derived-held refusal's shape, so the pending box paints red), and a
    retype under any other node moves the node with its stop — a node is `stop + offset` on a
    logarithmic board, so leaving it behind authored an offset the harmonic never had.
  - **Still deferred:** the counted skip. A press that only skipped is silent, exactly as the
    legato verb's is, until W5's non-modal notice channel exists. An open string states no position
    at all, and a charter learns that only by the mark not appearing.
- [x] **W14 — Legato after a DEAD note, and after a SCRAPE.**
  1. **A dead predecessor is an ordinary one** and justifies a connection like any other note. The
     premise that a deadened string has no energy to carry does not hold: the hammering finger
     supplies the energy in every hammer-on, and what a dead note lacks is a sounding *pitch* to
     connect from — a notation distinction, not a playability one. Disqualifying it would also turn
     every imported "dead note → h" (the standard muted-scratch-then-hammer of funk rhythm guitar)
     into a picked note, since the GP importer maps every hopo destination to a `Legato` claim, and
     would do the same to the successor of any note deadened with X. The 3D highway draws a
     `LeftTap` and a hammer identically, so the distinction buys the player nothing. D7's muted half
     stands.
  2. **The bound is the rule, uniformly.** Past the kept-sustain bound a claim needs the hold to
     reach; a distance exception for dead notes was rejected because there is no principled
     distance, and anything else is a second hold rule. Since E25 became a presentation rule the
     bound bites the same way for every note: the hold test reads the ring the note carries, so a
     chug chained to its restrike connects and a cluck the hand left long before does not.
  3. **E27 — a scrape predecessor justifies no connection.** A scrape's "released fret" is the
     PICK's position, not a finger's, so nothing waits at its end to release or continue from — the
     pull-from-a-scrape D7 once allowed was only ever authorable by treating the slide-out's end as
     a released finger, and that was the fiction. One resolver clause, and a net deletion:
     `releasedFret` lost its scrape branch (the resolver was its only reader) and the `L` assist's
     gesture-carrier guard lost its `PickSlide` clause (a scrape never reaches it). D7's scrape half
     is revised; the trail-off guard stays and is tested on its own.

## W9 — Rulings the deep review needs

A multi-pass review of everything the technique work touched produced these, and only these, as
questions the user has to answer. Everything else it found was fixed in place. Each item states the
defect, why it needs a ruling rather than a fix, and the options with the agent's recommendation.

- [x] **W9-A — Is a span-held strum notation, or a 3D affordance? RULED: notation, through one
  shared derivation.** `chartHolds` (`chart_presentation.h`) is the ONE authority for how far a
  hand-shape span pins a member's head, and `ChartViewState::display_hold_ends` is resolved from it
  rather than recomputed anywhere — so no third implementation exists. It is the board's value
  alone: the 2D lane states a span-held strum as the CHORD BOX and draws every tail to the note's
  presented end, which is why the two surfaces cannot disagree about a hold. The alternative
  considered and rejected was a second derivation for the tab projection, which is precisely the
  copy this option exists to avoid.
- [x] **W9-B — Should the two projections become one? RULED: FOLD, and the shipped shape went one
  step further than the ruling.** `TabNoteView` and `HighwayNoteView` were field-for-field
  identical, the bend views were identical, and the two projection functions differed only by where
  display string padding was resolved — the root cause of copied-or-omitted derivations, the
  two-producer defect shape. Building it showed that every field of the tab's state was a field of
  the highway's, so the core IS the tab's state. The shipped shape: `ChartViewState`
  (`chart/chart_view_state.h` — `NoteViewState`, `KeyframeViewState`, `BendPointViewState`,
  `ShapeViewState`, `ShapeStringViewState`, `FhpViewState`, plus `display_hold_ends`, capo, and the
  open strings) produced once by `makeChartViewState` (`chart/chart_projection.cpp`); the lane
  renders it directly, and `HighwayViewState` composes it as `chart` beside sections, tap onsets,
  chord groups, beats, and camera zones. `tab/` left `common/core` with it. Two consequences the
  fold forced, each a correctness condition of a shared core rather than an extra: (1) the highway
  no longer bakes display padding into note strings — the scene carries chart strings on both
  surfaces and each renderer maps them to lanes per frame through `displayedStringCount` /
  `displayedLane`, the same two functions; (2) the placement ramps moved into the shared producer
  (`FhpViewState::ramp_seconds`), because when the hand starts moving is a chart fact, and the one
  margin rule both it and the tap light rise read is `marginBefore` in grid arithmetic. `linked` is
  the read `linkedKeyframe(note, keyframe)`; the shape posture list is one `strings` on both
  surfaces; the per-note agreement test became "the highway composes the chart projection
  unchanged". The review's item 9 rode along, and RULED 2026-09-17 it is TWO claims, not one:
  WHERE a note sounds (`soundingStopAt` — the head text, the 3D placement and the node base) and
  WHETHER it is a harmonic (`isHarmonic` — the 2D diamond head and the 3D harmonic cell). They part
  company at the pinch, whose node is over the body: it sounds at its fretted stop, so it prints
  its FRET, and it is a harmonic all the same, so it wears the diamond with the pinch bar in front.
  3D is unchanged — a pinch already wore the harmonic cell. The pattern catalog's View-state
  push entry carries the element rule: types inside a view state are view state, named `*ViewState`,
  never `*View` (noting the float-member `std::is_eq` nuance against a defaulted `==`). An
  `INoteView` interface was considered and killed: it keeps the second producer, insures against
  divergence the surfaces law forbids, and costs vtables or double instantiation on the render path;
  if a genuine per-surface field ever appears, compose extras beside the shared core at that point.
- [x] **W9-C — What does an arpeggio bracket's `sounded` mean? RULED: it was never a musical claim,
  and the flag is deleted.** The brackets always draw — they state the POSTURE, the fretting hand's
  placement, which is their whole job — heads render in the normal note pass, and the flag gated
  exactly one thing: whether the bracket wrote its own fret text. Draw-dedup, misnamed, and it
  deduplicated non-duplicates: a span-start TAP suppressed the posture's fret while its head stated
  a different one, so the hand-placement information appeared nowhere. `sounded` is gone, the
  cross-reference scan against the notes at the span start is gone with it (a shape projection now
  asks the note list nothing), and the digit's COLUMN is the projection's choice —
  `ShapeStringViewState::digit` is published per posture string, and the painter draws where the
  projection put it. **The display question is SETTLED** in
  `docs/plans/in-progress/arpeggio-posture-display-options.md`. A satellite digit outboard of the
  closing bracket bar was tried and rejected in live use on two objections no harness measurement
  could catch, because neither is a contrast problem: the digits are unreadable at the real lane
  size, and a number *outside* the brackets "just straight up reads weird" — enclosure is a grouping
  cue, the brackets ARE the posture mark, so a number outside them reads as detached from what it
  names. Only the digit CENTRED in the brackets reads acceptably. The measured leader is a centred
  digit that slides in TIME past a head occupying the span start, suppressing only true duplicates.
  What survives unconditionally is the deletion of `sounded` and the principle that the posture
  states. **The ribbon problem and its answer (RULED and shipped).** A bare digit does not survive a
  sustain ribbon, which crosses the slot from BOTH directions — a previous note holding into the
  span start, and a posture string struck and held at it (the ordinary arpeggio picture: six ribbons
  through six digits at once). Measured: 15.6 dL* / 1.65:1 on the yellow string's own ribbon and
  17.3 / 1.74:1 on the green, five of six strings under WCAG's 3:1 floor, and on yellow the ribbon's
  bright edge is BRIGHTER than the ink, inverting the polarity inside one ribbon. Ruled: a **1 px
  casing** — the near-black the heads already back themselves with, stroked behind the letterforms —
  which restores the full 67.3 dL* of the clean lane. The casing grows into the gap the digit
  already keeps from the bar, so the mark costs exactly the clearance it cost before. **Why a casing
  and not a plate or a chip, and it is the general rule:** a backing with its own ground would have
  to pick a polarity, and polarity is how this lane names a HAND — dark is the picking hand's. A
  casing in the lane's own colour is not a ground at all, it is the lane showing through, so it
  cannot be read as either hand and it adds no silhouette for the attack-plate or label-chip
  vocabularies to collide with. A white fretting-hand plate was examined seriously and lost on
  geometry rather than semantics: a rim-bearing plate cannot be shorter than 10.25 px while a
  ribbon's interior is 9.92 px, so **no plate fits inside a sustain at either lane size**, and six
  of them would carry 3.82x the near-white pixels of the six heads they annotate. The LEFT slot
  (semantically better — the hand prepares the posture before the notes arrive) was reopened once
  the background dissolved its ribbon objection, and lost on the tap case it exists to serve: 2.3 px
  of clearance to the T plate against the right slot's 22.5, which no backing improves and every
  backing worsens. **The simpler alternative was reconsidered on its merits and rejected — recorded
  so it is not re-litigated.** The alternative: keep the digit dead centre and let a real attack on
  a different fret simply overrule it. In its favour, one argument originally made against it was
  WRONG and is retracted: suppression does not leave the posture column holey, because a head draws
  its own number, so a digit appears on every posture string either way. The loss is narrower — it
  is exactly the case where the two frets DIFFER, which in practice means a right-hand tap (a
  fretting-hand note at a fret other than the template's would mean a stale template, a data problem
  rather than a notation one). Two things decided it against. First, the saving is smaller than it
  looks: one of the three conditions that MAKE a span an arpeggio is a posture string still ringing
  through the start un-struck, and in that case no head exists, so the centred digit draws over the
  incoming ribbon and needs the casing anyway (white measures 3.34:1 on the yellow fill and 1.66:1
  on its bright edge). Only the satellite slot, the posture font and the widened gap would be saved.
  Second, and decisive: **taps commonly have a left-hand shape held under them**, and the arrival
  rule's own documentation agrees — it names "a held chord under two-hand tapping" as one of the
  conditions that create an arpeggio bracket (`chart_rules.h`). So the yield would drop the fretting
  hand's posting precisely in a figure the notation was written to produce, on the one string where
  it is hardest to infer. **The hand-partition law:** posture notation — arpeggio brackets,
  shape-span chord boxes, FHP — is FRETTING-HAND-ONLY. The tapping hand gets its own parallel
  family: tap positions (3D's orange tap-position floor numbers), and simultaneous taps derive a
  RIGHT-HAND CHORD BOX (W12). There is NO right-hand arpeggio notation, by law.
- [x] **W9-D — Does a glide end state its fret when a landing exists? RULED: 2D states a pitched
  glide's arrival fret always** — no cross-note lookup, no proximity test, no validation rule. The
  defect it fixed: a pitched glide arriving at the sustain end drew neither a continuation head nor
  a chip, so its arrival fret was stated nowhere in 2D while 3D drew it. Confirmed in real imported
  data (`Periphery - It's Only Smiles` measure 20 beat 4 carries two GP shift slides, string 4 fret
  8→4 and string 3 fret 6→2, where the destination IS re-picked). **Two premises of the original
  writeup were wrong; corrected here so they cannot mislead again.** (1) Validation does not make
  the documented condition impossible. It forbids a fret-stating keyframe sitting *on* a later
  sounding onset of its own string, but a shift-slide glide legitimately ends the minimum sustain
  distance *before* its landing — the landing is real, just later. That kills deriving "linked" from
  a nearby re-picked note, which would be a proximity heuristic guessing intent the format does not
  store. (2) *Pitched-target vs falls-away* and *has-a-landing vs not* are INDEPENDENT axes, and the
  payload types already encode the first. A pitched glide that genuinely lands with nothing picked
  after is a real technique — 3D renders it correctly by straightening the tail where an unpitched
  exit dims away — so making that state illegal was withdrawn, and `slide_out`'s own doc now says it
  plainly: never a sounded landing, and a glide INTO a note is fret-stating keyframe data whose note
  renders its own head. **Open, and deliberately entangled with W9-F:** which glyph states it. The
  direction is the keyframe's own head rather than the unpitched chip — sized to the TAIL's height
  so it reads as part of the tail rather than as an event, which moves the strike/no-strike
  distinction onto the size channel instead of fill darkness alone (`headShapeFor`'s
  plectrum-at-turnarounds behaviour must survive it). Being linked is already a shared READ,
  `linkedKeyframe(note, keyframe)` — the keyframe's time against the note's presented end — rather
  than a stored per-surface flag, and the view state mirrors the domain (keyframes plus an optional
  terminal) rather than flattening the slide-out in among them. 3D needs the counterpart glyph or
  the no-surface-divergence law is broken.
- [x] **W9-E — Where does the attack mark go on a muted head? RULED: neither option — the OVERLAP is
  settled by order, and the order differs per surface because the geometry does.** The rule: *"the
  dead note (mute X) should draw ON TOP of legato ... And honestly it should draw on top of just
  about any other technique too"*, then, on sighting: *"The X over the legato definitely looks
  BETTER although it still definitely conflicts. Both techniques NEED to be supported together
  though ... Supporting it in a slightly ugly way is better than no support at all."*

  The shared law is that whichever mark cannot afford to be CUT draws last, and it resolves
  differently on each surface. On the 3D head every mark is pushed at the same centre and the same
  family size, so marks overlap by construction and order only picks the survivor: the X wins,
  because a broken X reads as a different mark. In the 2D lane the satellite sits in its own slot up
  and left of the head and meets the X at one arm's tip, so the satellite draws last — covering the
  small mark entirely costs the reader more than clipping the end of a long stroke whose identity is
  already legible. Carrying the 3D order into 2D was tried and reverted on sight.

  The 3D marker order is not hand-written per branch. `highwayHeadMarks` (`highway_head_marks.h`) is
  the one ordered authority both the open-string overlay and the fretted head ask, derived from how
  much of the note's identity each mark overrides: palm mute, hand mark, connection, harmonic, dead
  X — with a pick slide a category of one that returns alone, because the chart rules prove it
  carries nothing else. The residual dead-plus-legato conflict is accepted and recorded in
  `docs/tracking/watch-items.md`; a pair-by-pair re-check of the whole ladder against the note
  compatibility matrix is a separate task.

  **The residual is SEQUENCED, not dropped:** *"This type of conflict is hard to avoid and we MAY
  need a better solution later. But we may want to queue it up behind our work on mipmaps to make
  things read better at a distance first so we can REALLY do a clear evaluation."* Marks are sampled
  with no mip chain today, so a fix chosen now would be tuned against a sampling artifact. Plan 56
  (`docs/plans/roadmap/56-head-atlas-mipmapping.md`) states the same constraint from its own side —
  mipmapping and mark distance-legibility work "should be judged together", since mips alone make
  every annotated note relatively less conspicuous at the horizon — so the two belong in one pass.
- [ ] **W9-F — Should 2D distinguish an unpitched slide?** Every slide diagonal is stroked plain
  white whatever the stop's `unpitched` says, while the highway dims an unpitched run to a quarter
  alpha. So a note that glides 5 to 7 and then trails off shows two identical diagonals in 2D and
  two visibly different ones in 3D. D17 already named the lever — "the unpitched diagonal's own
  treatment, broken rather than solid". Options: pull it now, or record the divergence deliberately.
- [ ] **W9-G — Does a mute restate at each slide junction?** A linked junction head draws the full
  layered head with a plain white number and neither the X nor the mute plate the onset head gets,
  so a muted slide's continuation asserts a pitched landing. Partly mitigated by the linked fill
  reading darker. Options: the mute restates at every junction, or it is a once-at-the-onset
  property. **The candidate frame, presented and unruled:** the mute restates at every junction of
  an UNSPLIT gesture as derived display (a head stating a fret must state the sound's character or
  it lies), with a change of character authored by the split verb. Its "inheritance" premise depends
  on what a junction point IS, which the bend study decides — rule after that study, with the frame
  kept as the candidate.
- [x] **W9-H — Is the scrape toggle a true ON/OFF? RULED: YES.** *"pressing Shift+X a second time
  should restore exactly what the note had before. Once you change selection or move the caret then
  the change is committed and the toggle behavior goes away. We did this with legato and other
  techniques. This should function the same for consistency."* Built SHARED rather than copied: the
  reversal proof is `reverseChartVerbWindow`, which every windowed verb calls, and the commit points
  call the one disarm instead of naming a member. The per-verb window fields collapsed to ONE
  `m_chart_verb_window` keyed by `ChartVerbWindowVerb`, and the eight toggle methods to one
  `onChartTechniqueToggleRequested` driven by `chartTechniqueLaw` (`chart_edits.h`), so a verb
  joining the family is one enumerator and one law row — and a verb LEAVING it is the same delta
  backwards, which is exactly what the fret-hand harmonic did on 2026-09-16 when it stopped being a
  toggle: `ChartTechnique::Harmonic` went with it, and `Shift+H`'s pinch is now the family's one
  harmonic row. The scrape arms on the entering AND the
  clearing press, because reversal restores what its own clear law cannot: the sustain the default
  grew on a note that had none, and the glide a conversion consumed into the terminal.

  The problem it answers: toggling a zero-sustain note into a pick slide grows its sustain to the
  minimum gesture window (correctly — a path needs room to travel), and toggling back clears the
  path but leaves the grown sustain, so the note would end carrying a tail it never had.
- [x] **W9-I — Which chord does the scrape toggle get? RULED: `Shift+X`.** Both natural first
  letters are plate letters (`P` pop, `S` slap) and a plate letter outranks a name letter, so the
  scrape joins the `X` family: `X` is tab's dead-note glyph and the full mute's letter, and a full
  mute and a scrape are both unpitched NOISE. Full reasoning in `keymap-matrix.md`. It had shipped
  registered with no default chord, reachable only from the lane's right-click menu, because the
  signed keymap never assigned one; `Ctrl+H` was taken by the left-hand tap.
- [x] **W9-J — Is a scrape's start a stop or travel? RULED: the whole gesture is floored.** *"Even
  unpitched slides should probably require ending at 1 or 1 fret above the capo."* So every fret a
  slide gesture names — a scrape's start, its turnarounds, and every slide-out's exit, a pitched
  note's trail-off included — sits at or above `firstPlayableFret(capo)`; the pick travels the
  sounding string, and a scrape at the nut is no scrape. The start/travel tension dissolves because
  start and path are judged alike. The validator lost the scrape exemptions and gained the slide-out
  exit floor, and the default scrape path floors its terminal through the same low-endpoint
  authority the direction chooser uses (`pickSlideDefaultLowFret`) — under a high capo it used to
  synthesize a terminal the rule refuses.
- [x] **W9-K — Is a negative bend amount legal? RULED: NO — bend amounts are ≥ 0 and the validator
  refuses a negative**, both on the note's own `bend` and on every keyframe's bend channel
  (`chart_rules.cpp`). Dips and dives belong to the whammy's own model field
  (`docs/plans/todo/whammy-bar-support.md`); a finger cannot lower pitch, so a negative bend amount
  is a data error, not a technique — refused loudly beside the existing offset checks. No drawing
  question survives: nothing legal renders below the tail, and deep dives arrive with the whammy's
  twist notation. The defect it closed: nothing validated a bend's AMOUNT, and a negative one
  rendered a chip with no number at all (the formatter's fraction lookup clamps to an empty string),
  so the mark said a bend existed and refused to say how much.
- [x] **W9-L — The scope of the no-naming rule.** It covers the real-guitar game only. The
  plastic-guitar franchise whose installments the scoring plan names as its feel baseline does NOT
  need to be avoided, and the named baseline stands everywhere it carries information.

## W10 — The tie/slide-link verb (`Shift+L`) and the split-tail law

> **AMENDMENT 2026-09-12 — user-signed. The verb is BUILT, whole, as "Split or Join at Selection"
> (`planToggleJunctions`, `ChartJunctionToggle` `0x1713`, the id value kept across the rename).**
> The tie/slide-link half is built as the SPLIT'S EXACT INVERSE under one immediate undo entry: at
> every selected junction the press moves it to its other state — a selected keyframe becomes a
> head, a selected head becomes a point on its same-string predecessor's path — and both halves run
> in one press. Split-then-join restores the chart byte for byte, because the arrival the split
> retreated off the new head returns to the junction through the same clearance authority run
> backward.
>
> **The tie needed nothing built.** Joining an equal-fret head leaves a point that says nothing the
> path does not already say, so the commit law sheds it from the history entry and the writer sheds
> it from the document: what is recorded is one longer ring with one note fewer, and no tie datum
> exists anywhere. Different frets leave an ordinary slide keyframe at the junction — the connecting
> slide this section asked for.
>
> **SUPERSEDED, and NOT built:** the pending-intent / ghosted-head / settle-on-selection-change
> mechanism below, and the `LegatoMotion::Continuation` amendment with it. A joined head's per-note
> techniques go WITH the head — attack, mutes, node, tremolo, emphasis, held stop are facts about a
> strike, and the join is the statement that no strike happens there — so there is no
> technique-changed junction for a `Continuation` claim to describe. A junction where something
> changes stays a head for one reason only: the charter did not press the verb there. The
> struck/unstruck boundary, ruling 1a's de-justified-continuation settle and the ghosting affordance
> are all consequences of that unbuilt mechanism and stand superseded with it; the split head still
> stores the plain `Legato` claim W10 signed, whose unstruck-tie reading remains a PROPOSAL.
>
> **Still open:** the importer's own tie-destination merge (`gp_chart_builder.cpp`) hand-writes the
> merge `planToggleJunctions` now owns, and W10 ruled import and editor one law — tracked in
> `docs/tracking/backlog.md`.

`Shift+L` is the `L` verb extended with travel, recorded alongside the technique-letter map in
`keymap-matrix.md` (legato `H`→`L`, left tap `Ctrl+H`→`Shift+T`, `H` freed for the harmonics). GP's
own `Shift+L` ("tie the beat") is subsumed by the uniform-scope law, so the slot is vacated by our
design, not stolen.

**Semantics by junction:**

- **Different frets → author the slide.** Grow/shape the predecessor's tail to the junction and link
  it — origin-side geometry authored from the destination-side press, the D14 assist's own
  precedent. Applies AT PRESS (a generated slide must draw as real geometry, never as fiction);
  within the selection window a second press reverses exactly via the W7 mechanism (`{keys,
  history_position}`, applied-plan inverse, grown tails included — the window generalizes into a
  shared authority, not a second copy). Beyond any window, `Shift+L` on an already-linked junction
  CLEARS the link — apply-or-clear parity with `L` — which IS the slide break: W6's separate break
  verb dissolves into this toggle and its binding question closes with it.
- **Equal frets, no technique change → the tie, and the tie never enters the format.** The settled
  truth is one longer sustain. The press commits NOTHING: a pending intent held for the selection
  window, the junction head drawn GHOSTED through the shared paint primitive (the Alt-ghost idiom —
  here ghost means about-to-vanish; the data still holds both notes, so nothing fictitious is drawn
  and the "provisional renderer" costs one style substitution). A second press DISCARDS the intent —
  no reversal machinery at all. Selection change settles: delete the arriving note, grow the
  predecessor's sustain over it, one undo entry. The guard (equal fret, equal technique set at the
  junction) is exactly what makes the merge lossless — removing the strike is the verb's point, and
  the guard ensures the strike is the only thing removed. The two halves deliberately stage
  differently: an addition previews as reality because its reversal machinery already exists; a
  removal previews as a ghost because deferring it costs nothing (the data still holds the head) and
  makes the discard trivial.
- **Refusals:** a gesture-carrying predecessor (scrape, slide-out) refuses the tie — its tail is
  authored geometry, the D14 assist's own rule. Refusal feedback rides W3's non-modal channel like
  every other refusal.

**The split-tail law — the inverse gesture, and it is general.** A settled tie leaves no trace, so
re-splitting cannot be tie-specific; the law is: **a note head exists exactly where something
changes (fret or technique)**. A technique verb pressed with the armed caret on a tail point creates
a head there — the predecessor's tail shortens to the split, the new note carries the remainder and
the technique. Technique verbs thereby gain the digits' own three-rung ladder: apply to the
selection, else split-the-tail at the armed caret, else inert.

**The disconnect joins the verb's scope.** `Shift+L` with a selected KEYFRAME disconnects that
keyframe from its note ("to make it feel consistent" — the split-tail law applied at the keyframe
instead of a bare tail point): the note's path ends there and a new head takes the remainder. The
orchestrator's proposed default — the split product is an unstruck tie, so the sound is unchanged
and a second press can make it struck — is PROPOSED, not ruled.

**BUILT: the keyframe clause** (2026-08-26, then spelled `planDisconnectKeyframes` /
`ChartKeyframeDisconnect` `0x1713`; the join half and the rename landed 2026-09-12 — see the
amendment at the top of this section). Three things the build settled or exposed:

- **The unstruck-tie default is still a proposal, and the code says so.** The split head stores the
  plain `Legato` claim W10 signed; under today's resolver an equal-fret claim is Unjustified, so the
  settle sweep flattens it to a pick and the product reads as STRUCK until `LegatoMotion` gains
  `Continuation` (it holds `Unjustified`, `Hammer`, `Pull` today). Nothing was written as if the
  default were ruled.
- **The arrival cannot sit where the split does, and that is the format's own law.** A fret-stating
  keyframe may never sit on a later sounding onset of its own string, so the origin's arrival
  retreats by the minimum-sustain-distance margin — the shift-slide shape the importer's policy rule
  13 already synthesizes for exactly this figure, and where the presentation trim ends the drawn
  tail anyway. Without it the verb could never produce a legal chart at all. The margin is a
  consequence of the signed format rule rather than a new ruling, but it MOVES an authored instant,
  so it wants a sighting.
- **A junction with no room for that retreat is refused, never clamped** (one within a margin of the
  onset or of the statement before it).

**RULED — the split head's attack:**

- **The split head stores plain `Legato`; `LegatoMotion` gains `Continuation`.** No new stored value
  (`Pick` killed for authoring a strike that is not in the music; a stored `Tie` killed on
  derived-over-authored — struck-ness is fully derivable, so storing it would let two facts
  disagree). The resolver's equal-fret arm is amended: equal released fret resolves to
  `Continuation` (no strike), **justified iff the junction changes technique** — the exact dual of
  the head-exists law. Physics closes the derivation: on equal frets no fret-hand strike is possible
  (a same-fret re-strike is the left-hand tap, already `Shift+T`'s stated attack), and on unequal
  frets an unstruck connection requires travel, which is slide geometry — so frets-plus-geometry
  always answer "was it struck."
- **Struck/unstruck verb boundary:** plain `L` authors and clears STRIKE-motion claims only — it
  never authors a continuation and its clear never destroys one, the LeftTap precedent enforced by
  the derived motion instead of a stored value (the planner filters on what the resolver derives;
  still one authority). `Shift+L` is the sole author of unstruck connections: the tie merge when
  nothing changes at the junction, the `Continuation` claim when a technique does, and slide
  geometry for travel. Severing via `Shift+L`'s toggle reverts the head to `Pick`, which is then
  true.
- **1a — a de-justified continuation settles by MERGE, not a Pick flatten.** Editing away the
  junction's technique difference leaves an equal-fret claim nothing justifies; a Pick flatten would
  invent a strike, so for equal-fret claims the sweep's sound-preserving flatten is the tie merge
  itself (one authority with `Shift+L`'s settle). Direction claims keep their Pick flatten.
- **1b — W8's junk-hopo-flag landing is untouched:** an equal-fret hopo FLAG is author junk, not a
  tie (GP's tie is the explicit no-restrike notation, which the importer already merges), so it
  keeps landing as a counted Pick conversion.
- **Import evidence that motivated the ruling:** the importer already implements tie-as-merge
  (`gp_chart_builder.cpp`'s tie-destination arm; positional payloads keep their junction per policy
  rule 15). It once SMEARED boolean techniques across the merge — the tie junction's own vibrato
  start was destroyed on import — and the keyframe channels are the fix: `stateVibratoAt` states the
  merged value at the junction's offset rather than OR-ing it onto the whole note. Under this ruling
  the tie merge gains the same guard as `Shift+L`'s settle: merge when nothing changes, keep a
  `Continuation` head when something does. Import and editor become one law.
- **Slides are outside the attack model entirely:** an unpicked slide chain is ONE note whose travel
  is keyframes — no second note exists to carry an attack; a re-picked landing is an ordinary `Pick`
  note.
- **Staging:** the no-change tie stores nothing — pending intent + ghost head through the window,
  merge at selection-change settle. The `Continuation` claim exists only where the head survives.

**RULED — slide tails: digits state the path, technique verbs split it:**

- **Technique verbs split only at stated frets.** Legal at a keyframe (the split un-merges exactly
  one link of the chain — the precise inverse of the importer's linked-note merge; the new head's
  claim resolves `Continuation` naturally, the handed-over keyframe fret equalling the new head's)
  and on the post-travel hold segment (a plain tail split, the fret being the last keyframe's).
  Refused strictly between keyframes — the fret there is interpolated travel, and a head must sit on
  a stated fret. Snapping to the nearest keyframe was killed as a clamp; rounding the interpolated
  fret as invented data.
- **Digits on a slide note's tail author the path**: a digit is a fret statement, and on a travel
  path "the hand is at fret N here" has one honest meaning — caret between keyframes creates a
  keyframe, caret on a keyframe retypes it. Plain-note tails keep insert-with-truncation (40-Q2-B);
  the region rule is by note kind, not by segment. This converts W6's lock refusal into the useful
  meaning (an insert-truncate mid-travel would have been refused anyway), and it gives the
  technique-verb refusal a composable escape hatch: state a keyframe, then split at it — everything
  stated, nothing guessed. Keyframe fret validity rides the normal fret-entry validation under W3's
  pending model (provisional in the window, red when invalid; direction reversals are representable
  — scrape turnarounds prove it). *(Which KEY does this was re-ruled 2026-09-09 and finally
  2026-09-11 — the entry-grammar note further down: the by-note-kind split is gone, and a DIGIT on
  any covered slot states the point, bare or under `Alt` alike. "Plain-note tails keep
  insert-with-truncation" above is superseded with it — truncation survives only for load, import
  and the MOVE verb, and dividing a ring is the two keystrokes "digit, then `Shift+L`", which is
  exactly the "state a keyframe, then split at it" escape hatch this bullet already wanted.)*

**RULED — the tie's ghost is editor-2D-only:** no editor-authoring chrome displays in 3D — the ghost
heads exist strictly to help authoring, and the 3D view is specifically for reading, not authoring.
Same footing as the light-T charting mark, the FHP chips, and W6's lock indication; at settle both
surfaces show the merged tail identically, so nothing diverges.

**All three rulings are closed — W10 is fully specified.** Sequencing note, not a gate: the refusal
*feedback* and the typed-keyframe entry ride W3's channel and pending model, so those surfaces land
with W3; the verb itself can build silent-at-parity first, like the shipped technique verbs.

**Addendum — the fret-verb law, keyframe gestures, and path scope:**

- **The deliberate-placement principle, and the law it yields.** Every keyframe was placed on its
  fret on purpose, so **a fret verb edits exactly the selected objects' own frets — no path ever
  rides, in either mode.** `planRetypeFrets` carries the rule in code, with an explicit warning not
  to restore the scrape special case that translated the path with the start; it was ruled a bug and
  died whole. A typed or transposed start stilled against its adjacent path position refuses via
  always-traveling — a **scrape-only** rule: a pitched slide legally holds (the repeated-fret
  hold-then-glide encoding the importer emits), so retyping a pitched 5→7 slide's start to 7 is a
  legitimate correction, not data loss. The stilled-scrape refusal rides the existing
  always-traveling rule, pinned with the pitched equal-fret hold acceptance.
- **THE SLIDE-OUT IS THE KEYFRAME AT THE RING'S END (user-signed 2026-09-09, effort excluded
  from the judgement: "whatever is definitively better").** `ChartNote::slide_out` is deleted; a
  fret stated exactly where the sound stops is a fret the hand never sounds, so it is the release —
  unpitched by position, read through `releaseKeyframe` / `slideOutFretOrNull` (`chart.h`), never
  stored as a kind. What the field had pinned structurally is now ONE rule in the one resize
  authority (`clipPayloadsToSustain(note, sustain)`): a ring shortened under its release carries
  the release with the end. RULED 2026-09-10 (user, after re-opening it as a toss-up): **a point
  never moves because the ring did, and the chip is the fall's own handle.** The duration verb
  moves the RIBBON — lengthening a released ring leaves the release where it was, a pitched stop
  with the tail running on as a regular slide; shrinking a ring that simply ends exactly onto its
  last stated fret makes that fret the release (the floor is inclusive there); shrinking a released
  ring holds at the release, since the ribbon cannot pass its own end point. The MOVE verb
  (`Alt+←/→`) on the release drags the ring's end with it — the slide-out lengthens or shortens —
  refused onto the last sounded fret (a fall needs its own leg) and parking on the next same-string
  onset as any ring does. Chosen over "the release rides on extend" because the charter reads the
  chip as a point they can grab, and every other point stays put under the tail verb. A release
  STATES ITS FRET AND NOTHING ELSE (user, 2026-09-10): a bend or shake on the point the ring was
  pulled onto has no ring to sound in, so it goes with that ring (`stripReleaseChannels`, one
  spelling, asked by the writer, the resize, the move step and the load repair).
  Consequences signed: a pitched arrival exactly at the end is
  no longer a distinct state (arrive-and-stop is written as the importer writes every arrival, one
  margin inside the end); the release may park on the onset that silences the string; the capo
  floor lifts a release rather than stripping it; saved projects carrying `slideOut` re-import. What it buys: the falls-away chip is a selection citizen
  through the keyframe machinery with no new kind — click, ring, digit retype, Delete, the commit
  law — and an `Alt`+digit at a bare tail END authors it, so the FALL verb (`F`) is unnecessary. The
  projection carries `KeyframeViewState::release` read off the STORED ring, because the drawn end
  can also be a shift slide's trimmed arrival. UNSIGHTED.
- **Keyframe creation needs no new gesture.** *(Which KEY carries it was re-ruled again, finally on
  2026-09-11 — see the entry-grammar note at the end of this bullet; what a planted point IS did
  not change.)*
  **RE-RULED 2026-09-09 at the P2 sighting (user):
  `Insert` on ANY ringing tail plants a REAL keyframe** at the previous path point's fret — the
  last fret STATED at or before the caret's offset, the note's own where nothing states one earlier,
  never the interpolated travel — selected, with the caret still on its slot; no ghost, no window.
  The by-note-kind split is gone: a plain note's tail plants a point too. *(A note INSIDE a tail was
  a gesture of its own here and is not one any more — see the 2026-09-11 note below: no gesture
  lands a head on a ringing note at all.)*
  **THE COMMIT LAW: a silent point is AUTHORING STATE (user, 2026-09-10):** a
  point that says nothing — no bend, no shake, a fret the path passes through anyway
  (`keyframeSaysNothingNew`, `chart.h`) — is never document and never history. The undo history
  records WRITTEN states (`writtenChartPlan`): planting one pushes no entry, and the edit that gives
  it a meaning — the landing typed — diffs from the written state before it and so carries both
  points in one entry. It dissolves when its NOTE leaves focus (`dissolveSilentKeyframes`, the
  settle's second half), with no entry and nothing on the history stack able to defer it, and it
  collapses before undo or redo replays. The document writer and the load repair both shed it
  (`documentChart`, `ChartRepair::SilentKeyframe`). The editor keeps no record of who planted
  what: the chart is the only state. (Superseded: a settle-time dissolve folded into history, with
  a record of touched points carried across settles and re-armed by undo — built 2026-09-10 and
  replaced the same day; its dissolve deferred behind an unrelated entry on the note's stack, which
  is what the no-entry model cannot do.) **The DIGIT half
  is LIVE too:** a digit at a caret a ring covers states a point through the pending entry's third
  beginning (`ChartFretEntry::CreateKeyframe`) — the box at the slot, red where the gate refuses
  the fret — and lands planted and selected exactly as `Insert`'s does, so a typed fret the path
  passes through is a silent point like any other (user, 2026-09-09: never a keystroke no-op).
  Every refusal is the rule authority's
  through the finalize gate — offset zero, past the ring, onto an existing point, a path a
  fret-hand harmonic or an open string may not carry, the capo floor, a scrape a repeated position
  would still — so the planner carries none of them.
  **RE-RULED FINALLY 2026-09-11 (user): every note is TYPED, a click never creates, and `Alt`
  creates only the slide-out.** The keystroke that states a point is the DIGIT — bare or under
  `Alt`, which land the same point on a covered slot — and the fret-less keys that used to state one
  are retired with the rest of the `Insert` family. On an empty slot and at a ring's exact END the
  digit is a head instead, the next note, and `Alt`+digit at that end is the slide-out: the one cell
  where the two chords differ, and the only thing `Alt` creates here. No press authors at all, under
  any modifier. Everything above about what a stated point IS — the previous path point's fret
  supplying a fret-less restatement, the planted-and-selected landing, the commit law, the refusal
  list — is unchanged; only which key carries it moved. **Dividing a ring is now two keystrokes**:
  the digit plants the point, `Shift+L` splits it, and `planToggleJunctions` is that verb's
  alone again — no entry gesture splits, so nothing single-press truncates a ring or clips a
  keyframe, and the clamp and the clearance repair are the load, import and MOVE authorities. A
  point that merely restates the running fret is silent authoring state, so typing the same fret on
  a tail and stopping there leaves nothing behind; a fret-stating point in an open string's tail is
  refused (`OpenStringSlide`) and shows the red box. *Superseded records of the earlier builds: a
  bare digit on a slide tail taking the note flow's insert-with-truncation; then a bare gesture on a
  covered slot striking a head and truncating the ring; then a bare gesture there splitting the ring
  losslessly, with `Alt`+click and `Alt`+double-click as the pointer forms.*
- **The keyframe-commit law (closes the junk state).** A pending keyframe COMMITS at settle only if
  it changes the path function — a fret change, or a hold boundary that alters when travel resumes —
  and otherwise dissolves back into plain tail, exactly like an unjustified pending entry. One
  oracle question (the path with it versus without it), the keyframe half of the head-exists law:
  the all-equal junk path is unrepresentable by construction, because no gesture can commit a
  keyframe that states nothing.
  **Live, and the oracle is exact.** The path is the stops that STATE it — the onset at offset zero,
  each fret-stating keyframe, the terminal at the ring's end — linear between them and holding past
  the last, so "the path already passes through this point" is exact collinearity, cross-multiplied
  rather than evaluated into a rational fret no statement could equal. A ghost nobody retyped
  therefore dissolves wherever the path holds and commits where it was travelling, which is exactly
  the hold boundary this ruling names. On a scrape the two authorities split the case cleanly: a
  point on the travel line dissolves under this law, while one repeating a neighbour's position
  refuses through the always-traveling rule.
- **A selected keyframe retypes like a head**, transpose scopes to exactly the selected points, and
  string moves are allowed whenever the head is in the selection (the path rides by construction; a
  keyframe-only selection refuses). **Live.** Digits and `Alt+Shift+↑/↓` both route through
  `planRetypeFrets`, which takes the selection's two key lists and transposes off ONE anchor — the
  lowest stop the operand addresses, heads and points together, which is exactly what a chord slide
  needs. The multi-digit pending window is the note flow's, unchanged: the entry gained a keyframe
  operand, not a second entry kind. `ChartStopChannel` gained NO third value — a keyframe has one
  position channel and no satellite, so the SELECTION KIND is the discriminator, and a third
  enumerator would have made the channel and the kind two authorities for one question. A keyframe
  stating no fret is not addressed: it says nothing about position, and nothing draws it to point
  at. The refusals are the rule authority's as ever — the capo floor, the fret cap, a stated fret
  under a slide-out — so the planner carries no fret bound of its own.

## Ruled: import dispositions

- [x] **R1 — Semi-harmonics import as pinch.** A semi-harmonic is "basically a pinch harmonic where
  you didn't really FULLY execute the pinch" — the fundamental rings through. The squeal gesture is
  the technique's identity, and the pinch is the honest nearest representation until the format
  distinguishes them. **Implemented** with its own conversion note; revisit as a distinct technique
  only when a UI need appears.
- [x] **R2 — Feedback harmonics stay unsupported.** Feedback needs a real amp in the room —
  headphone play cannot produce it, and its pitch behavior is amp/room-dependent. Import drops the
  harmonic loudly, note survives. **Implemented** (unknown types land in the same diagnostic).
- [x] **R3 — Tap harmonic excludes tremolo (E23).** Not executable fast enough. The model agrees for
  a structural reason: a tap harmonic's damping finger *leaves* the string, so nothing holds the
  node under re-picking and the harmonic dies — while a natural or artificial harmonic keeps a
  finger on the node, which is why those two still allow tremolo (A.H. tremolo is "oddly actually
  possible").
- [x] **R4 — Legato + tremolo stay allowed.** Hammer or pull the onset, then tremolo the sustain. No
  change needed.

## Decisions D1–D18

- [x] **D1 — The fret floor and the capo convention — ADOPTED and shipped.** `fret` is absolute with
  0 meaning the open string, capo'd or not; capo'd naturals import as `fret = 0`; `fretFor` keys its
  node branch on `fret == 0` (fixing the artificial-harmonic hand placement); E21 generalized to the
  physical stop (the capo when `fret == 0`); E22 enforced; E7/E9/E19 re-keyed on "no real stop" in
  the matrix doc; E4's capo caveat dissolved (`fret > 0` already means a real stop). Two
  refinements: E22 (and `fretFor`) exclude the `Tap` attack — an open-string tap harmonic's node
  belongs to the picking hand, so only the universal node bound applies to it — while E7/E9/E19
  *include* `Tap` (nothing pressed is nothing pressed). A 2D display note for the notation pass, no
  action now: a fretted tap harmonic's head shows the node, and its pressed stop reaches 2D only as
  its CLAIM — the standing satellite beside the bracket, read-only there (2026-09-17, the ruling
  that puts a harmonic's pressed stop in `fret` for either hand) — while an artificial harmonic's
  press reaches it only through a covering span's own bracket digit. A two-position technique may
  eventually want both on the head itself. 3D already carries both, which
  settles what "both" should look like: the note (head, tail, glide) sounds from the node, while the
  board's own furniture — glow post and fret-span line — marks the stop the hand presses. That
  division is stated once, in `highwayNoteFretboardX`, which takes the stop as a parameter so every
  point of a gesture reads the same axis: a node RIDES its stop (fret spacing is logarithmic, so the
  offset above the stop is constant in fret units), the same rule `tabNoteHeadText` labels each head
  by. Before that, an artificial harmonic's glide left the node axis and landed on the raw fret slot
  of its keyframe, so its tail traveled to a place 2D never labeled.
- [x] **D2 — The scrape's payload shape — ADOPTED and shipped flat.** `slide_out` is the required
  unpitched terminal, `keyframes` are optional turnaround stops, the whole path always traveling —
  implemented across the rules, writer, projection, defaults, the sustain-trim planners, the retype
  transposition, the importer's carrier conversion and crowding trim, and the slide-out exit
  resolver (which skips scrapes: their terminal is authored travel, not an exit to resolve). The old
  scrape-terminal carve-out in the keyframe-on-onset rule became structural and was deleted. Within
  a scrape the keyframes were never pitched anyway (fret data is right-hand travel), so the previous
  shape was not *wrong* — but this one is more honest about the terminal. Two clarifications from
  the same exchange, which also demoted the scrape variant (see the matrix doc's hardening item 2):
  the variant was never "scrape stops being a note" — outer position/string/fret/sustain stay
  shared, so min-distance, overlap normalization, sorting and selection remain
  single-implementation, and the cost is dispatch breadth across technique-field consumers rather
  than duplicated relational logic; and D2 + D4 shrink the variant's structural payoff to five
  excluded fields plus the required terminal, which enforced rules already cover, so the lean is
  that the variant will not be worth it.
- [x] **D3 — Pick slide + the tremolo flag — CLOSED: exclusion stands, name stays `tremolo`, "noise"
  is texture vocabulary.** The discussion's arc, kept because each step sharpened the model:
  - The field means *unmeasured noise texture* (measured repetition is always spelled out as
    separate heads), and a scrape has that property — so "why not REQ?" was legitimate.
  - REQ lost on information content: a flag another field forces to true stores one fact twice and
    manufactures a new invalid state (`PickSlide` + flag false) — the rejected-tap-enum shape; E20's
    required node differs because the node carries *independent* information. A uniform "is this
    note noise?" read is a derived accessor, not authored duplication.
  - A rename to `noise_picking` lost to the reframe that beat it: untimed picking, as fast as
    physically possible, IS tremolo picking, so the technique name is honest — and plain `noise`
    would misdescribe the field, because a tremolo-picked note is **pitched** noise (the fret still
    sets a measurable pitch) where a scrape is **unpitched** noise.
  - **The settled taxonomy: two noise textures, distinguished by pitch, one per axis** — pitched
    noise on the `tremolo` flag, unpitched noise on the `PickSlide` attack — which is why they never
    share a field. Recorded in the field's own doc comment. Display: today's marks are
    tremolo-specific slashes and keep their literal names; "noise" is the name for a tail style only
    if one is ever genuinely shared between the two textures.
- [x] **D4 — Accent on a scrape — ADOPTED and shipped** with D2: E2 dropped emphasis from its
  exclusions, the writer and the projection stopped suppressing it on scrapes, and H3 closed as
  "emphasis is compatible with **everything**," no exception. The tight glow clearance (0.331 px at
  a 25 px head against the disc's 1.560) is accepted as-is; `glow_size` is the joint retune knob if
  it ever needs air.
- [x] **D5 — Muted legato — ADOPTED, recorded as E24.** A dead note allows the hammer motion, the
  pull motion and `Tap`: muted legato clucks are standard funk and R&B vocabulary, dead-note taps
  core percussive-fingerstyle material. E4's positive-sounding-position rule still binds the
  hammered and tapped forms. Tweakable later if it feels wrong. **Terminology: NOT "ghost"** — ghost
  is the emphasis axis's soft tier (D8); the muted-legato family says "muted".
- [x] **D6 — Dead note + pre-bend — KEPT FORBIDDEN**, pre-bend included in E10's bend exclusion: the
  data stores a pitch offset a dead note lacks (incoherent, not merely pointless), and no notation
  source writes the gesture. Reopens only on real chart evidence.
- [x] **D7 — E5: derivation vs validity — CLOSED, simpler than every draft.** Muted legato is
  *common* vocabulary (funk/R&B, bass especially), so plain `L` **derives across dead predecessors
  normally** — requiring a modifier for the common case would surprise exactly the charts that use
  it most; that half stands, bounded by the hold test alone. **The scrape half is E27:** a scrape's
  released fret is the pick's position, not a finger's, so a scrape predecessor justifies nothing,
  and the derivation-vs-validity gap closed by making the two agree. The final shape:
  - **Validity (E5):** a pull needs a same-string predecessor whose released fret is higher.
    Fret-hand-harmonic predecessors are disqualified (E19) and so are scrapes (E27).
  - **Derivation (`L`):** infers across ordinary, muted, and tapped predecessors alike, uniformly at
    every selection size — which rejected the single-note exception and the three-press cycle
    (uniform-scope law, and the toggle contract that a second press undoes the first).
  - **No `Shift+L` for the marginal case.** A keybind for one marginal case is unwarranted until
    proven needed, and the case needs no affordance anyway: **it is reachable by edit order** —
    author the pull, then scrape the predecessor, and the value-based settle re-tests what stays
    justified. Folded into `legato-authoring-model.md` (Layer 1 rows, the scrape-predecessor note,
    the invalidating-edits row) and the matrix doc's E5 row.
- [x] **D8 — The emphasis axis — ADOPTED** as the three-value shape: `NoteEmphasis { Normal, Ghost,
  Accent }` replacing the accent bool, `Normal` never serialized, ghost+accent unrepresentable by
  construction (the enum IS the hardening). `Soft` dropped (GP has one quiet tier; detection argues
  against a second), `Heavy` deferred — GP heavy accents import as regular accents for now, with a
  comment that Heavy may be supported later. Full design in
  `docs/plans/completed/note-emphasis-axis.md`.
- [x] **D9 — The GP capo frame — ANSWERED: capo-relative, and shipped.** With a capo at 3, an
  entered "1" resolves to the pitch at absolute fret 4, corroborated by a real capo'd tab.
  Consequences: the importer shifts fretted notes by the capo (relative F > 0 → absolute F + capo; 0
  stays the open string per D1); the harmonic stop reads the shifted note fret (the same
  physical-stop formula E21 validates); the natural-label formula (`capo + snapped offset`) was
  already exactly right — the labels ARE capo-relative, which is why the capo-1 score's 7.0/8.2 read
  as standard open-string-family values. The capo-2 fixture's expectations all moved by exactly +2,
  each verified as the intended shift. GP cannot express an absolute sub-capo fret, so imports never
  produce one; sub-capo validation ships with the editor verb guards (D12), inheriting the template
  and fret-hand-position analogs — a posture or hand window below the capo is equally meaningless.
- [x] **D10 — The legato workflow's five calls — ALL RULED.** (1) No recalculating chrome; the
  window is settle-event-scoped with NO timer. (2) Delete clears everything — which deleted a
  special case, since the ordinary settle-on-selection-change rule already covers it. (3)
  Released-fret semantics adopted: the ring's state at its own end (`releasedFret`). (4) **Option C
  accepted, the notation split rejected**: plain `L` infers only fret-justified directions
  (equal/absent predecessor refuses), `Shift+T` is the sole author of the left-hand tap across its
  matrix-verified domain, including overriding a derived Pull. (5) Legato settles at IMPORT so
  charts are never born invalid. Three further rulings recorded with them:
  - **The 2D connector is REJECTED.** It would further diverge the 2D view from what the player sees
    in 3D, and rejecting the data half strengthens that — the surfaces should teach the same
    reading. The rules are enforced regardless, so drawing the connection simplifies nothing. The
    floating triangle stays; the tap-vs-hammer display disambiguation is NOT bought
    (author-is-authority). **Consequence to design for:** the tail lock has no visual affordance, so
    its refusal feedback becomes load-bearing rather than nice-to-have.
  - **Tail LOCKING replaces repair-on-shrink for explicit sustain edits, family-wide.** When a
    note's tail is load-bearing for a dependent transition — a legato successor that needs the
    connection, or its own slide payload — the explicit duration verb REFUSES to shrink it rather
    than silently repairing the transition away (refuse-never-clamp; "no code that lies"). Stated
    uniformly: *a sustain edit may not shrink a tail below what a dependent transition requires* —
    per-kind requirement (legato: the connection point; slide: the last keyframe), no stored lock
    state, all derived. This also fixes an existing silent data loss: shrinking a slide's tail clips
    its keyframes. **Legato and slides get ONE shared mechanism** — Phase 7 inherits it rather than
    inventing its own. The lock binds the EXPLICIT verb; implicit 40-Q2-B truncation (placing a note
    in front of a tail) keeps truncate-and-repair, since refusing a placement would be worse.
  - **Junk hopo flags are cleaned at import** (W8): a Guitar Pro hopo destination with no fret
    motion imports as a plain `Pick` with a conversion note. An open-to-open hopo flag is junk data,
    and the derivation refuses exactly this case, so import must not create what the verb would
    refuse.
  - **Locked-tail feedback is required, not optional, and lands WITH the lock** — "pretty important"
    once the connector was rejected. Recorded as roadmap **40-Q5** with the constraint that the
    indicator must sit on the PREVIOUS NOTE'S NOTEHEAD, because a hammer-on's origin may show no
    tail at all, so the tail is not a place to put it.
- [x] **D12 — Enforcement pass — SHIPPED.** It consumed D1–D9: E4–E19 + E23/E24 guards and rules
  (E2/E20/E21/E22 already enforced; D4's E2 emphasis change shipped), the pinch-verb node obligation
  and attack-away-from-pinch node clearing, the two rule-violating test fixtures (tab-paint
  dead+pinch vs E8; GP fixture natural+bend vs E9), and the **sub-capo family as one unit**:
  validation (note frets 1..capo invalid when capo > 0) paired with the editor verb guards in the
  same change — never validation alone, or verbs could author charts that cannot re-load — plus the
  template, fret-hand-position, and pitched-glide-keyframe analogs. The shape that shipped: rules
  live once in `validateChartNotes` (`chart_rules.cpp`); every planner funnels through the
  `finalizePlan` gate, which validates the SAVED form (`savedChartNote`, the one memory-vs-document
  seam, also the document writer's source); import sheds harmonic-impossible techniques loudly
  (conversion note) and the FHP generators floor every window, slide-in approach, and gesture dip at
  `firstPlayableFret`.
- [x] **D13 — The release-inference refinement of legato.** The sustain conventions make "the
  predecessor released early" *provable* beyond a bound, refining "still ringing is unknowable" to
  "unknowable only at close range":
  - **The two data facts.** (1) The import drop rule sheds the tail of any effect-free note whose
    ring does not run longer than the kept-sustain bound — the shared
    `g_minimum_kept_sustain_seconds` (`grid_arithmetic.h`), a duration read through the tempo map,
    so the drop rule and the inference can never disagree. (2) A held tail is trimmed/clamped to end
    exactly the minimum-sustain-distance margin (`g_minimum_sustain_distance_whole_note`,
    meter-scaled) before the next same-string onset, so "ring end reaches (next onset − margin)" IS
    the maximum representable hold.
  - **The inference.** Onset gap strictly under the bound: tails may have been legitimately dropped,
    so derivability stays fret-only. Gap at or past the bound: a held-through predecessor
    necessarily carries a ring reaching the margin, so a ring ending short of it proves the string
    was released, and no hammer-on or pull-off from that predecessor is real. Editor-inserted notes
    default to zero sustain, so hand-authoring legato across such a gap means dragging the
    predecessor's tail first — consistent with the display convention, where a tail-less note does
    not read as held either.
  - **One shared predicate.** `predecessorHoldReaches` (`grid_arithmetic.h`) is asked by the
    resolver, which is in turn the sole authority for the surfaces, the gameplay build, the reader,
    and the `L` planner. It reads the note's STORED ring directly — the span-implied display hold is
    not a rule input (W1).
- [x] **D14 + D15 — CLOSED by the legato ruling.**
  - **D14's assist: ADOPTED and SHIPPED** with all three corrections intact — pre-checked against
    the growth clamp and the note skipped whole rather than partially extended, the selection pinned
    (`select_exactly` = original keys) so the grown predecessor does not join it, and one addition
    the ruling made: the assist skips gesture-carrying predecessors (a scrape, or any slide-out),
    whose tail is authored geometry rather than slack. The third correction — "geometry reversal is
    Ctrl+Z, no window-scoped restore" — was **overturned**: the toggle window restores grown tails
    too, and the "remote note memory" objection dissolves because the window reverses the ENTRY
    rather than remembering values (W7). Provably order-independent on groups; the assist fires on
    ~1% of real pairs.
  - **D15: REJECTED, and re-signed by the final spec.** The connected display died on the
    no-surface-divergence rule (D10 ruling 1); the stored-connection data half died on the corpus
    arithmetic below; forward-addressed `L` died on eight concrete kills. The one claim of D15's
    that the new model *did* adopt in a different form: the enum split it called unworkable "without
    stored connection" is exactly what shipped — because the alias zone it feared is answered by the
    resolver reporting the hammer motion for `LeftTap` unconditionally while plain `L` is forbidden
    from producing one, not by stored connection.
  - **The corpus arithmetic that killed the data half** (102 scores, 4,317 legato transitions):
    73.8% of real legato pairs are a sixteenth apart or closer, where the spacing margin makes the
    maximum representable connection tail exactly ZERO — the rule would be vacuous yet undrawable
    for most legato; above that it converts routine sustain edits into silent legato loss for 26.2%
    of pairs (against 1.0% under D13); and connection-as-normalized-data would hold-score every fast
    run (60–125 ms held fractions detection cannot resolve) or demand scoring carve-outs. D13 as
    shipped is the right and sufficient data rule, its strict zone biting on 1.0% of corpus pairs.
  - **The eight kills against forward-addressed `L`**, three of them against shipped mechanisms: the
    selection-follow rule enlarges the verb's own scope each press and breaks the armed-caret
    invariant; the any-string growth clamp turns unreachable extensions into mutating no-ops labeled
    "Legato"; the multi-digit fret window settles on the hot path so two-digit frets die on any
    legato predecessor; plus left-tap fabrication, chain-gutting toggle, and span-splitting on
    chords. Its real GP-muscle-memory benefit is outweighed.
  - **The seven "found en route" items are all resolved:** (a) the out-of-selection repair enlarging
    the selection — gone with the repair engine; (b) `L` inert on mixed selections — fixed by the
    oracle law (W5); (c) two-digit fret entry unreachable on a legato predecessor — dissolved
    outright, since no repair fires inside a burst any more; (d) silent refusals — W5's counted
    feedback ships as data; (e) the span-blind hold rule — dissolved by the stored-ring hold test
    (W1); (f) import yielding a tap claim from junk hopo flags — the completion sweep converts it
    (W8); (g) doc staleness — closed out.
- [x] **D16 — What a fully muted note's tail means.** The question: does the all-muted carve-out
  reverse E24 (muted legato), and has the design around muted tails "been fully thought through"?
  - **Diagnosed root: a DISPLAY rule had been imported into a PHYSICAL rule.** The display hold
    rule's all-muted exemption exists because *a dead chug does not ring* — a sounding statement.
    The hold test asks a different question: *is the finger still down?* Two different questions
    were being answered with one function.
  - **The wider question, and the SIGNED RULE SET.** Two observations settle it: a muted note CAN be
    tremolo picked (so a noise tail is valid with or without a slide), but merely holding a dead
    note makes no sound (so a plain muted tail is silence pretending to be sound). Three rules:
    1. **E25: a dead note draws a sustain tail only when something keeps making noise or
       travelling** — `tremolo`, or a slide payload. This is the whole rule; the other two rest on
       it. It is a PRESENTATION rule, so the stored ring is untouched and only the drawn tail goes
       (W4).
    2. **A muted tail always draws in the NOISE idiom**, unconditionally: the only muted tails that
       CAN be drawn are noise or travel, so the display needs no "is this a slide" branch. The
       tremolo band and a slide diagonal already coexist on one tail.
    3. **Muted legato is bounded by the hold test like every other note, with ZERO new code.** A
       distance exception for dead notes was rejected: there is no principled distance. A muted note
       with tremolo keeps a long ring and can therefore justify legato across any gap — the chug is
       the evidence that the hand stayed, which is exactly what the hold test asks for.
  - **The sounding-vs-fretted hold split that was recommended earlier is RETRACTED.** With E25 in
    place, one hold concept serves both readers again, so the split would add a concept to solve a
    problem that no longer exists. Do not build it.
  - **Consequences built with it:** the slide-authoring verb grows a muted note's tail as part of
    authoring the slide, the way the scrape verb extends a zero sustain for its gesture (otherwise
    tail-and-slide are chicken-and-egg); `palm_mute` is untouched, since palm-muted notes ring; and
    the highway's held bar on a muted note means "keep chugging or dragging", whose hold-scoring
    treatment is a plan 24 question, not a chart one.
- [x] **D17 — The teeth mean REPEATED ATTACKS, not noise.** A pick slide draws as a regular smooth
  slide, since its motion is one clean drag and it can never be tremolo picked — and a dead note
  then gains a real distinction: "noisy travel" (a tremolo-picked muted slide, jagged) versus a
  plain muted slide (smooth, one drag).
  - **Why this reading is right.** (1) It is factually accurate about the gesture: a scrape is ONE
    continuous drag, and teeth assert a repetition that is not happening. (2) It stops the tail
    duplicating the head: a scrape already says "noise" three ways at the head (the plectrum
    silhouette, the PS chip, the required terminal) and a dead note says it with the X, so spending
    the only jagged texture on restating that leaves nothing for repetition-versus-continuous. (3)
    It buys a distinction otherwise UNREPRESENTABLE — noisy travel — which matters exactly because
    E25 makes muted tails either travel or repetition. (4) The tail axis then means one thing per
    decoration: ribbon = duration, diagonal = position travel, curve = bend, sine = vibrato, **teeth
    = repeated attacks** — and pitched-versus-noise lives wholly at the head, which is where the
    attack is.
  - **The change was a deletion, both surfaces together:** the `|| scrape` disjunct is gone from the
    2D lane's teeth test and from the highway's `teethed`, which now read `note.tremolo` alone, with
    the same comment on each. No data, format, or import change.
  - **Cost, measured rather than assumed.** At the editor's true default zoom (316 px/s — the
    test-suite geometry of 20 px/s is 15.8x zoomed out and flatters the change), with keyframe times
    matched, a scrape tail is **byte-identical** to an ordinary pitched slide chain, so the tail
    carries no scrape identity at all. What carries it is the head band: of 3024 differing pixels,
    1191 sit in the head and the strip above the line — the plectrum silhouette (508), the `PS`
    plate (135), and the boxed unpitched fret chips (396), against a pitched slide's round linked
    heads ON the line. The silhouette ALONE (24 px wide against the disc's 26, same height) is not
    enough; the plate is what makes it plain at 1x. The real cost is salience, not identification:
    the identity is ~12% of the note's ink packed into the first 40 px of a 632 px object, so where
    the old serration was a full-length signal the new look is a point signal. **If scannability
    ever complains, the lever is the unpitched diagonal's own treatment** (broken rather than solid)
    — the one tail element a scrape does not share in kind with the pitched case — NOT a return to
    the teeth, which would resume asserting a repetition that never happens.
- [x] **D18 — A scrape's junctions carry continuation heads.** With the teeth gone, a multi-leg
  scrape read as disconnected diagonals: each turnaround was a bare kink in a white line although
  the pick never leaves the string. Junction heads restore it, carrying the pick shape.
  - **The fix was a deletion, not an addition.** A scrape's turnarounds had been marked unlinked,
    but "linked" means "the same note continues through here", which is simply TRUE for a scrape —
    being unpitched had been conflated with not continuing. The condition is the time test alone
    (`linkedKeyframe`), and the painter already knew what to do with a linked keyframe.
  - **Three consequences, all falling out rather than being built:** the junction draws in the
    note's OWN head shape via `headShapeFor` (so a scrape junction is a plectrum); it carries the
    traveled fret at the shared `g_plectrum_digit_raise`, which sits beside the head-shape helpers
    so the onset and junction digits cannot end up at different heights; and a junction that carries
    a head no longer ALSO gets the floating chip (the same number twice), which also relieves the
    three-chip collision at wide zoom.
  - **The terminal deliberately keeps its chip and gets no head:** that is where the pick leaves the
    string, and nothing lands there — the same reading a pitched trail-off already has.
  - **No 3D change:** the highway sweeps its ribbon continuously, so it has no junction
    discontinuity to answer, and being linked is read by the tab painter alone. Per-surface idiom
    for one fact, not divergence.

## Recorded, no decision needed

- **D11 — The pick-side tap.** Rare technique: a tap performed with the side of the pick, itself
  slidable — distinct from the pick slide in many aspects. May need its own representation later;
  recorded here and in the matrix doc so nobody force-fits it into `PickSlide` or `Tap` when it
  surfaces.
