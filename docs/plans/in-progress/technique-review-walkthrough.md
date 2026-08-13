# Technique Review Walkthrough — the decision queue

Status: **ACTIVE — combing through with the user, one decision at a time.** Opened 2026-08-08 at
the user's direction after the Fable review produced more open items than a single reply could
carry: *"perhaps we should break this out into a list of tasks to comb through methodically"* and
*"keep your todo list clearly recorded somewhere it is recoverable."* This file is that record —
it survives any session, and each item closes here first, then flows into
`technique-compatibility-and-hardening.md` (the matrix authority), `legato-authoring-model.md`,
or code.

Working order: top to bottom unless the user redirects. Each open item carries the agent's
recommendation so the user can rule with full context in front of them.

> **Legato note (2026-08-11).** The closed items below record what shipped *at the time they
> closed*, and the legato machinery they name has since been replaced by
> `legato-final-spec.md`: no direction is stored (`Hammer`/`Pull` became the single `Legato` claim
> plus `LeftTap`), `normalizeChartLegato` and the whole repair engine are deleted, the relational
> rules are resolver clauses instead of validation rows, and the repair-in-the-finalize step is now
> a settle sweep at commit points. The *physics* each item settled (D13's release inference,
> released-fret semantics, the assist, the toggle window) is unchanged and still binding — only the
> mechanism moved. The records are left as written so the decision history stays auditable.

## Work queue (LIVE — the session task list mirrored here so a disconnect loses nothing)

Approved order, user-signed 2026-08-09. Keep this list and the session task list in step; when an
item ships, mark it and name the commit.

