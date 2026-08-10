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

## Work queue (LIVE — the session task list mirrored here so a disconnect loses nothing)

Approved order, user-signed 2026-08-09. Keep this list and the session task list in step; when an
item ships, mark it and name the commit.

- [x] **W1 — Span-blind hold rule** (defect in D13's implementation). SHIPPED `4f1e793c`:
  `chartEffectiveSustains` resolves span-extended held lengths; three call sites pass shapes.
  **Reopened in part by D16 below** — its all-muted carve-out is a display rule that should not
  bind validation.
- [x] **W2 — Two-digit fret entry on a legato predecessor** (defect in D12's settle hack).
  SHIPPED `d0b1d32b`: the entry carries its applied plan and the widen reverses it to
  reconstruct pre-entry exactly; the selection-follow rule no longer adopts repaired notes.
  **Superseded if W3 is adopted** (the pending model deletes the splice machinery entirely).
- [ ] **W3 — Pending fret entry with invalid-red feedback (user proposal 2026-08-09; needs a
  ruling).** Nothing commits mid-entry: the typed value is provisional, drawn on the head(s),
  RED when it cannot be applied, committed as one undo entry when the window settles and
  discarded (previous value preserved) when it is invalid. Recommended, with one refinement:
  commit IMMEDIATELY for any digit that cannot be widened (`digit * 10 > g_max_fret`, i.e. 3–9),
  so only 1 and 2 ever wait — otherwise every single-digit retype would feel laggy for the
  length of the window. Deletes W2's reversal machinery, the history splice, and all mid-entry
  chart mutation; needs provisional-value state on the chart-edit view model. Insert-at-caret
  becomes a ghost note, which matches the existing Alt-hover insert-ghost idiom.
- [ ] **W4 — Muted-tail consistency (D16 below; needs a ruling).** Blocks nothing but touches
  W1, E10, E24 and the display; settle before the lock/break work so the tail rules are stated
  once.
- [ ] **W5 — `H` eligible-subset fix + counted feedback.** `H` is currently inert on any mixed
  selection (its all-legato test is never true, so the toggle sticks and the plan finds nothing
  to change), and every refusal is silent. The feedback channel is load-bearing for W6.
- [ ] **W6 — Tail lock + break verb + locked-tail feedback (40-Q5).** One shared mechanism for
  legato and slides; the break verb frees a tail from the origin's side; the feedback is
  **editor-only** (user ruling: not visible in 3D).
- [ ] **W7 — D14 assist + the `H` toggle window.** The assist extends a predecessor's tail when
  that is the only blocker, groups included; the second press reverses this verb's own entry.
- [ ] **W8 — Cleanups found by the design agents.** Import junk-hopo flags to `Pick` with a
  conversion note (ruled); doc staleness in `file-formats.md` (retired `harmonic`/`touch` keys,
  missing `pinch` token) and the recalc-window settle-list wording.

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
  2D surface — a two-position technique may eventually want both.
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
     convention (verified: `highway_view_state.h` `highwayDisplayHoldEnds`, honored by
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

- [ ] **D14+D15 — RECONCILED RECOMMENDATION (three-agent ground-up pass, 2026-08-09; superseded
  in part by the user rulings above).** Three parallel design agents (data model / authoring grammar /
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

- [ ] **D15 — The connected-legato model with forward `H` (user direction, 2026-08-09).**
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

- [ ] **D16 — What a FULLY MUTED note's tail means (user-spotted inconsistency, 2026-08-09;
  needs a ruling).** The user asked whether the all-muted carve-out reverses E24 (muted legato)
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
  - **Recommended fix (the narrow half):** name the two questions separately — a SOUNDING hold
    for display (keeps the all-muted carve-out) and a FRETTED hold for validation (span extension
    regardless of mute), each documented as to why it differs, so the divergence is deliberate
    rather than an accident waiting to be "unified" by a later reader.
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
    bars and their hold scoring, and the import drop rule. **No R yet.**

## Recorded, no decision needed

- **D11 — The pick-side tap.** Rare technique: a tap performed with the side of the pick, itself
  slidable — distinct from the pick slide in many aspects (user, 2026-08-08). May need its own
  representation later; recorded here and in the matrix doc so nobody force-fits it into
  `PickSlide` or `Tap` when it surfaces.
