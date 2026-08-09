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
- [ ] **D9 — The GP capo frame, empirically.** Our storage convention closes with D1; what
  remains is whether GP's *ordinary* note frets are nut-absolute or capo-relative, which decides
  whether import must shift them by the capo. The harmonic labels now lean capo-relative (7.0 /
  8.2 on the capo-1 score are standard open-string-family labels — correct capo-relative, junk
  absolute). Cheap decisive test: scan the capo'd corpus scores for frets in 1..capo (their
  presence proves capo-relative). **Recommendation: run the measurement before the corpus
  re-import; only 2 capo'd scores exist, so also spot-check against audio pitch if inconclusive.**
- [ ] **D10 — The legato workflow's five calls**, one at a time, from
  `legato-authoring-model.md` ("Remaining user calls"): (1) no recalculating chrome initially;
  (2) empty-selection scope survives a delete; (3) released-fret semantics = last pitched
  waypoint; (4) defer the left-hand-tap concept; (5) whole-stream Layer 1 sweep. Each has a
  recommendation in place.
- [ ] **D12 — Enforcement pass (#27).** Starts once D1–D7 close, consuming their outcomes:
  E4–E19 + E22/E23 guards and rules, the pinch-verb node obligation and attack-away-from-pinch
  node clearing, the two rule-violating test fixtures (tab-paint full-mute+pinch vs E8; GP
  fixture natural+bend vs E9), and D4's E2/glow change.

## Recorded, no decision needed

- **D11 — The pick-side tap.** Rare technique: a tap performed with the side of the pick, itself
  slidable — distinct from the pick slide in many aspects (user, 2026-08-08). May need its own
  representation later; recorded here and in the matrix doc so nobody force-fits it into
  `PickSlide` or `Tap` when it surfaces.
