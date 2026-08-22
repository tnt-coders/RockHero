# E25 Muted Tails — Implementation Design (W4)

**Status: SHIPPED 2026-08-20.** Built exactly as ruled below, as the one normalizer
(`normalizeChart` / `normalizeChartNote` in `chart_rules.h`): the technique drops, the board and
capo range repairs, the stranded strike, the stilled-scrape demotion, and E25's muted-tail trim are
one function with the settle sweep as its last stage; the validator is structural refusals plus the
fixpoint; the package reader and the GP importer both call it and nothing else; the document
writer refuses what the reader would; the editor's plans carry the two own-truth repairs (E4, E25)
and refuse everything else; and an open that normalized anything shows the one-shot notice
(`loadConversionNoticeText`, `IEditorView::showNotice`) naming each rule and its places, opens
dirty, and leaves the file untouched until a save. The principle is codified in
`docs/design/architectural-principles.md` ("Domain Invariants: One Normalizer, Normalize-or-Refuse").
What follows is the design record that produced it.

**Superseded in part 2026-08-22 by the note-sustain model**
(`docs/plans/in-progress/note-sustain-model.md`): E25 is now presentation rule 4 in
`presentedChartNotes` rather than a repair of the stored field, so a dead note keeps the ring it
was notated with (that ring is the timing a legato claim after the cluck reads) and only its DRAWN
tail goes. The rule's substance — a dead string presents no tail unless tremolo or a slide keeps it
making noise or travelling — is unchanged. Two code citations below went with that change and are
left as the record they were: `sustainGrowthLimit` is deleted (the duration verbs and the `L`
assist clamp at `sustainBoundOf`, the next onset on the note's own string), and no verb extends a
zero ring any more, because no note stores one.

The rule itself was signed 2026-08-09 as E25 (ruling D16) and needs no revisiting. What this
document holds is the *implementation* design, opened 2026-08-20, paused mid-discussion, and
settled later the same day: the user ruled all four open questions, each on the recommended
option (the resolutions are recorded in §6). What remains before code is §6.5 — a standing
watch-item remedy that the load design overrides and that needs the user's read.

Source of the rule: `technique-review-walkthrough.md` (D16 for the ruling, W4 for the work item)
and `technique-compatibility-and-hardening.md` (the E25 row, and the matrix status line that names
E25 as the one signed rule with no code).

---

## 1. The rule, as signed

> A `Full`-muted note may carry a **sustain** only when something keeps making noise or
> travelling — `tremolo`, or a slide payload (`slides` / `slide_out`). Otherwise its sustain is
> zero.

The reasoning, from D16: a dead note does not ring, so a plain muted tail is silence pretending to
be sound. The two things that legitimately fill it are repeated raking (you *can* tremolo pick a
mute) and a dragged muted slide. `Palm` is untouched — palm-muted notes ring.

**The exceptions were re-confirmed 2026-08-20.** The user initially stated the rule more strictly
("ANY note marked dead regardless of other techniques it carries should not be able to carry a note
tail"), was shown that E25 already exists with the two exceptions, and confirmed the signed version
stands. Keeping them matters beyond taste: D16 records that they are what make a muted tail *always*
mean noise-or-travel (so the display needs no conditional), what bounds muted legato to the
sub-bound window with zero new code, and what makes the display's all-muted span carve-out correct
rather than a bug. Dropping them re-opens D13 and E24.

**The bound was re-affirmed as THE rule on 2026-08-20** (walkthrough W14), after a same-day detour:
disqualifying a dead predecessor outright (E26) was built and reversed because it turned every
imported muted cluck into a picked note. The user then asked the real question — a dead note has no
tail, so past the bound it can never prove a hold; is that an exception waiting to happen? — and
ruled no: the bound is the same hold test every note obeys, a strike a quarter note or more after a
muted scratch is a fresh one (the `LeftTap`'s statement), and a distance exception has no principled
distance. The one alternative with no exception either — reversing E25 so a dead tail means "the
hand stays on the damped stop", which would delete this whole plan — was put up and rejected: it
puts tails on dead notes in both views, and those are the only tails the game could never judge.

---

## 2. What was established on 2026-08-20

The session began with the user observing that the example project has dead notes carrying tails,
and asking whether that was an artifact of hand-editing and why the editor neither fixes nor warns.
Both halves were investigated.

**It is not a hand-editing artifact.** E25 has *zero* implementation anywhere — not in the editor's
edit verbs, not in the GP importer, not in package read. Every path produces dead notes with tails
equally, and a GP import would import and validate one as fine, because the rule that would refuse
it does not exist yet.

**The editor really does not check on load or save, and the gap is wider than E25.** Confirmed by
reading the code:

| boundary | validates? |
|---|---|
| `readChartDocument` (`chart_document.cpp:594`) | **No** — structural parse only; its own header says "Parsing is structural only; run validateChartRules … afterwards" |
| `writeChartDocument` (`chart_document.cpp:674`) | **No** — writes unconditionally |
| package read (`rock_song_package_read.cpp:830`) | Yes |
| GP import (`gp_chart_builder.cpp:2785`) | Yes |
| editor edit verbs (`chart_edits.cpp:275, 720, 829, 874`) | Yes, per note |

So the editor can open, edit and save a chart violating rules that *are* implemented (dead+bend,
dead+pinch, the pick-slide fixpoint), with no warning, and it surfaces only later when the song is
read as a package. The validator exists; it is simply not wired to the editor's own file boundary.

---

## 3. The reframe that drives the design

Invalid charts do not mainly come from corruption. The importer validates and the edit verbs
validate, so neither can produce one. That leaves hand-editing and — far more importantly — **rule
changes**. E25 will invalidate previously-valid charts the moment it lands, and the compatibility
matrix is still growing, so this recurs.

This is therefore a routine workflow, not an exceptional error path, which argues against anything
shaped like a one-shot dialog.

---

## 4. Load policy — where the discussion landed

Three positions were considered in order.

1. **Refuse on load** (agent's initial recommendation, matching package read). Rejected by the user:
   *"If we flat out refuse there will be no way to load a broken chart to repair it."* Correct — a
   rule change would brick every existing project.
2. **Load, raise an error, allow continuing only if you normalize** (user). Better, but the agent
   raised that it leaves an *invalid chart in memory*, and proposed instead that normalization be an
   ordinary undoable command with the hard gate moved to save.
3. **Normalize by default at load, warn if normalization occurred** (user). **This is the current
   position, and the agent agrees it is the best of the three.**

**Why option 3 wins.** Option 2's real cost is that every projection, display and verb is written
against the matrix's invariants; tolerating a chart that violates them weakens that contract
everywhere to buy one recovery case. Normalizing at the boundary keeps the illegal state out of the
program entirely, which is the project's own "illegal states unrepresentable" principle, and it
reuses the boundary-normalizer pattern that `savedChartNote` and `executableChartNote` already
establish. It also deletes the save gate, the normalize command, and the error state that option 2
required.

**What option 2 was protecting, and how to keep it cheaply.** A dead note with a tail has two valid
repairs — drop the tail, or add tremolo — and normalization always picks the first, discarding the
duration before the user has looked at it. That is recoverable if the warning is *specific*: name
the rule and the note positions, not just a count. The file is untouched until save, so anyone who
meant tremolo knows exactly where to go add it.

---

## 5. Proposed shape (agreed in principle, blocked on the open questions)

**State the rule once, as a FIXPOINT.** The hazard in "normalize + validate" is the rule being
written twice — once as "drop this", once as "reject this" — which must then agree by hand. That
defect is already solved a few lines away in `chart_rules.cpp`, for pick slides:

```cpp
// Stated as a FIXPOINT rather than by listing the overridden fields:
// a saved note must already equal its own saved form.
if (!(savedChartNote(note) == note))
```

So the **normalizer owns the rule** (a `Full`-muted note without `tremolo` or a slide payload has
zero sustain) and the **validator is just `normalized(note) == note`**. One authority, no hand
agreement, and the next droppable rule slots in without touching the validator at all.

Then:

- **One normalizer**, called by **both** import and load, so the two cannot drift.
- **Load**: parse, normalize, warn with rule and positions if anything changed. Never refuses
  (subject to open question 2 below).
- **Save**: validates as an **assertion** returning the typed error — not a UX gate. It should never
  fire; if it does, that is a verb bug worth surfacing rather than persisting.

### Consequences D16 already lists, to build with it

- **Import must zero the sustain** of a `Full`-muted note carrying neither tremolo nor a slide,
  "a muted variant of the existing drop rule". The existing drop rule is at
  `gp_chart_builder.cpp:1135` ("short sustains without techniques were dropped").
- **The slide-authoring verb must grow a muted note's tail** as part of authoring the slide, the way
  the scrape verb already extends a zero sustain for its gesture — otherwise tail-and-slide are
  chicken-and-egg. The scrape precedent is `chart_edits.cpp:750` ("A scrape needs room to travel, so
  a sustainless note grows one first"), bounded by `sustainGrowthLimit` (`chart_edits.cpp:120-128`).
- **The matrix test's plain `dead` note with a one-beat sustain becomes invalid** and must gain
  tremolo or lose its tail.
- **`Palm` is untouched.**

### Confirmed ZERO code

D16's rule 2 ("a muted tail always draws in the NOISE idiom, now unconditional") needs no display
work. Under E25 the only muted tails that can exist already carry teeth (tremolo) or a diagonal
(slide), so there is nothing to branch on and nothing new to draw. It is a consequence of the rule,
not a task.

The highway's held-bar meaning on a muted note ("keep chugging or dragging") is a plan 24
hold-scoring question, not a chart one.

---

## 6. OPEN QUESTIONS — settle these before writing code

> **Second look, 2026-08-20 (later session): the normalizer already exists, which resolves most of
> this.** `executableChartNote` (`chart_rules.cpp:239`) already DROPS exactly the payloads the
> validator refuses — dead+bend/vibrato, the dead pinch, tap-harmonic tremolo, fret-hand-harmonic
> payloads — and its header contract (`chart_rules.h:171`) states the job in so many words: *"It
> covers exactly the rules a single note can be made to obey by DROPPING something."* The GP
> importer already applies it destructively as its counted shed pass (`gp_chart_builder.cpp:2686`),
> with a comment recording that a hand-kept copy of the list "drifted from the list there twice."
> So the validator's droppable section is today a hand-restated fixpoint of the shed — the
> rule-stated-twice defect. On the reporting side, the package reader already has a `conversions`
> channel that the editor logs and that opens the session dirty ("memory no longer equals disk",
> `project.cpp:383-400`), and the load gate that would brick projects is the package read's
> `validateChartRules` refusal (`rock_song_package_read.cpp:830`).
>
> **All four RULED by the user 2026-08-20 (same day, later session), each on the option below:**
>
> - **Q1 — all drops, stated once.** The validator's droppable section becomes the fixpoint
>   `executableChartNote(note) == note` (the pick-slide `savedChartNote` fixpoint precedent,
>   `chart_rules.cpp:557`), deleting the four hand-restated refusals. E25's arm lands in the shed
>   and the validator enforces it for free. Edit verbs still refuse — a plan failing the fixpoint
>   is refused — so verb behavior is unchanged.
> - **Q2 — shed, then refuse structural, and the taxonomy needs no enum.** Parse → shed
>   (normalize + warn) → validate; whatever the validator still refuses after the shed is
>   structural by construction. The shed's own header already draws the line: range violations,
>   missing data (the pinch node), and everything relational stay refusals.
> - **Q3 — no third normalizer.** The E25 arm (`dead`, no `tremolo`, no slide payload → zero
>   sustain) goes into `executableChartNote`, placed LAST so the fixpoint settles in one pass
>   (the tap-harmonic arm can clear a tremolo that was the tail's justification). The package
>   read runs the same shed just before its validate, reporting through the existing
>   `conversions` channel. `savedChartNote` stays the memory→document latents seam.
> - **Q4 — one-shot notice with positions.** The conversion note names the rule and the note
>   positions (capped, full list in the log) and a themed message box shows it once at open, only
>   when load actually shed something — which after a rule change happens once per project, since
>   save normalizes. The session still opens dirty. The rejected alternatives: conversions log +
>   dirty flag only (import parity, but close to silent — the concern that opened this question),
>   and deferring a durable surface to W3's channel. GP import keeps its counted convention:
>   import converts wholesale, while a load shed edits saved work, which earns specificity.

## 6.5. The legato-flatten knock-on — RULED 2026-08-20 with the load design

**Status.** Live, and settled. (It was briefly recorded as dissolved by E26 — a dead predecessor
justifying nothing — until E26 was reversed the same afternoon; see walkthrough W14.) With the
bound re-affirmed as the rule, the trim DOES have a legato knock-on, and the user ruled the
reconciliation below along with option A: the flatten is reported in the one open-time notice
beside the trims, the session opens dirty, the file is untouched until save, and the watch item is
retired when this plan ships. The original analysis:

`docs/tracking/watch-items.md` ("A muted-tail trim would flatten legato claims corpus-wide —
trigger: W4/E25 builds the trim", recorded 2026-08-11) fires on this exact work, and its recorded
remedy predates today's rulings and conflicts with them. The hazard is real and second-order: a
fully muted note's tail counts at stored length for the connection hold test, so the moment the
shed trims it, every legato claim that depended on that tail stops resolving and the load-time
settle sweep (`sweepUnjustifiedLegato`, run by every load path) flattens them to plain picks — a
knock-on edit the trim warning alone does not mention. The remedy recorded then: run the trim as
an editor plan operation riding one undo entry, "never as a silent conversion inside
`readRockSongPackageDirectory`".

That remedy imagined a world where the validator refuses and a migration tool repairs. Today's
rulings chose the other world: load normalizes and can never refuse, so the trim MUST live in the
load shed — an editor-verb-only trim would leave the package read either refusing (bricking, the
thing Q2 forbids) or admitting an invalid chart. The reconciliation, RULED with option A:

- Keep the signed load design unchanged.
- Extend the Q4 notice to cover the knock-on: the settle sweep already returns the set it
  flattened, so the one open-time notice names BOTH the trimmed tails and the legato claims that
  consequently read as plain picks, each with positions. Nothing is silent, which was the watch
  item's real complaint; "reversible" is answered by the file being untouched until save rather
  than by undo.
- Retire the watch item with this ruling recorded (its trigger has fired and its remedy is
  superseded), per the registry's own discipline.

## 6.6. Q2's PREMISE WAS FALSIFIED the same day — range rules change too (needs the user's read)

Q2 classed range violations (fret past the cap, a fret on or below the capo, an FHP window off
the neck) as *structural*: "only producible by a corrupt or hand-mangled file, never by a rule
change, so load never refuses in practice." Four rulings shipped on 2026-08-20 are rule changes
that produce exactly those violations in previously-valid saved projects:

| change | previously-valid form it invalidates |
|---|---|
| `g_max_fret` 30 → 24 | a note, waypoint, exit, template, or FHP window the old importer clamped to 25–30 (high-capo imports), and any FHP window generated near the old cap |
| open-string slide rules | an imported legato glide from or to an open string |
| every slide fret floored at `capo + 1` | an imported scrape under capo ≥ 3 whose default terminal sat at the bare fret 3; any turnaround or exit on a capo'd fret |

Today none of these can enter memory — the package reader refuses at `validateChartRules`, so
the editor and the game both refuse to OPEN such a project. That is the brick W4 exists to
prevent, arriving through the class Q2 said could not change. (The shed's scrape-start
exclusion is a red herring here: the shed does not run at load at all today.)

**RULED 2026-08-20 (user): ONE normalizer, not two.** A first draft here proposed a tuning-aware
range pass *beside* the per-note shed. The user objected to the proliferating validation paths,
and the objection was right: that would have been a fourth statement of the rules (validator,
shed, the importer's scattered clamps, and the new pass). The signed shape instead:

- **`normalizeChart(chart) → {chart, conversions}`**, chart-level, with the tuning in hand, owns
  every REPAIRABLE rule: the technique drops (today's `executableChartNote` becomes one stage
  of it, not a sibling) and the range repairs (clamp a fret or exit past the cap to the cap, drop
  a waypoint below the floor, lift an exit below the floor to `capo + 1`, fit an FHP window
  onto the neck, demote a scrape whose start cannot be floored to the plain pick it sounds like
  with its path cleared). The repair POLICY — clamp vs. drop vs. demote — is stated exactly
  once, as this function.
- **The validator shrinks to two things**: the genuinely structural refusals no repair can
  express (ordering, missing data, a string the tuning lacks — which no rule change produces),
  and the fixpoint `normalizeChart(chart) == chart`. Nothing else is restated there.
- **Import** calls it at build completion (where it calls the shed today); its scattered clamps
  and floors migrate INTO it as each is next touched, instead of living on as guarantees kept
  in step by hand.
- **Load** (this plan) calls it, reports the diff, opens the session dirty — a rule change
  repairs-and-warns instead of bricking.
- **Editing** stays refuse-at-the-gate: `finalizePlan` asks whether the candidate is a
  fixpoint, and the pending entry paints red when it is not. Where the design prefers a repair
  to a refusal (the E4 strike flatten already does this inside `finalizePlan`), that repair IS
  the normalizer, not a second rule.

Every chart the editor, importer, or loader can produce is then a fixpoint of one authority —
the user's "invalid states impossible while editing", achieved with one statement of each rule
instead of three. The standing principle, to be codified in `architectural-principles.md` once
the user confirms it firmly: **memory holds only normal charts; every mutation path is
normalize-or-refuse through one authority.** Until this builds, the policy for projects today's
rulings invalidated is the backlog's: re-import from the GP source.

### 6.7. The load flow in detail (user question, 2026-08-20)

Opening a `.rhp` whose chart carries normalizable issues:

1. The package extracts and the chart document PARSES structurally (today's `readChartDocument`,
   unchanged — it has never validated).
2. `normalizeChart` runs on the parsed chart BEFORE anything else sees it. The invalid form
   exists only as a local inside the reader; memory, the view, the undo history, and the game
   never hold it. There is no "enter the invalid state, then fix it" step.
3. `validateChartRules` runs on the result. The fixpoint half is trivially satisfied; only a
   structural violation can still refuse, and it refuses loudly exactly as today (the file is
   corrupt, not out of date).
4. The conversions — rule, count, positions — flow through the package reader's existing
   `conversions` channel: logged at `project open` as today, and the session opens DIRTY
   ("memory no longer equals disk"), exactly as hand-made and third-party files already do.
5. Per the Q4 ruling, a one-shot themed notice shows at open, naming each rule and its positions
   (capped, full list in the log) — and also the legato claims the settle sweep flattened as a
   consequence (§6.5), so nothing is silent. It is a summary AFTER the fact, never a prompt
   before: a prompt's decline branch would need somewhere to put an un-normalized chart, and
   there is nowhere. The decline branch is simply "close without saving" — the file on disk is
   untouched until the user saves, so anyone who meant a trimmed tail as tremolo, or wants the
   original bytes kept, has them.
6. The conversions are part of the loaded BASELINE, not undo entries: history starts from the
   normal chart. That is what "never enter the invalid state" means for undo.

The game's package load is the same reader and the same normalizer; it logs and plays the
normal chart. One path.

### 6.8. Validation at save and publish (user question, 2026-08-20)

Yes, and it duplicates nothing: it is one more CALL of the one validator, at the package
WRITER, so save and publish both get it and the writer refuses to emit a document the reader
would refuse — a symmetric gate with zero restated rules. Memory is valid by induction (load
normalizes, verbs refuse), so this assertion should never fire; when it does, it has caught a
verb bug, and it must fail LOUDLY: the save refuses with the typed error naming the rule and the
position, and the message says plainly that this is a defect worth reporting. The editor may
ALSO offer "repair and save", because the repair is the same normalizer applied as one undoable
edit (diff the normalized chart against memory into a `ChartNotesEditPlan`, push it as "Repair
chart", then write) — so the user is never stranded, and the offer adds no second rule. The
offer must not soften the message: an auto-fix that hides the bug is worse than the bug.

**Q1. Normalize-on-load forces a taxonomy the rules do not currently have.** If load normalizes
rather than refuses, every rule must be normalizable or load still has a failure path. Today they
are not uniform:

| rule | today |
|---|---|
| dead + bend/vibrato | validator **refuses** |
| dead + pinch harmonic | validator **refuses** |
| pick slide carrying anything | fixpoint against `savedChartNote` (**drops**) |
| **E25** dead + plain sustain | D16 specifies **drop** |

E25 is specified as droppable while its two immediate siblings are hard refusals, so a chart with a
dead+bend note would still fail to load. *Agent's lean:* make them all drops — they are all "this
payload cannot exist on this note", and dropping the payload is the obvious repair in each case. The
fixpoint pattern would absorb them and the validator's dead-note section would largely **delete**.

**Q2. Some violations are not payload-shaped.** Trimming a tail or dropping a bend is repair. A note
on a string the tuning lacks, a fret past the board, or non-ascending slide waypoints is not — the
note itself is incoherent, and "normalizing" it means inventing data or deleting the note. Silently
deleting a user's notes on load is a very different act from trimming a tail. *Agent's lean:* name
two classes explicitly — **droppable** (a payload the note may not carry: normalize and warn) and
**structural** (the note is incoherent: still fail the load, loudly). Structural violations can only
come from a corrupt or hand-mangled file, never from a rule change, so "load never refuses" still
holds for every case that arises in practice.

**Q3. Where does the normalizer live?** The proposal adds a third boundary function beside
`savedChartNote` (write-time latents) and `executableChartNote` (playability). Three normalizers is
a smell worth poking at; the alternative is folding legality into one of the existing two. *Agent's
lean:* keep it separate, because the jobs really are distinct — but this is not settled, and if it
should be two, which one absorbs it must be worked out before any code.

**Q4. What surface carries the warning?** `showError` is transient. On a GP import of a dense song
this could be hundreds of notes, and a toast that vanishes is close to silent — which defeats the
point of warning at all. A durable surface would fix it but adds UI concepts. Needs the user's read.

---

## 7. Code sites, so the next session does not re-investigate

| what | where |
|---|---|
| dead-note rules (bend/vibrato, pinch) | `chart_rules.cpp:371-400` |
| pick-slide fixpoint precedent | `chart_rules.cpp:~549` |
| `savedChartNote` | `chart.cpp:38` |
| `executableChartNote` | `chart_rules.cpp:239` |
| `validateChartRules` | `chart_rules.cpp:123` |
| sustain sign check (only existing sustain rule) | `chart_rules.cpp:304` |
| load (structural only) | `chart_document.cpp:594` |
| save (no validation) | `chart_document.cpp:674` |
| package read (validates) | `rock_song_package_read.cpp:830` |
| GP import (validates) | `gp_chart_builder.cpp:2785` |
| existing import drop rule | `gp_chart_builder.cpp:1135` |
| scrape grows a sustainless note | `chart_edits.cpp:750`, limit at `120-128` |
| edit verbs validate per note | `chart_edits.cpp:275, 720, 829, 874` |
