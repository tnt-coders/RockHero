# Plan 62 — Kept-Sustain Bound Per Song

## 1. Status

**SUPERSEDED — kept as a watch item's remedy.** The kept-sustain bound is a DURATION in seconds
(`g_minimum_kept_sustain_seconds`), signed on sighting; one value serves every song today. This
plan is the pre-scoped remedy of the watch item "The kept-sustain bound is one duration for every
song" in `docs/tracking/watch-items.md`: if a sighting ever shows that no single value serves, the
per-song value becomes an override of the seconds constant. Do not execute until that trigger
fires, and re-verify the inventory below first — it names the note-value symbols the duration
bound removed, and a per-song value would be seconds, not a note value. The rest of the plan
stands as authored otherwise: **ungated**, the shape settled (§7), with 62-Q1..Q4 as build-session
questions carrying recommendations.

## 2. Goal

The **kept-sustain bound** — the shortest ring that earns a drawn sustain tail on both surfaces,
today one constant for every song — becomes **a per-song value**, with the current setting as the
default for every song that states none. A charter sets it once for a song; the whole song is
judged against that one note value; nothing about how a tail is drawn, rested or trimmed changes.

## 3. Non-goals

- **No change to the default.** The shipped rule — a ring LONGER than an eighth note earns a tail,
  compared strictly — stays exactly as it is for every song without a stated value. The user
  sighted the inclusive form on 2026-09-08 and reverted it the same day ("> 1/8th note looks right
  for MOST songs", `7bbe403b`).
- **No tempo-derived bound.** A time-based threshold was designed and rejected for now (§11).
- **No per-section, per-region or per-note value.** One value per song, by ruling (§7 decision 2).
- **No change to the minimum sustain distance** (`g_minimum_sustain_distance_seconds`), the
  reveal lead, or any other presentation constant. Each is its own question.
- **No new tail rule.** Rule 3 of `presentedChartNotes` reads a value instead of a constant; its
  comparison and its group verdict are untouched.

## 4. Constraints

- **Layering.** The value is read by the presentation pass in `rock-hero-common/core`; the editor
  authors it, the game reads it, neither restates the rule
  (docs/design/architectural-principles.md).
- **Stated once.** Today the note value appears in exactly one place, the constant's initializer
  (`grid_arithmetic.h`), and every comment, guide and rule text names "the kept-sustain bound" and
  points there. That discipline was kept deliberately for this plan: the constant becomes the
  DEFAULT of a policy value, and still the only place the default's value is spelled.
- **Formats change in place.** No format version bump and no migration path: a song without the
  field reads as the default, a song with it reads the value (the project's standing rule on
  legacy code).
- **Derived over authored, respected.** The bound is a charter's presentation taste, not a
  performance fact, so it is authored — but it is one scalar per song, never a per-note field, and
  nothing downstream may cache a derived copy of it.
- **Builds** through `.agents/rockhero-build.ps1`, as separate invocations, only where a change
  determinately warrants the check.

## 5. Current-state inventory (verified 2026-09-08 @ `7bbe403b`)

- **The bound**: `g_minimum_kept_sustain_whole_note{1, 8}` and `minimumKeptSustainBeats(denominator)`
  in `rock-hero-common/core/include/rock_hero/common/core/chart/grid_arithmetic.h`. Its doc block
  already says it is "headed for a user-tunable option" and that the initializer is the only
  statement of the value.
- **The one reader**: rule 3's per-member earning in `presentedChartNotes`
  (`rock-hero-common/core/src/chart/chart_presentation.cpp`, the `kept_bound` local),
  `saved_notes[index].sustain > kept_bound`. Nothing else reads the constant.
- **The pass's signature**: `presentedChartNotes(const ChartConnections&, const TempoMap&)`
  (`chart_presentation.h`), called from `chartResolutions(notes, tempo_map)`
  (`chart_legato.cpp`), which is the one derivation every surface reads. Production callers of
  `chartResolutions`: the projection (`chart_projection.cpp`, `makeChartViewState`), the editor's
  verbs (`rock-hero-editor/core/src/chart/chart_edits.cpp`, two sites), and the importer's
  `presentedNotes` in `gp_chart_builder.cpp` (which calls `presentedChartNotes` directly and reads
  only `.notes`). Tests call both with the default.
- **The song**: `Song` carries the arrangements and the tempo map; `song.json` is the package's
  song-level document (plan 43 owns its metadata fields and the Song Information dialog). There is
  no presentation section in it today.
- **The census** (`test_corpus_census.cpp`, `[.local-corpus]`) reports "tails standing after rules
  1-4" and "hidden: the curtain owns part of it"; both moved 52,990 → 167,819 and 47,638 →
  161,000 under the inclusive experiment, which is the measurement that motivated this plan's
  §11.
- **The 2D lane and the 3D board** both draw the presented tail and neither carries a threshold of
  its own, so both follow any value the pass is handed.

## 6. Dependencies

- **docs/plans/roadmap/43-song-information-and-art.md** — owns `song.json`'s metadata fields and the
  Song Information dialog. The per-song value is a song-level field and its editor surface belongs
  in that dialog; if plan 43 has not executed, Phase 3 lands the field's control in the same
  dialog shell 43 will own, not a second dialog.
- **docs/plans/roadmap/40-chart-editing.md** — the undo history the edit enters.
- **docs/plans/roadmap/26-game-startup-menus-library.md** — the game reads the song document; no
  work there beyond consuming the field through the shared reader.
- **docs/tracking/backlog.md**, "Make the kept-sustain bound a user option" (2026-09-07) — that item
  is this plan's 62-Q1 option B, recorded there and answered here.

## 7. Decisions already made (the settled shape)

1. **A note value, compared strictly.** The bound stays a fraction of a whole note and rule 3 keeps
   "LONGER than". The inclusive comparison was built, sighted and reverted on 2026-09-08.
2. **One value per song.** The user's ruling on a tempo-derived bound (§11): "songs with varying
   tempo right around a threshold BPM will look inconsistent switching between displaying or not
   displaying tails on shorter notes." A per-song value cannot flip inside a song.
3. **Absent means the default, and the default is the constant.** The constant's initializer stays
   the one place the default's value is spelled; the policy value's default is read from it.
4. **The pass takes the value as a parameter.** `presentedChartNotes` and `chartResolutions` gain a
   presentation-policy argument carrying the bound; the constant is that argument's default. No
   global, no singleton, no reading a settings store from core.
5. **The value travels with the song.** It is the charter's statement about the song, so it lives
   in the package and the game shows what the charter chose (62-Q1 recommendation).

## 8. Open questions for the build session

1. **62-Q1 — where the value lives.** (A) Authored per song in `song.json`, travelling with the
   package, editable in the Song Information dialog. (B) A per-app user option overriding the
   song's value locally (the 2026-09-07 backlog item). (C) Derived from the song's dominant tempo
   (§11). **R: A now; B as a compatible later addition layered over A; C rejected for now.**
2. **62-Q2 — the allowed values.** (A) Any whole-note fraction. (B) A fixed menu of common values
   (sixteenth, eighth, dotted eighth, quarter, half) stored as the fraction. **R: B** — the
   picker is the affordance a charter wants and the stored form stays the exact fraction the rule
   reads, so A's generality costs nothing if it is wanted later.
3. **62-Q3 — does the editor show the effective value anywhere but the dialog?** (A) No. (B) A
   status-bar chip when a song states a non-default value. **R: A** until a sighting asks for B.
4. **62-Q4 — import.** (A) Imported songs state no value (default). (B) The importer guesses one
   from tempo. **R: A** — the importer states performance facts, and this is a taste.

## 9. Phased implementation

Command forms (from `.agents/README.md`, run from the repository root):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 1 — The policy value, threaded, default only

A presentation-policy value carrying the kept-sustain bound as a whole-note fraction, defaulted from
the constant; `presentedChartNotes` and `chartResolutions` take it; every production caller passes
the default. Behaviour identical, census identical. Tests: the default equals the constant; a
non-default value changes rule 3's verdict for a ring between the two values and nothing else.

### Phase 2 — The song document

The field in `song.json` (read and written in place; absent reads as default; a value the menu does
not offer is still read as the fraction it is). The `Song` model carries it; the projection and the
verbs hand the song's value to the pass. The importer writes nothing (62-Q4).

### Phase 3 — The editor surface

The picker (62-Q2) in the Song Information dialog, an undoable edit, save-on-write like every other
song field. Every presented tail in the editor re-derives on the edit, on both surfaces.

### Phase 4 — Acceptance

The sanctioned bundle as separate invocations, plus the sightings: a fast song set to a quarter
(the Master of Puppets case that raised the question) and a slow one left at the default, on the 3D
board and the 2D lane; the census at the default unchanged to the row.

## 10. Runtime performance

One scalar read per presentation pass. Nothing per frame, nothing per note beyond the comparison
that exists today.

## 11. The rejected alternative: a tempo-derived bound

Designed and discussed 2026-09-08 after the user noticed that eighth tails look right at 75 BPM and
would look terrible at 212. The proposal: the bound becomes a duration in seconds, and each song
takes the smallest plain note value lasting at least that long at the song's dominant tempo (the
time-weighted median of its tempo map) — a quarter second puts eighths up to 120 BPM, quarters to
240, halves above. Song-scoped so a ramp cannot flip a note inside a song.

Rejected for now because the user judged the default right for most songs and the per-song value
is the simpler answer to the rest: it needs no threshold to tune, no median to define, and no
edge at 120 BPM where two songs a few beats apart look different for no reason a charter chose.
The measurement that shows the stakes stands: under the inclusive comparison the corpus's standing
tails tripled (52,990 → 167,819), which is what "tails on every eighth" costs at speed. If the
per-song value proves tedious to set, the tempo-derived rule is the DEFAULT this plan's absent
value could grow into, with the authored value as its override — and that is the order to build
them in, never the reverse.

## 12. References

- `rock-hero-common/core/include/rock_hero/common/core/chart/grid_arithmetic.h` — the constant and
  its doc block.
- `rock-hero-common/core/src/chart/chart_presentation.cpp` — rule 3, the one reader.
- `rock-hero-common/core/src/chart/chart_legato.cpp` — `chartResolutions`, the derivation every
  surface reads.
- `docs/plans/in-progress/chart-ruleset.md` — the dated bound rulings of 2026-09-07 and 2026-09-08.