- [x] **W1 — Span-blind hold rule** (defect in D13's implementation). SHIPPED `4f1e793c`:
  `chartEffectiveSustains` resolves span-extended held lengths; three call sites pass shapes.
  **Its all-muted carve-out is CORRECT and stays** — an earlier note here called it a display
  rule leaking into validation and proposed splitting sounding-hold from fretted-hold; D16's
  signed E25 retracts that, because once a plain muted note cannot carry a tail at all, "a dead
  chug is not held" is a physical statement and not merely a display one. One hold concept, both
  readers. Do not implement the split.
- [x] **W2 — Two-digit fret entry on a legato predecessor** (defect in D12's settle hack).
  SHIPPED `d0b1d32b`: the entry carries its applied plan and the widen reverses it to
  reconstruct pre-entry exactly; the selection-follow rule no longer adopts repaired notes.
  **Superseded if W3 is adopted** (the pending model deletes the splice machinery entirely).
- [ ] **W3 — Pending fret entry with invalid-red feedback (user proposal 2026-08-09; APPROVED to
  build, ground-up design done).** Nothing commits mid-entry: the typed value is provisional,
  drawn on the head(s), RED when it cannot be applied, committed as one undo entry when the
  window settles and discarded (previous value preserved) when it is invalid. Deletes W2's
  reversal machinery, the history splice, and all mid-entry chart mutation.
  - **CORRECTED digit split — my refinement had an arithmetic error.** `g_max_fret` is **30**
    (`chart_rules.h:34`), so `digit * 10 > g_max_fret` is FALSE for 3 (30 > 30) — committing 3
    immediately would make **fret 30 untypable**, a refuse-never-clamp violation dressed as a UX
    tweak. The predicate is *"could a second digit reach a value this digit alone cannot?"* —
    `value >= 1 && value * 10 <= g_max_fret` — giving **provisional {1,2,3}, immediate
    {0,4..9}**. `0` joins the immediate set deliberately: today it arms the window for a
    leading-zero path nobody types, making the open string (the commonest value on the
    instrument) wait out the window. Keep the predicate general so a raised cap needs no rework,
    and note that at cap 30 a second digit always ends the entry, so a third digit is currently
    unreachable.
  - **Prerequisite (also fixes a standing blind spot): `finalizePlan` conflates "refused" with
    "nothing changed"** — both return `nullopt` (the technique gate's own `nullopt` inside
    `finalizePlan`, then `diffNotes`' empty-diff `nullopt`, both in `chart_edits.cpp`; cited by
    name rather than by line, which has drifted twice). The pending model cannot paint a valid
    no-op red, so the seven
    planners move to `std::expected<ChartNotesEditPlan, ChartPlanRefusal>` with
    `{NoChange, Invalid}`. This is why **every refusal in the editor is silent today** — no
    caller can tell one from the other. **Correction 2026-08-11:** W5's counted feedback did NOT
    need this channel and no longer waits on it — `planSetLegato` returns its own typed skip report
    beside the plan. The two are different questions: this one distinguishes a refused plan from an
    empty one, W5's names which notes the resolver turned down and why.
  - **An ENTRY BOX for the whole pending state, red text inside it for invalid (user ruling
    2026-08-09, better than the agent's invalid-only chip).** The box is drawn for as long as the
    value is provisional — valid or not — and disappears when the entry settles, reusing the plate
    the mute heads already draw permanently. Three things fall out that the invalid-only chip did
    not give: the provisional state itself becomes visible (my chip left a VALID pending value
    looking exactly like a committed one, which is a lie about whether the value landed); the box
    supplies a known background, so red text is readable over any string-lane colour and the
    red-on-a-red-lane hazard is structurally gone; and red versus white text inside it differ in
    LUMINANCE as well as hue (~0.21 against 1.0), so the valid/invalid signal survives protan and
    deutan vision without a second shape. **One conflict to resolve in the build:** a muted head
    already draws a plate permanently, so the pending box must differ from it — give the box the
    editor accent as a border (pending is an editor state and accent is the editor's active
    colour), which also keeps shape-plus-colour redundancy on muted heads.
  - **Provisional drawing is editor chrome, not the shared paint core** (the core's contract is
    that both products produce identical notation pixels, and the game has no keyboard entry) —
    but export ONE primitive, `paintTabNoteHead` with a text/colour substitution, so the digit's
    typography and placement cannot drift from the committed head the way the insert ghost
    already has.
  - **The window rides the existing `IMessageThreadScheduler::callAfterDelay` port** with
    `safeCallback`, and the injected clock is the authority so a stale or duplicated wake is a
    no-op. Trap for the implementer: `ImmediateMessageThreadScheduler` runs the work
    SYNCHRONOUSLY and every controller test gets it, so the harness needs a deferring scheduler
    plus a settable clock (no test injects `now_milliseconds` today, which makes the Services
    doc claim about it currently false).
  - ~~**`EditorUndoHistory::replaceTop` dies entirely** — its only production caller is the widen.~~
    **NO LONGER TRUE (2026-08-11): it has a second production caller**, the legato settle sweep,
    which folds its flatten into the burst's own chart-notes entry. `replaceTop` therefore survives
    W3; only the *widen's* use of it goes away with the pending model. W7's own need was a different
    verb (`dropTop()`, which drops the entry rather than rewriting it) and shipped as one.
  - **Undo is NOT special (user ruling 2026-08-09, and it deletes work).** Nothing is committed
    until the entry settles, so undo has nothing of its own to cancel: it is just another action,
    and the uniform prologue — *every action and intent settles the pending entry first, commit
    if valid, discard if invalid* — already covers it. That drops the agent's `undo_available`
    and `undo_label` changes, i.e. two lying-affordance risks, and leaves ONE settle rule instead
    of a rule plus an exception. Consequence to accept: `Ctrl+Z` pressed inside the window on a
    VALID pending value commits it and then undoes it, so the value appears and is removed with a
    redo entry left behind — which is the visible result the user asked for, and honest, because
    the value really was a valid edit.
  - **Esc's rung is claimed by an INVALID pending value only** (user reasoning): Esc cancels the
    *problem*, so an invalid value discards and the caret survives for an immediate retype, while
    a VALID pending value is not a cancellable thing — it falls through to the caret rung and
    commits on the way through the uniform settle. That matches the ladder's own semantics (one
    press takes the topmost APPLICABLE rung) instead of adding an Esc special case, and it means
    a valid typed value is a value you meant.
  - **Chord semantics: red marks EVERY selected head.** `validateChartNotes` returns the first
    violated rule with no note identity, and relational refusals (E5, the hold test) are
    properties of a PAIR, so per-note attribution is both unavailable and often ill-defined.
    "Which note" belongs in W5's feedback channel, not in the colour.
  - **Two latent bugs die with it:** a paused seek does not settle the window today, so "1",
    ruler-click, "2" widens to fret 12 on a dissolved caret; and a mid-window save splits one
    typed number into two undo entries because `replaceTop` refuses when a clean marker sits on
    the top entry.
  - **Ruling 8's objection dissolves.** I argued deferral would either commit invalid data or
    make the digit inert. Neither happens: `finalizePlan` runs in FULL every keystroke (repair
    and gate included) and the digit visibly does something — only the *apply* waits. The
    mid-entry legato flicker the user worried about is then gone by construction rather than
    tuned. Do not preview the repaired neighbour in the overlay, though the plan is in hand:
    that would re-import the flicker as chrome.
- [ ] **W4 — Implement E25's muted-tail rules (D16 below; SIGNED, unbuilt).** The ruling is done;
  what remains is code, in three parts: the validation rule (a `Full`-muted note's sustain requires
  `tremolo` or a slide payload), the import normalization that zeroes such a sustain so a re-read
  cannot refuse the chart, and the noise-idiom tail display. Blocks nothing but touches W1, E10,
  E24 and the display; build before the lock/break work so the tail rules are stated once.
- [ ] **W5 — `H` eligible-subset fix SHIPPED 2026-08-10; counted feedback SHIPPED AS DATA ONLY, its
  surface deferred to W3 (revised 2026-08-11 by the legato review's F1).** The eligible-subset half:
  the controller asks `planSetLegato` itself whether applying would change anything (the oracle, never
  a restated predicate), and only when it would not does the press clear — targeting just the legato
  subset, so a rider `Tap` or `Pinch` keeps its own attack. A second press always undoes the first;
  pinned by the mixed-selection round-trip test. **The counted feedback's payload landed with the
  DERIVED-DIRECTION model** as the verb's own typed return — `ChartLegatoPlan{plan, skipped, reason}`
  with `ChartLegatoSkip` naming the four refusal classes, every class pinned by `test_chart_edits.cpp`.
  **Nothing is shown**: the rendering half was deleted the same day it was reviewed, because the
  view's only reporting seam is a modal `showThemedWarningBox` titled "Could not complete request", so
  it popped a dialog on the commonest press in charting (a phrase's first note) to announce that
  nothing had failed. `H` is silent when it applies nothing, at parity with `Ctrl+H` and the
  pick-slide toggle. So W5 **does** wait on W3's non-modal channel after all — for the surface only;
  the per-note information the planner already had is built and waiting.
- [ ] **W6 — Tail lock + break verb + locked-tail feedback (40-Q5). NARROWED TO SLIDES ONLY
  2026-08-11** by the legato ruling: a connection claim stores no direction, so shrinking its
  predecessor's tail drops the mark live, regrowing restores it inside the burst, and the settle
  sweep flattens what is left as one folded batch — nothing to lock and nothing to break. Slides
  keep all three (waypoints are real data). ~~The break verb frees a tail from the origin's side,
  binding TBD in keymap review~~ — **break verb DISSOLVED INTO W10 (2026-08-12):** `Shift+L`'s
  apply-or-clear toggle severs an existing link, so the break needs no verb or binding of its own;
  W6's remaining scope is the slide tail lock and the 40-Q5 feedback. The feedback is
  **editor-only** (user ruling: not visible in 3D). **Still blocked on the channel
  (corrected 2026-08-11):** W5's counted-skip report is not the precedent it briefly looked like — its
  data ships but its surface does not, because the only reporting seam is a modal error box. The
  locked-tail feedback and W5's count are now two payloads waiting on the same W3 work.
- [x] **W7 — D14 assist + the `H` toggle window. SHIPPED 2026-08-10, RESHIPPED UNDER THE NEW MODEL
  2026-08-11.** Both halves survived the storage change and were retargeted rather than rebuilt: the
  assist now re-asks `resolveLegato` under a grown tail (the same only-blocker test, one authority
  instead of two) and skips gesture-carrying predecessors, and the toggle window keeps its
  `{keys, history_position}` proof but stores only the keys — the burst's plan already lives once in
  `m_chart_notes_top`, which the settle sweep reads too, so the two verbs cannot disagree about what
  the burst did. One ruled addition: when the entry the reversal would drop is the reachable clean
  state, it pushes the exact inverse as a new entry instead of dying, so the grown tail still comes
  back (`2e840872`). The record as it shipped first follows. The assist lives inside
  `planSetLegato`: when the hold test is the only blocker, the plan grows the predecessor's tail
  to the margin point and derives the direction in the same entry, pre-checked against the
  extracted `sustainGrowthLimit` (one authority with the duration verb's clamp — the assist never
  authors what a manual drag could not), groups included, with a blocked note skipped whole. The
  window is ruling 4's mechanism verbatim: `{keys, history_position}` plus the applied plan,
  validity the fret window's own proof, reversal via `applyChartNotesChange` of the plan's
  inverse and the new `EditorUndoHistory::dropTop` (mirrors `replaceTop`'s guards; a mid-window
  save makes the entry the clean state, so the window simply dies rather than compensating).
  Both toggle halves arm it, so H-H restores an authored mix the clear would flatten. ~~Counted
  feedback for skips still rides the refusal channel (W5).~~ — shipped 2026-08-11 as the planner's
  own typed report, no refusal channel needed.
- [x] **W8 — Cleanups found by the design agents. CLOSED 2026-08-11.** ~~Import junk-hopo flags to
  `Pick` with a conversion note (ruled)~~ **done** — the importer runs the shared settle sweep at
  build completion, so an equal- or absent-predecessor hopo flag lands as a plain pick with a counted
  conversion note, through the same code every load path uses; ~~doc staleness in `file-formats.md`
  (retired `harmonic`/`touch` keys, missing `pinch` token)~~ **done 2026-08-10** — the chart-note
  table now documents `harmonicNode`, the `pinch` token, the `slideOut` terminal, and the capo
  convention, and carries the `legato`/`leftTap` tokens since 2026-08-11; ~~and the recalc-window
  settle-list wording~~ **dissolved** — the window was cancelled unbuilt, so it has no settle list
  left to word (the sweep's settle set is stated once, in `legato-authoring-model.md`).
- [ ] **W9 — Rulings the deep review needs.** Twelve questions, in the section below; **two are now
  closed** (W9-L reverted 2026-08-10, W9-A ruled and shipped 2026-08-11), leaving ten. Nothing else
  from that review is waiting: the rest was fixed in place on 2026-08-10.
- [ ] **W10 — The tie/slide-link verb (`Shift+L`) and the split-tail law (opened AND fully ruled
  2026-08-12 — ready to build).** Design and the three signed rulings in the W10 section below:
  the split head's attack (stored `Legato`, derived `Continuation` motion, struck/unstruck verb
  boundary, merge-settle for de-justified continuations), slide tails (digits state the path —
  waypoint authoring; technique verbs split only at stated frets), and the tie ghost
  (editor-2D-only). Absorbs W6's break verb. Refusal feedback + typed-waypoint entry surfaces land
  with W3; the verb can build silent-at-parity first. The technique-letter amendment that opened
  it (legato `H`→`L`, left tap `Ctrl+H`→`Shift+T`, `H` freed for harmonics — SHIPPED 2026-08-12 in
  the registry and its locked test) is recorded in `keymap-matrix.md`.

## W9 — Rulings the deep review needs (opened 2026-08-10)

A multi-pass review of everything the technique work touched produced these, and only these, as
questions the user has to answer. Everything else it found was fixed in place; the fixes are in the
commit log for 2026-08-10. Each item states the defect, why it needs a ruling rather than a fix, and
the options with the agent's recommendation.

- [x] **W9-A — Is a span-held strum notation, or a 3D affordance? RULED 2026-08-11: notation, fixed
  by option (a), and SHIPPED with the legato model.** The 2D lane now carries `display_hold_ends`
  resolved from the same `chartEffectiveSustains` authority the board resolves
  `HighwayViewState::display_hold_ends` from — one shared derivation feeding both view states, so no
  third implementation appeared. Each surface still spends the value in its own idiom (a 2D ribbon
  against a pinned 3D head), which is lawful. **One residual, recorded as a watch item rather than
  closed here:** the board additionally clamps its pin with `HighwayChordGroupView::hold_cap_seconds`
  and 2D has no cap, so a span covering two strums ends the drawn hold earlier in 3D. The original
  question and its options follow.
  The rule that a sustainless note
  in a two-or-more onset group under a hand-shape span is held for the whole span is implemented for
  the highway (`chartEffectiveSustains`, resolved into `HighwayViewState::display_hold_ends` — it was
  a separate seconds-space `highwayDisplayHoldEnds` when this was written, and the two copies were
  merged 2026-08-10) and NOT for the 2D lane, which draws bare heads with
  zero-width tails for the same chart. The walkthrough calls it "the chart convention", so the
  omission looks like a gap rather than a choice — but fixing it the obvious way makes a THIRD
  implementation of one rule. Options: (a) one shared display helper both view states feed from
  (recommended — it is the only option that does not add a copy); (b) the tab projection gains its
  own derivation; (c) drop the 3D extension and let the span rail carry the hold on both surfaces.
- [ ] **W9-B — Should the two projections become one?** `TabNoteView` and `HighwayNoteView` are
  field-for-field identical; the bend views are identical; the slide views differ by exactly one
  field (`linked`, which D18 already ruled is a per-surface READ of one fact rather than per-surface
  data). The two projection functions differ only by that field and by where display string padding
  is resolved. This is the root cause of several smaller findings: because the view types are
  distinct, no shared helper can read a projected note, so every derivation over one gets copied for
  the other or omitted. Folding them would delete roughly 120 lines and make the surfaces agree by
  construction, at the cost of a mechanical rename sweep through both renderers. **Needs a ruling
  because of its size, not its direction.**
- [ ] **W9-C — What does an arpeggio bracket's `sounded` mean?** 2D marks a posture string
  `sounded` when ANY note sounds there at the span start, a two-hand tap included — so on a held
  chord under a tap it brackets the TAP's fret and never states the posture's, while 3D brackets the
  posture's. `rightHandOnset`'s own contract says those onsets "never anchor, cover, or ring into a
  fretting-hand posture", which argues for the first reading. Options: (a) `sounded` means the
  FRETTING hand struck this posture string, so picking-hand onsets are excluded and the highway
  gains the flag (recommended); (b) it means a head is drawn here whichever hand, and 3D adopts it.
- [ ] **W9-D — Does a glide end state its fret when a landing exists?** `TabSlideView::linked` is
  documented as false when a re-picked note sounds exactly at the waypoint — a condition validation
  makes impossible — and is implemented as `offset < sustain`, which assumes a waypoint at the
  sustain end implies a landing shortly after. A pitched glide ending at the sustain with NO landing
  note therefore draws a diagonal and states its arrival fret nowhere in 2D (3D draws the waypoint's
  post and fret-span line). Options: (a) drop the `unpitched &&` term from the chip guard so every
  unlinked leg states its arrival, which double-states the fret when a landing does exist; (b)
  derive `linked` from an actual re-picked note within the margin, which makes the doc true and costs
  about eight lines.
- [ ] **W9-E — Where does the attack mark go on a muted head?** (Still open, and unchanged by the
  legato model — the mark's *value* now comes from the resolved motion, but its geometry is the same
  triangle in the same slot.) The beside-head mark (the connection
  triangle and friends) sizes its slot from the head's rim and the fret digits, never from the mute
  X, and draws AFTER the X — so on a muted legato note, which E24 explicitly allows as the funk and
  percussive-fingerstyle vocabulary, the mark overpaints most of the X's upper-left arm and the X
  reads as broken. The root flaw is two independent size authorities on one head, so adding a third
  `max()` term is the wrong instinct. Options: (a) the mark moves to a shoulder the X does not use;
  (b) the X yields that arm when a mark is present.
- [ ] **W9-F — Should 2D distinguish an unpitched slide?** Every slide diagonal is stroked plain
  white whatever `unpitched` says, while the highway dims an unpitched run to a quarter alpha. So a
  note that glides 5 to 7 and then trails off shows two identical diagonals in 2D and two visibly
  different ones in 3D. D17 already named the lever — "the unpitched diagonal's own treatment, broken
  rather than solid". Options: pull it now, or record the divergence deliberately.
- [ ] **W9-G — Does a mute restate at each slide junction?** A linked junction head draws the full
  layered head with a plain white number and neither the X nor the mute plate the onset head gets, so
  a muted slide's continuation asserts a pitched landing. Partly mitigated by the linked fill reading
  darker. Options: the mute restates at every junction, or it is a once-at-the-onset property.
- [ ] **W9-H — Is the scrape toggle a true ON/OFF?** Toggling a zero-sustain note into a pick slide
  grows its sustain to the minimum gesture window (correctly — a path needs room to travel), and
  toggling back clears the path but leaves the grown sustain, so the note ends carrying a tail it
  never had. D14 ruling 4 settled the analogous question for the legato verb in favour of a true
  toggle through entry reversal, which W7 needs anyway and which would be shared rather than copied;
  the other signed position is that Ctrl+Z is the revert. Which applies here is the user's call.
  **The mechanism now exists (2026-08-11):** the legato window ships as `{armed keys}` beside the
  burst record `m_chart_notes_top` plus `EditorUndoHistory::dropTop`, with the clean-entry case
  pushing the exact inverse instead of dropping — so adopting it here is wiring rather than design,
  and the scrape verb would arm the same record it already writes.
- [ ] **W9-I — Which chord does the scrape toggle get?** It ships registered with no default chord,
  reachable only from the lane's right-click menu, because the signed keymap never assigned it one.
  `Ctrl+H` is **taken, not merely reserved** (the left-hand tap shipped 2026-08-10 and was
  relabelled "Left-Hand Tap" 2026-08-11), so the scrape needs its own.
- [ ] **W9-J — Is a scrape's start a stop or travel?** A scrape's start fret is floored at `capo + 1`
  like a pressed note, while its waypoints are exempt as unpitched travel — so one gesture has its
  start judged as a stop and its path as travel. Both readings are defensible; the code records the
  tension without resolving it.
- [ ] **W9-K — Is a negative bend amount legal?** Nothing validates a bend's AMOUNT: the rules check
  only offsets and the reader takes the value raw. A negative one renders a chip with no number at
  all (the formatter's fraction lookup clamps to an empty string), so the mark says a bend exists and
  refuses to say how much. A downward bend is a real technique on a vibrato bar, so this is a format
  question before it is a drawing one: does the format admit negative amounts, and if so does 2D draw
  them below the tail?
- [x] **W9-L — Did the naming scrub apply to the right thing? RULED 2026-08-10: no — reverted.**
  The scrub had read the standing no-naming rule as covering the plastic-guitar franchise whose
  installments the scoring plan names as its feel baseline, and removed 54 such references. The
  rule covers a DIFFERENT product — the real-guitar game — and the user ruled explicitly that
  Guitar Hero does not need to be avoided. The scrub commit was reverted the same day, restoring
  the named baseline (Guitar Hero: Warriors of Rock and the era references) everywhere it
  carried information; the no-naming rule remains in force, unchanged, for the real-guitar game
  only.

## W10 — The tie/slide-link verb (`Shift+L`) and the split-tail law (opened 2026-08-12)

User-signed direction 2026-08-12, alongside the technique-letter amendment recorded in
`keymap-matrix.md` (legato `H`→`L`, left tap `Ctrl+H`→`Shift+T`, `H` freed for the harmonics).
`Shift+L` is the `L` verb extended with travel; GP's own `Shift+L` ("tie the beat") is subsumed by
the uniform-scope law, so the slot is vacated by our design, not stolen.

**Semantics by junction:**

- **Different frets → author the slide.** Grow/shape the predecessor's tail to the junction and
  link it — origin-side geometry authored from the destination-side press, the D14 assist's own
  precedent. Applies AT PRESS (a generated slide must draw as real geometry, never as fiction);
  within the selection window a second press reverses exactly via the W7 mechanism
  (`{keys, history_position}`, applied-plan inverse, grown tails included — the window generalizes
  into a shared authority, not a second copy). Beyond any window, `Shift+L` on an already-linked
  junction CLEARS the link — apply-or-clear parity with `L` — which IS the slide break: W6's
  separate break verb dissolves into this toggle and its binding question closes with it.
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
  removal previews as a ghost because deferring it costs nothing (the data still holds the head)
  and makes the discard trivial.
- **Refusals:** a gesture-carrying predecessor (scrape, slide-out) refuses the tie — its tail is
  authored geometry, the D14 assist's own rule. Refusal feedback rides W3's non-modal channel like
  every other refusal.

**The split-tail law — the inverse gesture, and it is general.** A settled tie leaves no trace, so
re-splitting cannot be tie-specific; the law is: **a note head exists exactly where something
changes (fret or technique)**. A technique verb pressed with the armed caret on a tail point
creates a head there — the predecessor's tail shortens to the split, the new note carries the
remainder and the technique. Technique verbs thereby gain the digits' own three-rung ladder: apply
to the selection, else split-the-tail at the armed caret, else inert.

**RULED 2026-08-12 — the split head's attack (user-signed):**

- **The split head stores plain `Legato`; `LegatoMotion` gains `Continuation`.** No new stored
  value (`Pick` killed for authoring a strike that is not in the music; a stored `Tie` killed on
  derived-over-authored — struck-ness is fully derivable, so storing it would let two facts
  disagree). The resolver's equal-fret arm is amended: equal released fret resolves to
  `Continuation` (no strike), **justified iff the junction changes technique** — the exact dual of
  the head-exists law. Physics closes the derivation: on equal frets no fret-hand strike is
  possible (a same-fret re-strike is the left-hand tap, already `Shift+T`'s stated attack), and on
  unequal frets an unstruck connection requires travel, which is slide geometry — so
  frets-plus-geometry always answer "was it struck."
- **Struck/unstruck verb boundary** (replaces the draft's convergence note): plain `L` authors and
  clears STRIKE-motion claims only — it never authors a continuation and its clear never destroys
  one, the LeftTap precedent enforced by the derived motion instead of a stored value (the planner
  filters on what the resolver derives; still one authority). `Shift+L` is the sole author of
  unstruck connections: the tie merge when nothing changes at the junction, the `Continuation`
  claim when a technique does, and slide geometry for travel. Severing via `Shift+L`'s toggle
  reverts the head to `Pick`, which is then true.
- **1a — a de-justified continuation settles by MERGE, not a Pick flatten.** Editing away the
  junction's technique difference leaves an equal-fret claim nothing justifies; a Pick flatten
  would invent a strike, so for equal-fret claims the sweep's sound-preserving flatten is the tie
  merge itself (one authority with `Shift+L`'s settle). Direction claims keep their Pick flatten.
- **1b — W8's junk-hopo-flag landing is untouched:** an equal-fret hopo FLAG is author junk, not a
  tie (GP's tie is the explicit no-restrike notation, which the importer already merges), so it
  keeps landing as a counted Pick conversion.
- **Import evidence that motivated the ruling:** the importer already implements tie-as-merge
  (`gp_chart_builder.cpp` tie_destination arm; positional payloads keep their junction per policy
  rule 15) but SMEARS boolean techniques — `origin.note.vibrato = origin.note.vibrato ||
  source.vibrato` — so "vibrato starts at the tie junction" is currently destroyed on import.
  Under this ruling the tie merge gains the same guard as `Shift+L`'s settle: merge when nothing
  changes, keep a `Continuation` head when something does. Import and editor become one law.
- **Slides confirmed outside the attack model entirely** (`tab_paint_core.cpp:682-686`): an
  unpicked slide chain is ONE note whose travel is waypoints — no second note exists to carry an
  attack; a re-picked landing is an ordinary `Pick` note.
- **Staging re-confirmed at sign-off:** the no-change tie stores nothing — pending intent + ghost
  head through the window, merge at selection-change settle. The `Continuation` claim exists only
  where the head survives.

**RULED 2026-08-12 — slide tails: digits state the path, technique verbs split it (user-signed,
with the waypoint-authoring rule the user added):**

- **Technique verbs split only at stated frets.** Legal at a waypoint (the split un-merges exactly
  one link of the chain — the precise inverse of the importer's Charter-linked-note merge; the new
  head's claim resolves `Continuation` naturally, the handed-over waypoint fret equalling the new
  head's) and on the post-travel hold segment (a plain tail split, the fret being the last
  waypoint's). Refused strictly between waypoints — the fret there is interpolated travel, and a
  head must sit on a stated fret. Snapping to the nearest waypoint was killed as a clamp; rounding
  the interpolated fret as invented data.
- **Digits on a slide note's tail author the path** (user rule): a digit is a fret statement, and
  on a travel path "the hand is at fret N here" has one honest meaning — caret between waypoints
  creates a waypoint, caret on a waypoint retypes it. Plain-note tails keep insert-with-truncation
  (40-Q2-B); the region rule is by note kind, not by segment. This converts W6's lock refusal into
  the useful meaning (an insert-truncate mid-travel would have been refused anyway), and it gives
  the technique-verb refusal a composable escape hatch: state a waypoint, then split at it —
  everything stated, nothing guessed. Waypoint fret validity rides the normal fret-entry
  validation under W3's pending model (provisional in the window, red when invalid; direction
  reversals are representable — scrape turnarounds prove it).

**RULED 2026-08-12 — the tie's ghost is editor-2D-only (user-signed):** no editor-authoring
chrome displays in 3D — the ghost heads exist strictly to help authoring, and the 3D view is
specifically for reading, not authoring. Same footing as the light-T charting mark, the FHP chips,
and W6's lock indication; at settle both surfaces show the merged tail identically, so nothing
diverges.

**All three rulings closed 2026-08-12 — W10 is fully specified and ready to build.** Sequencing
note, not a gate: the refusal *feedback* and the typed-waypoint entry ride W3's channel and pending
model, so those surfaces land with W3; the verb itself can build silent-at-parity first, like the
shipped technique verbs.

## Ruled by the user 2026-08-08 (done or queued to enforcement)

- [x] **R1 — Semi-harmonics import as pinch.** User: a semi-harmonic is "basically a pinch
  harmonic where you didn't really FULLY execute the pinch" — the fundamental rings through.
  Agent concurs: the squeal gesture is the technique's identity, and the pinch is the honest
  nearest representation until the format distinguishes them. **Implemented** with its own
  conversion note; revisit as a distinct technique only when a UI need appears.
- [x] **R2 — Feedback harmonics stay unsupported.** Feedback needs a real amp in the room —
  headphone play cannot produce it, and its pitch behavior is amp/room-dependent. Import drops
  the harmonic loudly, note survives. **Implemented** (unknown types land in the same diagnostic).
- [x] **R3 — Tap harmonic excludes tremolo (new E23).** User: not executable fast enough. The
  model agrees for a structural reason: a tap harmonic's damping finger *leaves* the string, so
  nothing holds the node under re-picking and the harmonic dies — while a natural or artificial
  harmonic keeps a finger on the node, which is why those two still allow tremolo (user: A.H.
  tremolo is "oddly actually possible"). **Recorded in the matrix doc**; enforcement with the
  matrix pass (D12).
- [x] **R4 — Hammer/Pull + tremolo stay allowed.** User confirmed the reading the matrix already
  used: hammer or pull the onset, then tremolo the sustain. No change needed.

## Open decisions

- [x] **D1 — The fret floor and the capo convention — ADOPTED 2026-08-08** (user: "Adopt all of
  it") and **shipped**: `fret` is absolute with 0 meaning the open string, capo'd or not; capo'd
  naturals now import as `fret = 0`; `fretFor` keys its node branch on `fret == 0` (fixing the
  artificial-harmonic hand placement); E21 generalized to the physical stop (the capo when
  `fret == 0`); E22 enforced; E7/E9/E19 re-keyed on "no real stop" in the matrix doc; E4's capo
  caveat dissolved (`fret > 0` already means a real stop). Two refinements recorded while
  implementing: E22 (and `fretFor`) exclude the `Tap` attack — an open-string tap harmonic's node
  belongs to the picking hand, so only the universal 48 bound applies to it — while E7/E9/E19
  *include* `Tap` (nothing pressed is nothing pressed). Sub-capo fret validation (frets 1..capo
  invalid) is part of the convention but **gated on D9**: enforcing it before GP's frame is
  measured could reject valid imports. A 2D display note for the notation pass, no action now:
  a fretted tap harmonic's head currently shows the node, and the stop is carried nowhere on the
  2D surface — a two-position technique may eventually want both. 3D already carries both, which
  settles what "both" should look like: the note (head, tail, glide) sounds from the node, while
  the board's own furniture — glow post and fret-span line — marks the stop the hand presses. That
  division is now stated once, in `noteFretboardX`, which takes the stop as a parameter so every
  point of a gesture reads the same axis: a node RIDES its stop (fret spacing is logarithmic, so
  the offset above the stop is constant in fret units), which is the same rule `tabNoteHeadText`
  labels each head by. Before that, an artificial harmonic's glide left the node axis and landed on
  the raw fret slot of its waypoint, so its tail traveled to a place 2D never labeled.
- [x] **D2 — The scrape's payload shape — ADOPTED 2026-08-08 and shipped flat.** `slide_out` is
  the required unpitched terminal (offset exactly at the sustain), `slides` are optional
  turnaround waypoints, the whole path always traveling — implemented across the rules, writer,
  projections, defaults, the sustain-trim planners, the retype transposition, the importer's
  carrier conversion and crowding trim, and the slide-out exit resolver (which now skips scrapes:
  their terminal is authored travel, not an exit to resolve). The old scrape-terminal carve-out
  in the waypoint-on-onset rule became structural and was deleted. Original analysis retained
  below for the record: User proposes: `slide_out` **required** (a pick slide
  always ends unpitched — a pitched waypoint terminal would imply a turnaround or a held
  landing), `slides` **optional** (turnarounds only). Replaces E2's current "required traveling
  path ending exactly at sustain, slide_out excluded." Agent analysis: within a scrape the
  waypoints were never pitched anyway (fret data is right-hand travel), so the current shape is
  not *wrong* — but the user's shape is more honest about the terminal, and "ends at sustain"
  becomes the slide_out's own offset. **Recommendation (REVISED 2026-08-08 after the user's
  variant challenge): adopt the semantics and implement directly in the flat struct** — one E2
  rewrite plus writer, scrape-path planners, renderer terminal geometry, and tests — with **no
  bundling to the variant**, which is demoted to its own later decision (see the hardening
  section's item 2 in the matrix doc for the full exchange). Key clarifications from that
  exchange: the variant was never "scrape stops being a note" (outer position/string/fret/sustain
  stay shared, so min-distance, overlap normalization, sorting and selection remain
  single-implementation — the cost is dispatch breadth across technique-field consumers, not
  duplicated relational logic); and D2 + D4 shrink the variant's structural payoff to five
  excluded fields plus the required terminal, which enforced rules already cover, so the current
  lean is that the variant will not be worth it.
- [x] **D3 — Pick slide + the tremolo flag — CLOSED 2026-08-08: exclusion stands, name stays
  `tremolo`, "noise" is texture vocabulary.** The discussion's arc, kept because each step
  sharpened the model:
  - The user's first reframe was right: the field means *unmeasured noise texture* (measured
    repetition is always spelled out as separate heads), and a scrape has that property — so
    "why not REQ?" was legitimate.
  - REQ lost on information content: a flag another field forces to true stores one fact twice
    and manufactures a new invalid state (`PickSlide` + flag false) — the rejected-tap-enum
    shape; E20's required node differs because the node carries *independent* information. A
    uniform "is this note noise?" read is a derived accessor, not authored duplication.
  - The agent proposed renaming to `noise_picking`; the user's second reframe beat it: untimed
    as-fast-as-physically-possible picking IS tremolo picking, so the technique name is honest —
    and plain `noise` would misdescribe the field, because a tremolo-picked note is **pitched**
    noise (the fret still sets a measurable pitch) where a scrape is **unpitched** noise.
  - **The settled taxonomy: two noise textures, distinguished by pitch, one per axis** — pitched
    noise on the `tremolo` flag, unpitched noise on the `PickSlide` attack — which is why they
    never share a field. Recorded in the field's own doc comment. Display: today's marks are
    tremolo-specific slashes and keep their literal names; "noise" is the name for a tail style
    only if one is ever genuinely shared between the two textures.
- [x] **D4 — Accent on a scrape — ADOPTED 2026-08-08 and shipped** with D2: E2 dropped `accent`
  from its exclusions, the writer and both projections stopped suppressing it on scrapes, and H3
  closed as "accent is compatible with **everything**," no exception. The tight glow clearance
  (0.331 px at a 25 px head vs the disc's 1.560) is accepted as-is; `glow_size` is the joint
  retune knob if it ever needs air.
- [x] **D5 — Muted legato — ADOPTED 2026-08-09 ("your logic is reasonable"), recorded as E24.**
  Full mute allows Hammer/Pull/Tap: muted legato clucks are standard funk and R&B vocabulary,
  dead-note taps core percussive-fingerstyle material. E4's positive-sounding-position rule still
  binds the hammered/tapped forms. Tweakable later if it feels wrong (user's caveat).
  **Terminology corrected same day (user): NOT "ghost"** — ghost is the emphasis axis's soft tier
  (D8); the muted-legato family says "muted".
- [x] **D6 — Full mute + pre-bend — KEPT FORBIDDEN 2026-08-09**, pre-bend included in E10's bend
  exclusion: the data stores a pitch offset a dead note lacks (incoherent, not merely pointless),
  and no notation source writes the gesture. Reopens only on real chart evidence.
- [x] **D7 — E5: derivation vs validity — CLOSED 2026-08-09, simpler than every draft.** The
  user's second look used the D5 evidence against the first proposal and won: muted legato is
  *common* vocabulary (funk/R&B, bass especially), so plain `H` **derives across fully-muted
  predecessors normally** — requiring a modifier for the common case would surprise exactly the
  charts that use it most. The final shape:
  - **Validity (E5):** pull needs a same-string predecessor whose released fret is higher (a
    scrape's released fret is its slide-out's). Scrape predecessors are valid — pull-from-a-scrape
    "CAN be done" (user), authoring-only since Guitar Pro cannot write it (the earlier
    import-fidelity claim was wrong). Fret-hand-harmonic predecessors stay forbidden (E19).
  - **Derivation (`H`):** infers across ordinary, muted, and tapped predecessors alike; the one
    thing it never *creates* is legato from a scrape predecessor — the single
    derivation-vs-validity gap. Uniform at every selection size (which rejected the single-note
    exception and the three-press cycle: uniform-scope law, and the toggle contract that a second
    press undoes the first).
  - **No `Shift+H`.** A keybind for one marginal case is unwarranted until proven needed (user);
    the favorable analysis (free key, Shift-means-extend fit, own-subset toggling) stays here as
    the ready candidate. And the case needs no affordance anyway — **it is reachable by edit
    order**: author the pull, then scrape the predecessor; the value-based repair re-tests
    against the slide-out fret and keeps what stays justified.
  Folded into `legato-authoring-model.md` (Layer 1 rows, the scrape-predecessor defect note, the
  invalidating-edits row) and the matrix doc's E5 row.
- [x] **D8 — The emphasis axis — ADOPTED 2026-08-09** as the three-value shape:
  `NoteEmphasis { Ghost, Normal, Accent }` replacing the accent bool, `Normal` never serialized,
  ghost+accent unrepresentable by construction (the enum IS the hardening). `Soft` dropped (GP
  has one quiet tier; detection argues against a second), `Heavy` deferred — **GP heavy accents
  import as regular accents for now, with a comment that Heavy may be supported later** (user
  ruling). Full design recorded as `docs/plans/todo/note-emphasis-axis.md`; implementation
  unscheduled.
- [x] **D9 — The GP capo frame — ANSWERED 2026-08-09: capo-relative, and shipped.** The user ran
  the authored experiment: with a capo at 3, an entered "1" resolves to the pitch at absolute
  fret 4, corroborated by a real capo'd tab. Consequences implemented the same day: the importer
  shifts fretted notes by the capo (relative F > 0 → absolute F + capo; 0 stays the open string
  per D1); the harmonic stop reads the shifted note fret (the same physical-stop formula E21
  validates); the natural-label formula (`capo + snapped offset`) was already exactly right —
  the labels ARE capo-relative, which is why the capo-1 score's 7.0/8.2 read as standard
  open-string-family values. The capo-2 fixture's expectations all moved by exactly +2, each
  verified as the intended shift. GP cannot express an absolute sub-capo fret, so imports never
  produce one; **sub-capo validation moves to D12, paired with the editor verb guards** (adding
  the validation alone would let verbs author charts that cannot re-load — the silent-corruption
  class the scrape work closed). D12 also inherits the template and fret-hand-position sub-capo
  analogs (a posture or hand window below the capo is equally meaningless).
- [x] **D10 — The legato workflow's five calls — ALL RULED, closed 2026-08-09.** (1) No
  recalculating chrome; the window is settle-event-scoped with NO timer. (2) Delete clears
  everything — which deleted a special case, since the ordinary settle-on-selection-change rule
  already covers it. (3) Released-fret semantics adopted (last pitched waypoint; a scrape's
  slide-out fret per D7). (4) **Option C accepted, the notation split rejected**: plain `H`
  infers only fret-justified directions (equal/absent predecessor refuses), `Ctrl+H` is the sole
  author of the left-hand tap across its matrix-verified domain, including overriding a derived
  Pull. (5) Legato repairs at IMPORT so charts are never born invalid. The legato doc is now
  fully settled; implementation rides Phase 5 + D12.
- [x] **D12 — Enforcement pass (#27) — SHIPPED 2026-08-09.** D1–D9 are closed; this consumed
  their outcomes: E4–E19 + E23/E24 guards and rules (E2/E20/E21/E22 already enforced; D4's E2
  accent change shipped), the pinch-verb node obligation and attack-away-from-pinch node
  clearing, the two rule-violating test fixtures (tab-paint full-mute+pinch vs E8; GP fixture
  natural+bend vs E9), and the **sub-capo family as one unit**: validation (note frets 1..capo
  invalid when capo > 0) paired with the editor verb guards in the same change — never
  validation alone, or verbs could author charts that cannot re-load — plus the template,
  fret-hand-position, and pitched-glide-waypoint analogs (a scrape's turnarounds and every
  slide-out stay exempt as unpitched travel). The shape that shipped: rules live once in
  `validateChartNotes` (chart_rules.cpp); every planner funnels through the `finalizePlan` gate,
  which validates the SAVED form (`savedChartNote`, the one memory-vs-document seam, now also
  the document writer's source); `normalizeChartLegato` repairs legato in the finalize and at
  import completion; import sheds harmonic-impossible techniques loudly (conversion note) and
  the FHP generators floor every window, slide-in approach, and gesture dip at capo + 1.

- [x] **D13 — The release-inference refinement of legato (user proposal; SIGNED and SHIPPED
  2026-08-09).** The sustain conventions make "the predecessor released early" *provable*
  beyond a bound, refining Option C's "still ringing is unknowable" to "unknowable only at
  close range":
  - **The two data facts.** (1) Import rule 3 (`normalizeImportedSustains`) drops the tail of
    any effect-free note notated shorter than the kept-sustain bound — now the named shared
    constant `g_minimum_kept_sustain_beats` (grid_arithmetic.h, currently one beat), promoted
    from a bare literal so the drop rule and the inference can never disagree. (2) A held tail
    is trimmed/clamped to end exactly the minimum-sustain-distance margin
    (`g_minimum_sustain_distance_whole_note`, meter-scaled) before the next same-string onset,
    so "sustain end reaches (next onset − margin)" IS the maximum representable hold.
  - **The inference.** Onset gap strictly under the bound: tails may have been legitimately
    dropped, so hammer/pull derivability stays fret-only. Gap at or past the bound (`≥`,
    boundary included — user-verified against the quarter-note import test, which pins a
    one-beat note keeping its margin-trimmed tail): a held-through predecessor necessarily
    carries a tail reaching the margin, so a sustain ending short of it proves the string was
    released, and no hammer-on or pull-off from that predecessor is real. Editor-inserted
    notes default to zero sustain, so hand-authoring legato across such a gap means dragging
    the predecessor's tail first — consistent with the display convention, where a tail-less
    note does not read as held either.
  - **Shipped uniformly in all three layers** through the one shared predicate
    `predecessorHoldReaches` (grid_arithmetic): E5 validation rejects, `normalizeChartLegato`
    (now tempo-map-aware) repairs an orphaned Pull to a plain pick — a released predecessor
    justifies neither direction, hammering after a release being `Ctrl+H`'s left-hand-tap
    domain — and the `H` verb skips. A sustain shrink that disconnects a tail repairs its
    dependent Pull inside the same undo entry via the finalize gate (tested); imports
    normalize before validation, so charts are never born invalid.

- **USER RULINGS on the reconciliation, 2026-08-09 — these override the recommendation below
  where they differ:**
  1. **The 2D connector is REJECTED.** Reason (user): it would further diverge the 2D view from
     what the player sees in 3D, and rejecting the data half strengthens that — the surfaces
     should teach the same reading. The rules are enforced in validation/repair regardless, so
     drawing the connection simplifies nothing. The floating triangle stays; the tap-vs-hammer
     display disambiguation is NOT bought (status quo, author-is-authority). **Consequence to
     design for:** the tail lock (2) has no visual affordance, so its refusal feedback (issue
     (d) below) becomes load-bearing rather than nice-to-have.
  2. **Tail LOCKING replaces repair-on-shrink for explicit sustain edits, family-wide.** When a
     note's tail is load-bearing for a dependent transition — a legato successor that needs the
     connection, or its own slide payload — the explicit duration verb REFUSES to shrink it
     rather than silently repairing the transition away (refuse-never-clamp; "no code that
     lies"). Stated uniformly: *a sustain edit may not shrink a tail below what a dependent
     transition requires* — per-kind requirement (legato: the connection point, only where D13
     makes it load-bearing; slide: the last waypoint), no stored lock state, all derived. This
     also fixes an existing silent data loss: shrinking a slide's tail currently clips its
     waypoints. **Legato and slides get ONE shared mechanism** — Phase 7 inherits it rather than
     inventing its own. Sub-call recorded: the lock binds the EXPLICIT verb; implicit 40-Q2-B
     truncation (placing a note in front of a tail) keeps truncate-and-repair, since refusing a
     placement would be worse.
  3. **A break verb** severs the transition from the ORIGIN's side — deletes the successor's
     hopo attack or the origin's slide payload and frees the tail — so the user never walks
     forward to fix a tail. One hotkey, same behavior for both kinds. This is the one place
     forward addressing is right: a delete needs no derivation, and its intent is local to the
     selected note's own constraint. Binding TBD in the keymap review (`L` is already reserved
     for link/slide).
  4. **The `H` toggle-restore is ADOPTED (reversing the recommendation below).** Until the
     selection changes, `H` again returns to the exact previous state INCLUDING any tails the
     assist grew — a true ON/OFF toggle; leaving tails behind would not feel right. After the
     selection changes, tails stay and `Ctrl+Z` is the revert. **Clean mechanism (dissolves the
     "remote note memory" objection):** store only `{keys, history_position}` — the shape
     `ChartFretEntry` already uses — and let the second press REVERSE THE ENTRY THIS VERB
     PUSHED (the plan's exact inverse already exists in `ChartNotesEditPlan`) and drop it, the
     same history splice the multi-digit fret window performs. Nothing remembers a remote
     note's old value; exactness is structural. Settles on the standard set (selection change,
     seek, Esc, playback, undo/redo). Verify the history-splice API before implementing.

  5. **Locked-tail feedback is required, not optional, and lands WITH the lock** — raised by the
     user as "pretty important" once the connector was rejected. Recorded as roadmap **40-Q5**
     with the user's constraint: a display indicator must sit on the PREVIOUS NOTE'S NOTEHEAD,
     because a hammer-on's origin may show no tail at all, so the tail is not a place to put it.
  6. **Junk hopo flags are cleaned at import (user ruling, 2026-08-09):** a Guitar Pro hopo
     destination with no fret motion — equal or absent same-string predecessor — imports as a
     plain `Pick` with a conversion note, rather than the accidental `Hammer` (an unintended
     left-hand-tap claim) the builder produces today. An open-to-open hopo flag is junk data,
     and Option C's derivation refuses exactly this case, so import must not create what the
     verb would refuse.
  7. **The span-blind hold rule is a confirmed bug in D13's implementation, to fix.** The chart
     convention (verified: `grid_arithmetic.cpp` `chartEffectiveSustains`, honored by
     `planAdjustSustain`'s `shares_span`) is that a sustainless note in a same-onset group of
     TWO OR MORE covered by a hand-shape span is held to the SPAN's end — the span is what tells
     the player how long to keep the shape fretted. `validateChartNotes` receives no shapes, so
     `predecessorHoldReaches` reads such a member's zero sustain as a proven release: an
     arpeggiated chord whose held shape is hammered onto across the bound is refused by the `H`
     verb and repaired away at import. Fix: the hold test must judge the predecessor's EFFECTIVE
     hold end (its sustain, or its covering span's end when it is a member of a 2+ onset group),
     not its stored sustain.
  8. **The multi-digit fret fix keeps the repair LIVE (discussed 2026-08-09; user raised
     deferring recalculation until the entry timer settles).** Deferral is attractive but
     collides with two settled things: the finalize gate requires every committed plan's saved
     form to be valid, and a retype that orphans a neighbour's `Pull` is invalid *until* the
     repair runs — so suppressing the repair mid-window would either commit invalid data or make
     the digit do nothing; and D10 call 1 settled that the honest live marks ARE the feedback
     (the pull-off symbol visibly dropping to plain and rising to hammer-on IS the signal), so a
     neighbour's mark flipping during entry is the designed behavior rather than a defect. The
     adopted fix therefore stores the first digit's PLAN in the entry and widens by reversing it
     and re-planning from the pre-entry originals — exact regardless of what was repaired, one
     coalesced undo entry either way, and it retires the settle-on-repair hack. The user's
     concern that the live flip may read as jarring is recorded as a tuning question to judge on
     sight once it is visible.

- [x] **D14+D15 — CLOSED 2026-08-11 by the legato ruling.** How each half landed, before the record
  as written:
  - **D14's assist: ADOPTED and SHIPPED** (2026-08-10, retargeted 2026-08-11) with all three
    corrections intact — pre-checked against `sustainGrowthLimit` and the note skipped whole rather
    than partially extended, the selection pinned so the grown predecessor does not join it, and one
    addition the ruling made: the assist skips gesture-carrying predecessors (a scrape, or any
    slide-out), whose tail is authored geometry rather than slack. The third correction — "geometry
    reversal is Ctrl+Z, no window-scoped restore" — was **overturned by the user the same day**
    (ruling 4): the toggle window restores grown tails too, and the "remote note memory" objection
    dissolved because the window reverses the ENTRY rather than remembering values.
  - **D15: REJECTED, and re-signed by the final spec.** The connected display died on the
    no-surface-divergence rule (user ruling 1 below); the stored-connection data half died on the
    corpus arithmetic recorded below; forward-addressed `H` died on the eight kills. Note the one
    claim of D15's that the new model *did* adopt in a different form: the enum split it called
    unworkable "without stored connection" is exactly what shipped — because the alias zone it feared
    is answered by the resolver reporting the hammer motion for `LeftTap` unconditionally while plain
    `H` is forbidden from producing one, not by stored connection.
  - **The seven "found en route" items are all resolved:** (a) the out-of-selection repair enlarging
    the selection — gone with the repair engine; (b) `H` inert on mixed selections — fixed by the
    oracle law (W5); (c) two-digit fret entry unreachable on a legato predecessor — fixed by W2 and
    then dissolved outright, since no repair fires inside a burst any more; (d) silent refusals —
    W5's counted feedback ships; (e) the span-blind hold rule — fixed by W1's `chartEffectiveSustains`;
    (f) import yielding a tap claim from junk hopo flags — the completion sweep converts it (W8);
    (g) the doc staleness — this close-out.

  *The reconciled recommendation as recorded (three-agent ground-up pass, 2026-08-09; superseded
  in part by the user rulings above).* Three parallel design agents (data model / authoring grammar /
  display+surfaces) enumerated the full option space; one agent measured the local GP corpus
  (102 scores, 4,317 legato transitions). The reconciled shape:
  - **Adopt D15's connected LOOK as derived notation, not data.** 2D: a thin white connector on
    the string line from origin to destination — deliberately DISTINCT from the sustain ribbon
    — with the direction arrowhead at the junction (down = hammer, up = pull), drawn from the
    justifying-predecessor relation validation already computes. A left-hand tap (no justifying
    predecessor) keeps the floating triangle — connected-vs-disconnected disambiguates the
    merged notation with zero format change. 3D: unchanged head-marker idiom (no connector, no
    bars); one shared relation resolved in common core, consumed by both projections,
    per-surface idiom. Nothing forced on data, import, scoring, or the corpus.
  - **Reject D15's data half (connection required/stored at all gaps).** Corpus arithmetic:
    73.8% of real legato pairs are a sixteenth apart or closer, where the spacing margin makes
    the maximum representable connection tail exactly ZERO — the rule is vacuous yet
    undrawable for most legato; above that it converts routine sustain edits into silent
    legato loss for 26.2% of pairs (vs 1.0% under D13); and connection-as-normalized-data
    would hold-score every fast run (60–125 ms held fractions detection cannot resolve) or
    demand scoring carve-outs. D13 as shipped is the right and sufficient data rule (its
    strict zone bites on 1.0% of corpus pairs). The enum split (Legato/LeftTap with derived
    direction) falls with the data half: without stored connection it recreates the ascending
    damped-tap alias zone that killed Question B.
  - **Reject forward-addressed `H`** (eight concrete kills, three against SHIPPED mechanisms:
    the selection-follow rule enlarges the verb's own scope each press and breaks the
    armed-caret invariant; the any-string growth clamp turns unreachable extensions into
    mutating no-ops labeled "Legato"; the multi-digit fret window settles on the hot path so
    two-digit frets die on any legato predecessor; plus Ctrl+H fabrication, chain-gutting
    toggle, span-splitting on chords). Its real GP-muscle-memory benefit is outweighed; the
    connector teaches backward addressing by appearing behind the selected note on first press.
  - **Adopt D14's assist on backward `H`, groups allowed, with three corrections:** (1)
    pre-check reachability under the growth clamp and SKIP the note (counted feedback) rather
    than partially extend; (2) pin the selection (`select_exactly` = original keys) so the
    extended predecessor does not join the selection; (3) the toggle contract governs the
    TECHNIQUE only — H-again clears to Pick; geometry reversal is Ctrl+Z, exactly (no
    window-scoped restore; it would need remote-note memory in a rejected shape). Provably
    order-independent on groups; assist fires on ~1% of real pairs.
  - **Found en route, needs verification then fixes (agent-reported, cited against code):**
    (a) LIVE: out-of-selection repair enlarges the selection and breaks the armed-caret
    invariant (applyChartEditPlan union; comment predates D13; untested); (b) LIVE: `H` is
    inert on mixed selections (all_legato never true; the eligible-subset fix is the unshipped
    #26 item); (c) LIVE: two-digit fret entry is unreachable on any note that is a legato
    predecessor (the D12 repairs_neighbours settle fires on the hot path — needs its own
    ruling); (d) refusals are silent (counted feedback unbuilt — may dissolve most felt
    friction); (e) the hold rule is blind to shape spans (a zero-sustain member under a span
    reads as proven release; arpeggiated hammer-ons refused/repaired at import); (f) import
    yields Hammer on equal/absent-predecessor hopo flags where Option C refuses — corpus
    contains accidental tap claims, unrepaired by design; (g) doc staleness: legato doc's
    planner-bypass claim, the recalc-window settle list wording, file-formats.md's retired
    harmonic/touch keys and missing pinch token.

  *Original D14 entry (superseded by the reconciliation above):*
  With D13, `H` across a gap at or past the kept-sustain bound refuses even when the predecessor
  would justify legato if connected — annoying to fix by hand every time. Proposal: `H` extends
  the tail to the margin point and sets the derived legato in ONE plan/undo entry (precedent:
  the scrape verb extends a zero sustain for its gesture; the pinch verb authors its node).
  Refinements from the discussion: **groups allowed** (pressing `H` on a selection IS intent —
  one uniform rule, no single-vs-group branch); **`Ctrl+H` never extends** (a left-hand tap has
  no predecessor prerequisite); the assist honors the duration verb's any-string growth clamp
  (never authors what a manual drag could not — the cross-string-hold authorability gap is a
  separate watch item); **shrinking a connecting tail repairs the dependent Pull to Pick in the
  same undo entry** (already shipped and tested with D13 — repair over refusal, per the settled
  philosophy; a disconnected Hammer legitimately stands as the left-hand tap under the merged
  notation). The window-scoped toggle-restore idea is dissolved by D15's forward addressing.

- [x] **D15 — The connected-legato model with forward `H` (user direction, 2026-08-09) — REJECTED,
  see the D14+D15 closure above; recorded in full because its corpus evidence is what killed it.**
  Adopt the connected display (the user's reference: Bandfuse): legato ALWAYS shows as the
  origin's tail connecting to the destination, with the transition symbol on the connection —
  hammer/pull arrows join slides and bends in the connected-technique family, replacing the
  floating triangle. Data model: E5's hold requirement extends to ALL gaps (the D13 predicate
  drops its threshold branch — legato simply requires the connection; `g_minimum_kept_sustain_beats`
  remains the import drop rule's bound for non-legato tails). Verb model: **`H` becomes
  forward-addressed, GP-style** — select the origin, `H` derives the successor's attack from
  released-fret comparison and extends the origin's own tail as needed, per selected note,
  uniformly for singles/chords/groups. What this dissolves: the D14 assist machinery (the
  extension IS the verb, at every range), the window-scoped toggle-restore (the extended tail
  belongs to the SELECTED note — shrink it right there; repair drops the dependent legato),
  and the derived-Hammer-vs-tap display ambiguity (connected = hammer-on, disconnected = tap —
  structural, no repair agonizing). Verb grammar: `H` = transition verb anchored at the origin
  (like slides); `Ctrl+H` stays self-addressed (an attack property, no transition). Costs and
  opens: 2D connector rendering (thin connector vs the sustain ribbon — a visual design pass;
  interacts with the parked bend redesign's connected family), the 3D highway treatment (HOPO
  gems with tiny connection sustains — render/scoring idiom vs the WoR baseline), import keeps
  legato predecessors' tails (exempt from the drop rule — GP notates slur origins full-duration,
  so they connect naturally; verify by corpus scan), and corpus re-import (no legacy handling,
  per the standing rule). Evidence to pre-assemble for the gate: corpus measurement of legato
  pairs — gap distribution and whether notated origin durations reach the destination.

- [x] **D16 — What a FULLY MUTED note's tail means (user-spotted inconsistency, 2026-08-09;
  RULED — the rule set below is signed as E25, implementation tracked as W4).** The user asked
  whether the all-muted carve-out reverses E24 (muted legato)
  and observed that the design around muted tails "has not been fully thought through."
  - **Diagnosed root: a DISPLAY rule got imported into a PHYSICAL rule.** `chartEffectiveSustains`
    mirrors the highway's display rule, whose all-muted exemption exists because *a dead chug does
    not ring* — a sounding statement. The hold test asks a different question: *is the finger
    still down?* A span says the shape stays fretted, and muting does not lift the fingers, so for
    "can this be pulled off from" the mute is irrelevant. Two different questions were answered
    with one function.
  - **Scope of the live damage: narrow but real.** Validation applies the hold test ONLY to
    `Pull` (a `Hammer` has no relational constraint — it is always readable as a left-hand tap),
    and gaps under the kept-sustain bound are exempt, so real funk sixteenth-note clucks are
    untouched. The bite is exactly: a sustainless fully-muted strum under a span, pulled off from
    across the bound — refused today though the span says the shape is held. E24 is not reversed
    in general; one path contradicts it.
  - ~~**Recommended fix (the narrow half):** name the two questions separately — a SOUNDING hold
    for display and a FRETTED hold for validation.~~ **RETRACTED by the rule set below** — E25
    makes the existing single hold concept correct, so this split would add a concept to solve a
    problem that no longer exists. Kept only so the reasoning trail is legible; do not build it.
  - **The wider question the user raised, for the ruling:** should a fully muted note carry a tail
    at all? It does not ring, yet today a muted note with a sustain draws an ordinary sustain
    ribbon in 2D (`drawNoteTail` never consults `mute`), which reads as ringing. Options: (a)
    muted notes keep tails, drawn as-is (status quo); (b) muted notes draw no tail, and the
    duration is treated as fretted-time only (affects the hold test, arpeggio spans, and the
    highway's held bars); (c) muted notes keep tails but draw them in the NOISE idiom — the
    user's suggestion, consistent with the settled taxonomy where tremolo is pitched noise and a
    scrape is unpitched noise, since a dragged muted slide IS noise rather than pitch (E10 allows
    the positions); (d) muted notes only carry a tail when a slide payload needs one to travel.
    Interacts with: E10 (muted slides allowed), E24 (muted legato allowed), the highway's held
    bars and their hold scoring, and the import drop rule.
  - **SIGNED RULE SET (2026-08-09, after the user's exploration).** The user's two observations
    settle it: a muted note CAN be tremolo picked (so a noise tail is
    valid with or without a slide), but merely holding a dead note makes no sound (so a plain
    muted tail is silence pretending to be sound). Three rules, and the third costs nothing:
    1. **E25 (new): a `Full`-muted note may carry a sustain only when something keeps making
       noise or travelling** — `tremolo`, or a slide payload (`slides` / `slide_out`). Otherwise
       its sustain is zero. This is the whole rule; it is what the other two rest on.
    2. **A muted tail always draws in the NOISE idiom** (the user's ruling, and now
       unconditional): under E25 the only muted tails that CAN exist are noise or travel, so the
       display needs no "is this a slide" branch. The tremolo band and a slide diagonal already
       coexist on one tail (the band's always-covered core is the plain span), so nothing new is
       needed there.
    3. **Muted legato is thereby bounded to the sub-bound window, with ZERO new code** — a plain
       muted note cannot carry a tail, so past the kept-sustain bound its hold can never reach
       and D13 refuses it, while inside the bound nothing is proven and E24 applies untouched.
       That is where muted legato actually lives (clucks are sixteenths, not two-beat gestures),
       and past the bound the user's own reading is that a silently-held hand "may read odd."
       **This retracts the sounding-vs-fretted hold split recommended earlier**: with E25 in
       place the all-muted span carve-out is CORRECT rather than a bug, and one hold concept
       serves both readers again. A muted note WITH tremolo keeps a long tail and can therefore
       justify legato across any gap — the chug is the evidence that the hand stayed, which is
       exactly what the hold test asks for.
  - **Consequences to build with it:** import must zero the sustain of a `Full`-muted note
    carrying neither tremolo nor a slide (otherwise re-read refuses the chart) — a muted variant
    of the existing drop rule; the slide-authoring verb must grow a muted note's tail as part of
    authoring the slide, the way the scrape verb already extends a zero sustain for its gesture
    (otherwise tail-and-slide are chicken-and-egg); `Palm` mute is untouched, since palm-muted
    notes do ring; the matrix test's plain `dead` note with a one-beat sustain becomes invalid
    and must gain tremolo or lose its tail; and the highway's held bar on a muted note now
    always means "keep chugging or dragging", whose hold-scoring treatment is a plan 24
    question, not a chart one.

- [x] **D17 — The teeth mean REPEATED ATTACKS, not noise (user proposal 2026-08-09; agent
  endorses).** The user asked whether a pick slide should draw as a regular smooth slide, since
  its motion is one clean drag and it can never be tremolo picked, and observed that a full mute
  then gains a real distinction: "noisy travel" (a tremolo-picked muted slide, jagged) versus a
  plain muted slide (smooth, one drag).
  - **Shipped reading, with its rationale stated in code:** the teeth mean *unmeasured noise*, so
    a scrape rides the tremolo strip outright — `tab_paint_core.cpp:253-259`
    (`if (note.tremolo || scrape)`) and `highway_renderer.cpp:3364-3365` (`teethed = tremolo ||
    PickSlide`), both carrying the same comment. Coherent, and both surfaces already agree.
  - **Why the user's reading is better.** (1) It is factually accurate about the gesture: a
    scrape is ONE continuous drag, and teeth assert a repetition that is not happening. (2) It
    stops the tail duplicating the head: a scrape already says "noise" three ways at the head
    (the plectrum silhouette, the PS chip, the required terminal) and a dead note says it with
    the X, so spending the only jagged texture on restating that leaves nothing for
    repetition-versus-continuous. (3) It buys a distinction otherwise UNREPRESENTABLE — the
    user's noisy travel — which matters exactly because E25 makes muted tails either travel or
    repetition. (4) The tail axis then means one thing per decoration: ribbon = duration,
    diagonal = position travel, curve = bend, sine = vibrato, **teeth = repeated attacks** — and
    pitched-versus-noise lives wholly at the head, which is where the attack is.
  - **Cost, stated plainly:** a scrape's tail becomes visually similar to a pitched note's slide
    tail, differing by the head alone. Mitigated by the plectrum's very distinct silhouette (a
    0.94-aspect pick against a disc), its chip, and its always-at-sustain terminal. **Judge on
    sight:** a scrape is a dramatic gesture and the teeth are visually loud, so the smooth form
    may read milder than wanted — the same "look at it before believing the reasoning" caveat as
    the fret-entry flip.
  - **The change is a deletion, both surfaces together (no divergence):** drop the `|| scrape`
    disjunct in the two places above, update the two comments and the one test comment that
    mentions "the tremolo strip (which a scrape otherwise rides)". No data, format, or import
    change. Nothing else changes: a pitched note slid without tremolo already draws smooth, and
    with tremolo already draws teeth.
  - **SHIPPED `0cebd5cf`, and MEASURED.** A render pass at the editor's true default zoom
    (316 px/s — note that the test-suite geometry of 20 px/s is 15.8x zoomed out and flatters the
    change) found: with waypoint times matched, the new scrape tail is **byte-identical** to an
    ordinary pitched slide chain (0 differing pixels), so the tail now carries no scrape identity
    at all. What carries it is the head band: of 3024 differing pixels, 1191 sit in the head and
    the strip above the line — the plectrum silhouette (508), the `PS` plate (135), and the boxed
    unpitched fret chips (396), against a pitched slide's round linked heads ON the line. The
    silhouette ALONE (24 px wide against the disc's 26, same height) is not enough; the plate is
    what makes it plain at 1x. The real cost is salience, not identification: the identity is
    ~12% of the note's ink packed into the first 40 px of a 632 px object, so where the old
    serration was a full-length signal the new look is a point signal. **If scannability ever
    complains, the lever is the unpitched diagonal's own treatment** (broken rather than solid) —
    the one tail element a scrape does not share in kind with the pitched case — NOT a return to
    the teeth, which would resume asserting a repetition that never happens.

- [x] **D18 — A scrape's junctions carry continuation heads (user direction 2026-08-09; SHIPPED
  `9b2c9dd0`).** With the teeth gone, a multi-leg scrape read as disconnected diagonals: each
  turnaround was a bare kink in a white line although the pick never leaves the string. The user
  asked for junction heads "just like a regular slide would", carrying the pick shape.
  - **The fix was a deletion, not an addition.** `tab_projection.cpp` had been marking a scrape's
    turnarounds `linked = false`, but `linked` means "the same note continues through here",
    which is simply TRUE for a scrape — being unpitched had been conflated with not continuing.
    The condition is now the `offset < sustain` test alone, and the painter already knew what to
    do with a linked waypoint.
  - **Three consequences, all falling out rather than being built:** the junction draws in the
    note's OWN head shape via `headShapeFor` (so a scrape junction is a plectrum); it carries the
    traveled fret at the shared `g_plectrum_digit_raise`, which moved up beside the head-shape
    helpers so the onset and junction digits cannot sit at different heights; and a junction that
    carries a head no longer ALSO gets the floating chip (the same number twice), which should
    also relieve the three-chip collision the render pass found at wide zoom.
  - **The terminal deliberately keeps its chip and gets no head:** that is where the pick leaves
    the string, and nothing lands there — the same reading a pitched trail-off already has.
  - **No 3D change:** the highway sweeps its ribbon continuously, so it has no junction
    discontinuity to answer, and `linked` is read by the tab painter alone. Per-surface idiom for
    one fact, not divergence.

## Recorded, no decision needed

- **D11 — The pick-side tap.** Rare technique: a tap performed with the side of the pick, itself
  slidable — distinct from the pick slide in many aspects (user, 2026-08-08). May need its own
  representation later; recorded here and in the matrix doc so nobody force-fits it into
  `PickSlide` or `Tap` when it surfaces.
