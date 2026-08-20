# E25 Muted Tails — Implementation Design (W4)

**Status: rule SIGNED, implementation NOT STARTED, design UNDER DISCUSSION.**

The rule itself was signed 2026-08-09 as E25 (ruling D16) and needs no revisiting. What this
document holds is the *implementation* design, which was opened 2026-08-20 and paused mid-discussion
at the user's request. Four questions are still open — they are listed at the bottom, and they
should be settled before any code is written.

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
