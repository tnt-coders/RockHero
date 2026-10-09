# Plan 67 — Nothing snaps to downbeats: mid-measure sections and derived short bars

**Status:** Phases 1–5 DONE 2026-10-09 (see their "As built" notes). The user's four projects
were converted 2026-10-09 (backed up beside each as `.bak`; Creed loads). Left for the user: the
Phase 5 sighting checklist.
Authored 2026-10-08 against `master @ 3915d023` plus the in-flight Note Lock change (round once from the lock's origin; content frozen while locked), after which this plan
is sequenced. Rulings A, B and C below are the user's (2026-10-08); the storage design is this
plan's recommendation, with its rejected alternatives recorded, revised the same day by §1a (the
file stores every position as a whole-note place, so a meter edit rewrites nothing but the meter)
— DECIDED by the user 2026-10-08, spelled as a canonical mixed number (`"11+3/4"`). §9 records the user's
decisions Q1–Q7. Supersedes plan 65's Q3 and Q4 and plan 66's Q2 and decision 6; the sighting of
plans 65 and 66 can run on this plan's result instead of theirs.

## Goal

A meter edit never refuses and never moves anything: the measures of the new meter fill the reign
as far as whole measures go, and whatever is left before the next meter is ONE short measure whose
meter is DERIVED and shown grayed, never stored; changing the meter back makes it vanish. A section
is a named instant anywhere on the grid, not a barline: a meter round trip restores every section
exactly, and the section verbs read the cursor's slot like every other marker verb. The terminal
anchor stops being a special anchor: the song may end on a short final measure, and the last
mechanism by which a meter edit could move a time (`reseatTerminalAnchor`) is deleted. One
consequence is a format change: every stored position, a time signature's included, is a
whole-note place, not a measure number or a measure:beat token (§1a), so a meter edit's diff is the
meter line alone.

## Non-goals

- No change to GP import's meters or sections: both come per master bar and already land on
  downbeats, so the importer's output is byte-identical (its census row pins that).
- No new marker kind, chord letter, or row; no change to the tempo verbs or the Note Lock beyond
  the one type they share with the meter edit.
- No board-side section mark on the 3D floor: that is the standing backlog item
  (`docs/developer/the-3d-highway.md`, "A song section boundary draws nothing on the floor"); this
  plan adds the 2D ruler mark the user asked for and keeps the 3D label where it already is.
- No migration code: the `measure` key is removed, not read alongside `at` (the no-legacy rule
  until the first release). The user's own projects are handled by §9's decision.

## Constraints

- (a) **Layering**: the model, the derivation and the rules are `rock-hero-common/core`; the
  verbs are `rock-hero-editor/core`; nothing here reaches `common/ui` except the ruler's chip row.
- (b) **One authority**: the short bar is derived in exactly one place, `TempoMap`'s reign table,
  and every consumer — grid lines, beat bars, token validity, downbeat queries, the metronome
  mirror, the census — reads it from there. No second statement of "how long is measure n".
- (c) **Illegal states unrepresentable**: a place on the grid has exactly one address. This is the
  constraint that shapes the two yields in §6.
- (d) **No legacy code**: the reader refuses `timeSignatures[].measure`; it does not translate it.
- (e) **Performance**: the reign table gains at most one segment per non-dividing reign (O(meters)
  at construction); no per-frame path changes; the whole-song transform loses work.
- (f) **Builds** through `.agents/rockhero-build.ps1`; each phase runs only the checks its changes
  warrant; clang-tidy only when the user asks. Each phase gets a simplicity review before commit.

## Current state (verified 2026-10-08 against `master @ 3915d023`)

- **Storage.** `TimeSignatureChange{int measure; TimeSignature meter}`
  (`rock-hero-common/core/include/rock_hero/common/core/timeline/tempo_map.h:56-72`), written as
  `timeSignatures[].measure` (`rock_song_package_write.cpp:153`, `file-formats.md:77-88`), read at
  `rock_song_package_read.cpp:282-339`. Rules: non-empty, first at measure 1, strictly increasing
  measures, positive numerator, power-of-two denominator the tick lattice holds
  (`tempo_map_rules.cpp:33-61`); the terminal anchor must be a downbeat (`:99-104`).
- **The one derivation.** `TempoMap::buildDerivedIndices` (`tempo_map.cpp:94-147`) turns the
  list into `SignatureSegment{start_measure, meter, start_beat_index, start_whole_note_position,
  start_seconds}`; every address query — `globalBeatIndex`, `beatAtGlobalIndex`,
  `wholeNotePositionAt`, `gridPositionAtWholeNote`, `quarterPositionAtBeatPosition`,
  `ForwardBeatTimeCursor` — reads segments. `timeSignatureAt(measure)` (`:304-323`) and
  `timeSignatureAtSeconds` (`:333-349`) are the two queries that still answer from the AUTHORED
  list (the second maps a segment index back onto it one-to-one, which the short bar breaks).
- **The transform.** `readdressTimeline` (`timeline_readdress.cpp:151-235`) takes `PlacedMeter`s
  (`timeline_readdress.h:29-44`: a whole-note place plus a meter — exactly the type the stored
  change should be) and derives measure numbers in `timeSignaturesOf` (`:59-103`), refusing
  `MeterOffDownbeat` when a reign is not a whole number of measures (65-Q3); snaps every section
  to `measureDownbeat(mapped(position))` (`:189-195`, 65-Q4); re-seats the terminal
  (`reseatTerminalAnchor`, `tempo_map_edits.cpp:167-182`, through `terminalCovering(0.0, ...)`,
  `tempo_map.cpp:218-289`, whose "a terminal left mid-measure always moves" clause exists for
  it). Its three drivers are `readdressUnderMeters` (`meter_edits.cpp`), `scaledTimeline`
  (`tempo_scale.h`, plan 66 decision 6 refuses a meter landing off a downbeat) and
  `timeKeptTimeline` (`keep_notes_in_time.cpp`, in flux).
- **Sections.** `validateSongSectionRules` (`song_section_rules.cpp:26-80`) refuses
  `NotAMeasureDownbeat`; the chord reads the tick and floors to the measure's downbeat
  (`section_handlers.cpp:44-65`); the insert floors the captured position (`:110-126`); the
  rename finds a section at the exact position (`:134-157`); `Alt+←/→` steps one measure
  (`:163-194`); the ruler's "Insert Section Here" reads the click at the tick
  (`timeline_ruler.cpp:583-595`, `:650-685`). GP import writes one section per labelled master bar
  (`gp_chart_builder.cpp:3244-3262`).
- **Section display.** 2D: the ruler's section chip row with a dotted leader down to the ruler
  body, where "the ticks take over as the position marks" (`timeline_ruler.cpp:344-349`,
  `:1260-1279`); the measure tick is promoted to full height (`:1249-1258`), which is the only
  position mark a section has in the body. The tab lane draws nothing for a section (verified:
  no section drawing in `rock-hero-common/ui/src`). 3D: the name floats at `section.seconds`
  (`highway_projection.cpp:533-548`, `highway_renderer.cpp:6182-6250`); the floor draws nothing
  (the promoted bar was removed on sight 2026-09-24).
- **Camera zones.** `highway_projection.cpp:602-649`: zones are runs of measures; "a section
  starting since the previous downbeat (mid-measure starts snap forward to this one) restarts the
  grouping" — a mid-measure section already cuts, at the NEXT downbeat.
- **The terminal.** `terminalGlobalBeatIndex` (`tempo_map.cpp:535-543`, "honest because the rules
  hold the terminal on a downbeat") feeds the grid walker's end (`tempo_grid_geometry.cpp:59`,
  `:211-217`), the highway's beat bars (`highway_projection.cpp:589-600`) and two tests.
- **Meter identity.** `TimeSignatureSelection{int measure}`, `SetTimeSignature{measure, meter}`,
  `timeSignatureTargetAt(map, measure)` (`time_signature_target.cpp`), the ruler's chips
  (`timeline_ruler.cpp:1233-1246`, `:472-477`, `:804`) and the row (`marker_row_handlers.cpp:51-59`,
  `:117-120`) all key a meter by the measure it starts.
- **Meters elsewhere.** `tempo_mirror.cpp:90-110` mirrors each change at its downbeat's quarter
  position; `gp_chart_builder.cpp:162-179` builds one change per master-bar meter change;
  `test_corpus_census.cpp:1626` pins the import's meter per bar.
- **Test fixtures** construct `TimeSignatureChange{.measure = ...}` at 165 sites in 29 files
  (`rg` count); `g_reseated_time_tolerance_seconds` (`song_timeline_fixtures.h:67`) exempts the
  re-seated terminal in `test_meter_edits.cpp` and `test_tempo_scale.cpp`.
- **In flight (not committed):** the Note Lock's origin and frozen content
  (`keep_notes_in_time.*`, `tempo_handlers.cpp`, `editor_undo_history.*`,
  `marker_model_commit.h`). This plan touches `keep_notes_in_time.cpp` only to drop
  `placedMetersOf`, so it builds after that change lands.

## The rulings (user, 2026-10-08)

Quoted so the plan cannot drift from them.

- **A — "SECTIONS MAY LAND MID-MEASURE."** "Drop `NotAMeasureDownbeat`; a meter edit no longer
  snaps a section to its measure's downbeat, so an A->B->A meter round trip restores every section
  exactly; the section INSERT acts at the cursor's slot like every other Ctrl+letter marker verb,
  Alt+arrows move a section by the grid step like other markers instead of one measure, rename acts
  at the cursor's slot; an off-downbeat section needs a 2D mark (today the 2D view relies on
  highlighting the downbeat's bar) and the same place for the 3D label (surfaces must not
  diverge); the 3D camera framing zones need a ruling for a mid-measure section. GP import is
  unaffected. The format doc change is approved; existing files stay valid."
- **B — "A METER EDIT NEVER REFUSES FOR A REIGN THAT DOES NOT DIVIDE."** "Fill as many whole
  measures of the new meter as fit; the LAST measure before the next meter change is a SHORT
  measure whose meter is DERIVED (shown grayed, like derived fret-hand positions) — never stored.
  Changing the meter back makes the short bar vanish by itself. Typing a meter onto the derived
  short bar authors it (mirroring a digit on a derived hand-position chip). A stored auto-inserted
  filler meter was rejected (it leaves debris on round trips)."
- **C — to evaluate:** "the terminal anchor could likewise end the song on a short final measure,
  deleting `reseatTerminalAnchor` and its whole-millisecond re-timing (the last way a meter edit
  can move a time)." Evaluated in §5: yes, and it is the simpler shape.

## 1. The storage question: how a meter change is stored

A measure number cannot say where measure n starts once the bar before it may be short, so
`timeSignatures[].measure` is dead. The candidates, judged on what stays invariant under each
re-address driver (a meter edit, a tempo edit, Scale Tempo, the Note Lock), readability and
hand-editability, whether every other stored position stays well-defined and cheap, how many code
paths change, and what an invalid file looks like.

### Candidate 1 — a grid token read under the meters before it (the first pass's file form; superseded by §1a)

`{ "at": "6:2", "numerator": 7, "denominator": 8 }`: the 7/8 begins at beat 2 of bar 6 as bar 6
was being counted — under the 4/4 before it — so bar 6 is cut short after one beat (a derived
1/4 bar) and the 7/8's first full bar is bar 7.

- *Invariance.* A meter edit keeps every meter's whole-note place; the token is the place read
  under the earlier meters, so it changes exactly when an earlier meter changes — the same rule
  every other token obeys through `readdressTimeline`. A tempo edit changes no address. Scale
  Tempo scales the place like every other. The Note Lock lays the same meters under new anchors;
  the token is untouched. So the meter becomes an ordinary re-addressed slice.
- *Readability.* One address grammar in the whole file: anchors, sections, notes and meters all
  say `m:b[+n/d]`. Hand-editable in musical terms. Where no bar is cut — every GP import, every
  meter authored at a barline — the token is the full grid's own address of the downbeat.
- *The cost, stated plainly.* Where a bar IS cut, the token is not the full grid's address of the
  place (that is `7:1`, which cannot carry the information: it is defined by the meter standing
  there). The token names the CUT — "bar 6 ends after beat 1" — which is the one thing the file
  must say and the one thing the full grammar cannot. The reader resolves each token under the
  reigns built so far; `TempoMap` already builds its table in that order, and the validity test is
  the one every position takes (`isValidGridPosition`) against the previous reign extended.
- *Other positions.* Unchanged: they resolve through the reign table as today.
- *Code paths.* The package reader and writer gain the token conversion (two small functions, one
  pair); everything in memory reads places (§2).
- *An invalid file.* `"at": "6:5"` under 4/4 — refused, "not a position of the 4/4 before it";
  `"at": "6:1+1/3"` — refused, the remainder is a third of a beat and no meter can state it (§2's
  meter lattice); a token at or before the previous meter — refused, unordered; a meter whose
  reign holds no whole bar — refused (§2). Every refusal is one the current rules already have a
  sibling of.

### Candidate 2 — a whole-note place from the song's start (RECOMMENDED for memory; for the file too once §1a weighed the ripple)

`{ "at": "21/4", ... }`, the axis `PlacedMeter` already uses inside `readdressTimeline`.

- *Invariance.* Identical to candidate 1 (both are the same place, written two ways).
- *Readability.* A second position grammar in the file, and an unmusical one: "21/4" is nothing a
  charter counts, and the editor would still show the chip at `7:1`. It is the only stored
  position that would not be a token, and the one a hand editor would most likely get wrong.
- *Code paths.* The fewest in memory: `TempoMap` is built from places directly, `PlacedMeter`
  and `TimeSignatureChange` are one type, `placedMetersOf` and `timeSignaturesOf` are deleted,
  Scale Tempo scales a field. This is why the IN-MEMORY type is the place (§2): the file's
  grammar and the program's axis need not be the same thing, and converting at the package
  boundary costs one function pair.
- *An invalid file.* A denominator that is not a power of two (`"21/5"`) or a descending place.
  Equivalent to candidate 1's, minus the "misread the previous meter" catch.

The strongest rejected alternative, because it is correctness-equivalent and simpler to read in
code; it loses only on the file, where one grammar and musical readability win.

### Candidate 3 — measure numbers plus a stored short-bar length (REJECTED)

`{ "measure": 7, "numerator": 7, "denominator": 8, "shortBarBefore": "1/4" }` or a length on the
previous change. Rejected concretely: the length is derivable from the two places and is therefore
a datum stored twice (the rule-stated-twice defect this repository has found to be a bug every
time); a meter edit must rewrite the length AND the next measure number by hand, two places that
must agree (change amplification); the round trip leaves the stored length as debris, exactly the
objection that killed the stored filler meter; and the invalid-file space grows a dimension (a
length that disagrees with the next measure number). It also still cannot say where a bar is cut
without the length, so it is candidate 1 with the information moved to a worse place.

### Candidate 4 — others considered

- *Reign lengths* (`{meter, bars}` per entry, the last open): unreadable in position terms, a
  meter edit rewrites the previous entry, and a short bar is not a count of bars. Rejected.
- *One "grid event" list merging anchors and meters* (an event at a token carrying an optional
  meter and an optional time): braids two independent decisions (where tempo is pinned, where the
  bar changes) into one record for the sake of a shared key. Rejected.
- *Store the meter at the first full bar's full-grid token (`7:1`)*: circular — the token is
  defined by the meter it would place. Rejected.

### Recommendation (first pass; REVISED by §1a the same day)

The first pass recommended the FILE store each meter change at a grid token read under the meters
before it (candidate 1) and the PROGRAM hold it at its whole-note place (candidate 2). The user then
asked for the ripple question to be evaluated in depth — a meter edit re-spelling every later
token in the file — and §1a's answer changes the file half: **the file stores EVERY position as a
whole-note place; the program keeps `GridPosition` in memory for now.** What stands from the first
pass: `PlacedMeter` folds into `TimeSignatureChange`, `placedMetersOf` and `timeSignaturesOf` are
deleted, and `TempoMap`'s reign table is the single derivation of every measure's length.

### 1a. Addendum — the ripple question (2026-10-08)

**The question.** Under measure:beat tokens, a meter edit at bar 12 re-spells the stored address of
every object after bar 12 — notes, sections, anchors, fret-hand positions, tone changes,
automation points and, under this plan, later meters — although the edit moves nothing. The user:
"IF there is an alternative format that does not ripple (potentially ALSO solving the ripple of
OTHER edits) then it should at least be considered seriously ... I want to know the genuine best
answer here." Evaluated on the merits below; the verdict follows the table.

**What ripples under each edit, by axis.** Every stored position is a point on exactly one of
three axes, and the edit kinds partition by which axis they hold fixed:

| Format of a stored position | Meter edit (keeps time and place) | Tempo edit, grid as truth (keeps place) | Note Lock tempo edit / Scale Tempo (keep TIME, rewrite places) | A note or marker move | Hand edit of one position | Exact |
|---|---|---|---|---|---|---|
| **Token `m:b+n/d`** (today) | every later token re-spelled | nothing | every later / every token re-spelled | one line | musical, natural | yes (rationals) |
| **Whole-note place** (`"47/4"`) | ONLY the meter line | nothing | every later / every place re-spelled — the real change | one line | arithmetic (bars → whole notes) | yes |
| **Tick count** (`180480`) | as places | nothing | as places | one line | opaque | yes |
| **Quarter-note count** | as places | nothing | as places | one line | 4/4 bars × 4 — familiar, meter-blind | yes |
| **Seconds** for content | nothing | EVERY later time re-written | nothing | one line | no musical meaning | no — ms rounding, and the grid-as-truth model is lost |
| **Section-relative tokens** | within the edited section only, and every token in a section whose start moves | nothing | as tokens | one line | two levels to resolve | yes |
| **Place + a bar:beat annotation** | the annotation ripples, so the diff does; two fields that must agree | — | — | — | — | rejected: a rule stated twice |

Reading the table: the Note Lock and Scale Tempo ripple in EVERY musical-axis format because they
rewrite the places themselves — that diff shows the content actually changing, which is what a
diff is for. A time-axis format would quiet those two and make every ordinary tempo edit (the
common edit, and "the grid is truth" is ruled) rewrite the whole song. So the only ripple a format
can remove is the meter edit's, and the only way to remove it is an address independent of meters:
a count on the whole-note axis (places, ticks or quarters are one choice spelled three ways). The
hybrid is the rule-stated-twice defect and section-relative tokens only move the ripple.

**The three spellings of the whole-note axis.** The program's axis is whole notes (`Fraction`;
durations are already written in it: `"sustain": "1/4"`), the tick is `1/3840` of it, and a quarter
is a quarter of it. A tick count is the least readable and bakes the lattice constant into every
file. A quarter count reads like MIDI beats and is the most familiar in 4/4, but it is a second unit
beside the durations' whole notes. The whole-note place spelled by the ONE existing rational grammar
(`formatFractionToken` / `parseFractionToken`: `"47/4"`, `"11"`) adds no grammar and no unit:
positions and durations are then the same kind of number in the same spelling. **DECIDED
2026-10-08 (user): if the file stores counts, the spelling is the canonical MIXED number** — the
whole part, then `+n/d` for a remaining fraction in (0, 1) in lowest terms, each part omitted when
zero (`"11+3/4"`, `"12"`, `"3/8"`, `"0"`); an improper spelling (`"47/4"`) is refused, so every
value has one spelling. It replaces the rational grammar for durations too (`"1+1/2"`, not
`"3/2"`), so the file still has ONE number grammar, and it echoes the tokens' `+n/d`. Whether the
file stores counts at all was **DECIDED 2026-10-08 (user): counts everywhere.** The deciding
reason is correctness, not speed or diff size: a bar:beat token means a moment only together with
every meter before it, so any change to a meter that is not followed by the editor's re-address —
a hand edit, a git merge of a meter change with note edits, an external writer — silently moves
every later object in time, while a count means the same moment whatever the meters say. The
cost accepted: reading an object's bar in the raw file takes arithmetic. The performance probe (2026-10-08,
`relwithdebinfo`, 50,000 notes) settles nothing either way: the file change alone is
runtime-neutral (load 1.5 ms faster, save 3 ms slower), and B' would save only the 3.5% of a
rebuild spent converting positions — `chartNotePlaces` already converts each note once per
revision, and no frame path converts at all. B''s one real gain is memory: a meter edit's undo
entry falls from ≈9.6 MB to kilobytes.

**Exactness and validity.** A place is a reduced rational on the tick lattice: `isOnTickLattice`
(the denominator divides 3840), non-negative, strictly before the terminal for every marker
(`markerCanStartAt`'s terminal half), strictly ascending where a list is ordered (anchors, meters,
sections, tone changes, fret-hand positions), the first anchor and the first meter at `0`, a meter's
place on the meter lattice (§2.1) and its reign holding a whole measure (§6 item 4). The
beat-within-its-measure half of today's `isValidGridPosition` has no reader-side meaning for a
place — a place is always inside some measure — so the reader's validity shrinks to the lattice
and the ordering rules; `isValidGridPosition` stays for the `GridPosition`s edits produce in memory.
An invalid file: `"1/7"` (off the lattice), `"-1/4"`, a marker at or past the terminal, a
descending list, a meter at `"1/12"`, a meter with no whole measure before the next. Every refusal
is one the reader states today in token terms.

**Hand-editability and diff-readability in practice.** A `git diff` of a project after a meter
edit is one line: the meter. After a note move, one line, `"at": "47/4"` → `"at": "95/8"`, which a
reader can check as "moved an eighth later". Fixing a note by hand means computing a whole-note
count — in 4/4, `bars_before + quarters/4`, with the trap that bars and beats are one-based and
places zero-based (bar 12 beat 3 is `11 + 2/4 = 23/2`). That is the genuine cost, and it is paid
rarely: the user's own estimate is that files will not be hand-edited much, while every meter edit
is a diff. Under tokens the trade is the reverse: a natural hand edit, and a meter edit whose diff is
every later note.

**The in-memory model.** The format decides nothing about memory. Two shapes:

- **B — places in the file, `GridPosition` in memory (this plan).** The package reader builds the
  grid-only `TempoMap` from the meter places first (as `gp_chart_builder.cpp:183` already does) and
  turns every other place into a `GridPosition` through `gridPositionAtWholeNote`; the writer runs
  `wholeNotePositionAt` the other way. Both are exact rationals, and both cost what today's reader
  already pays per object (`isValidGridPosition` → `isOnTickLattice` → `wholeNotePositionAt`), so
  the runtime is unchanged — plan 64's streaming reader can drive a forward segment cursor if it
  measures a need. Selections, marker identity, undo snapshots, the editor's addresses, every test
  fixture spelling `at("12:3")` and `readdressTimeline` are untouched: the meter edit still
  re-addresses content in memory, exactly as today, and the file simply does not show it.
- **B' — places throughout the program.** `ChartNote::position`, `FretHandPosition::position`,
  `SongSection::position`, `ToneRegion::start`, `ToneAutomationPoint::position`,
  `BeatAnchor::position`, every selection and action payload become `Fraction` places; the
  `GridPosition` becomes a presentation projection (ruler labels, the readout, prompts, the caret's
  text) computed by `gridPositionAtWholeNote` where shown. The payoff is real and large: a meter
  edit changes the tempo map and NOTHING else, so it commits like a tempo edit (`TempoMapSnapshot`)
  and the whole-song re-address and snapshot exist only for Scale Tempo and the Note Lock, which
  need them — plan 64's F9 (≈ 10 MB of undo per meter entry at 50k notes) disappears for meter
  edits; `advanceGridPositionByWholeNotes`, `gridDistanceInWholeNotes` and `measureLatticeAt`'s
  measure walk collapse into rational addition; the hand-position derivation's "one spelling for
  every instant" (`gridPositionAtWholeNote(place).measure`) becomes the only spelling anywhere. The
  cost is a program-wide refactor: `GridPosition` appears at 383 sites in 63 source files and 127
  sites in 28 public headers (`rg` counts, 2026-10-08), with token fixtures at 555 sites in 35 test
  files — mechanical in most places (a fixture helper parses `"12:3"` into a place through the
  fixture's map), but crossing every feature. It does not change the file: B' reads and writes
  exactly B's format.

**Cost of B (the file alone).** The package reader and writer (`rock_song_package_read.cpp`,
`rock_song_package_write.cpp`, `chart_document.cpp`: eleven token sites) convert at the boundary;
the chart rules' diagnostic messages keep printing tokens (they are for people). The GP importer
is unchanged (it builds `GridPosition`s in memory and the writer converts). Package and chart
document tests (`test_rock_song_package.cpp`, 43 token spellings; the document tests) move to
places; in-memory fixtures do not. The custom-song converter
(`C:\__MAIN__\Coding\__git__\custom-song-importer\tools\custom-song-converter.py`) already computes
places (`position.place`, `mapper.at_place(place)`) and writes tokens at five sites (notes `:1835`,
fret-hand positions `:1786`, sections `:1931`, anchors `:1954`, meters `:1950`); it writes the
place's fraction instead, and `validate-package.py` (which already has `meter.place(position)`)
validates places. The scorer and census read through the production reader. The user's projects
convert by one script that resolves every token through the file's own time signatures (the
converter repository's `TempoGrid` arithmetic in Python), per Q5's decision.

**How it interacts with this plan.** It makes the plan SMALLER. The one special case the first
pass carried — a meter's token "read under the meters before it", with its `tokenOfTimeSignature`
/ prefix-resolution pair (§2.3 as first written) — is gone: a meter is a place like every other
position, the reader validates it on the meter lattice and in order, and `TempoMap` is built from
places directly. The reign table (§2.2), the transform (§2.4) and every later section stand.

**Verdict.** The genuine best answer for the FILE is the whole-note place for every position,
spelled by the existing rational grammar, folded into this plan's Phase 1 so the format changes
once (confidence: high — it is the only format that removes the meter edit's ripple, it is exact,
it validates with fewer rules than tokens, it deletes this plan's one special case, and the
converter already works on places). The best answer for the PROGRAM is B', recorded as its own
follow-on plan that depends on this one's format and not the reverse (confidence: high on the
shape, medium on the timing — it is a program-wide refactor whose payoff, the meter edit as a
map-only edit, is worth it on its own and does not gate anything the first release needs). **The
strongest argument against B:** the file stops being musically readable, and a person fixing a
position by hand must compute a whole-note count with a one-based/zero-based trap; a mixed spelling
would soften it at the price of a second grammar. The user's own ruling tips the balance: hand
edits will be rare, and a one-line diff for a meter edit is worth more than a natural spelling for
an edit nobody makes often. The strongest argument against B' now: 383 source sites across every
feature, concurrently with other work on the same files.

## 2. The model

### 2.1 The stored meter change

```cpp
/*! \brief A time signature that governs from a place on the whole-note axis. */
struct TimeSignatureChange
{
    /*! \brief Whole notes from the song's start to the first downbeat it governs. */
    Fraction place{};
    /*! \brief The meter that governs from that place on. */
    TimeSignature meter{};
};
```

Rules (`validateTempoMapRules`): at least one; the first at place 0; places strictly ascending;
each place on the METER LATTICE — its reduced denominator is a representable meter denominator
(`isRepresentableDenominator`, the same power-of-two-dividing-the-tick-quantum rule the meter's
own denominator obeys; today that is a whole number of 1/256 notes); each reign holds at least one
whole measure of its meter (§6); numerator and denominator as today. The terminal downbeat rule
(`TerminalOffDownbeat`) is deleted (§5).

### 2.2 The reign table: the one derivation of every measure's length

`TempoMap::buildDerivedIndices` walks the changes with the terminal's place as the last end. For a
change at place P with meter n/d whose reign ends at E (the next change's place, or the terminal's
for the last reign):

- `k = floor((E − P) / (n/d))` whole measures of n/d, one segment as today;
- `r = E − P − k·(n/d)`; if `r > 0` and the reign ends at a METER, one more one-measure segment,
  the DERIVED SHORT BAR, with meter `{ r·D, D }` where `D = max(d, denominator(r))` — the
  governing beat where the remainder is a whole number of them (4/4 cut after a beat is 1/4; 7/8
  cut after four eighths is 4/8), and the finer power of two where it is not (7/8 re-metered to 3/4
  leaves an eighth: 1/8). Both are powers of two, so `max` is the lcm, and the meter lattice rule
  guarantees `r·D` is whole;
- if `r > 0` and the reign ends at the TERMINAL, no segment: the last measure is the governing
  meter's and the terminal stands inside it (§5, §6).

The segment gains one field, `derived`, and the table is published as
`TempoMap::reigns()` → `MeterReign{int first_measure; Fraction place; TimeSignature meter; bool derived;}`,
which is what the ruler's chip row, the marker row, the Tracktion mirror, the chord target and
the validator read. `timeSignatureAt(measure)` answers from the table (today it walks the authored
list), and `timeSignatureAtSeconds` maps its segment to the segment's meter rather than to an
authored index. Nothing else in `TempoMap` changes shape: `globalBeatIndex`, `beatAtGlobalIndex`,
`wholeNotePositionAt`, `gridPositionAtWholeNote`, the quarter conversions and the forward cursor
already read segments, so the short bar is honoured everywhere by construction — grid lines
(`MeasureGridWalker` reads `timeSignatureAt(measure)` per measure), the tick lattice and
`isValidGridPosition` (`beatsPerMeasureAt`), downbeat queries (`measureDownbeat` is a spelling;
whether a position IS a downbeat is a comparison), both surfaces' beat bars
(`beatAtGlobalIndex`), the metronome mirror (iterates `reigns()`), GP import (unchanged input,
`audibilityHorizonFrom` reads `timeSignatureAt`), and the census and scorer (production reader).
The one place is the reign table; the audit in §3 lists every consumer that reads the AUTHORED
list today and must read the table instead.

Measure numbering: the short bar is a measure and takes a number; the next meter's first full bar
is the one after it. `firstRenumberedMeasure` keeps working unchanged (it compares downbeat
places).

### 2.3 The place at the package boundary (revised by §1a)

Every `at` in the package — a meter, an anchor, a section, a note, a fret-hand position, a tone
change, an automation point — is a whole-note place spelled as the canonical mixed number §1a
decided (`"11+3/4"`, `"11"`, `"3/8"`, `"0"`; an improper `"47/4"` refused), and so is every
duration (`sustain`, keyframe offsets), so the file keeps one number grammar: the existing
`formatFractionToken` / `parseFractionToken` pair becomes that grammar for every caller. The reader reads the meters first, as places, validates them (§2.1) and builds the
grid-only `TempoMap` from them; every other place is validated on the tick lattice and turned into
the in-memory `GridPosition` through `gridPositionAtWholeNote`. The writer runs `wholeNotePositionAt`
the other way. No position is read under a prefix of anything: a meter is a place like every other,
which is what deleted the first pass's token-under-the-previous-meters rule. The chart rules'
diagnostics keep printing tokens — they are for people, and the editor's addresses are tokens.

### 2.4 The transform

`readdressTimeline` takes `std::span<const TimeSignatureChange>` placed meters (its `PlacedMeter`
parameter under the type's new name) and `PlacedAnchor`s as today; it builds the new
`TempoMap{meters, anchors}` directly — `timeSignaturesOf` and its refusal are gone, the reign
table doing the derivation — maps every slice through the content map as today, and maps a
section like every other position (the snap is gone). `reseatTerminalAnchor` is gone. The result
is validated by the caller as today, and the only refusal a meter edit can now meet is a reign
with no whole measure (§6), reported through the tempo-map rules naming the meter.
`ReaddressErrorCode::MeterOffDownbeat` and `ReaddressError` leave with it unless Scale Tempo keeps
a refusal of its own (§6, item 3).

### 2.5 Sections

`SongSection{GridPosition position; std::string name}` is unchanged. Rules: a non-blank name,
`markerCanStartAt(position, map)` (on the grid, strictly before the terminal — the shared place
rule, already asked), strictly ascending. `NotAMeasureDownbeat` is deleted, and so is the
"Sections must stand on a downbeat" clause everywhere it is restated (§10). Every existing file
stays valid under this rule: a downbeat is a grid position like any other, so the section half of
the format change refuses nothing a reader accepts today (the meter key is the separate matter of
§9 Q5).

Verbs, per the marker grammar's one precedence: `Ctrl+M` reads `cursorPosition(placement quantum)`
— the slot an arrow press would arm on, as the tone and tempo chords do — restates (renames) a
section standing EXACTLY there, else inserts there. `Alt+←/→` moves a section by one line of the
placement quantum (`nearestTempoGridPosition` / the step the tone region's start takes), refused
onto another section or past the end. The ruler's "Insert Section Here" reads the click at the
placement quantum through the same `clickedGridPosition` the tempo insert uses
(`tempoInsertPositionAt`), so the two inserts share one rule and the tick-quantum branch of
`clickedGridPosition`'s comment goes. GP import is unchanged.

### 2.6 The meter verbs

`Ctrl+/` keeps today's precedence over a different set of row objects: the row's objects are every
reign start, authored and derived (`reigns()`), identified by their first measure, so
`TimeSignatureSelection{int measure}` and the ruler's `m_selected_signature_measure` survive. On
an authored start the chord RESTATES; on a derived short bar's start it AUTHORS — a stored change
is inserted at that place with the typed meter, which is the FHP row's "a stating verb on a
derived position first authors it" in meter form; where no reign starts it INSERTS. `Delete` and
`Alt+←/→` on a derived start are refused with the red glow, as on a derived position. Where the
chord reads the cursor is §9 Q2.

## 3. Every consumer that changes

Grouped by the decision each hides; file:line is today's.

**The type and its file form**

- `tempo_map.h:56-72` `TimeSignatureChange` → `{Fraction place; TimeSignature meter}`;
  `timeline_readdress.h:29-44` `PlacedMeter` deleted into it.
- `rock_song_package_read.cpp:282-339` reads each meter's `at` as a whole-note place and refuses
  `measure`; `rock_song_package_write.cpp:153, 368-374` writes the place through
  `formatFractionToken`, which (with `parseFractionToken`) now spells and reads the canonical
  mixed number for places and durations alike. Every other `at` in the reader, the writer and `chart_document.cpp`
  (eleven token sites) converts between place and `GridPosition` through the grid-only map (§2.3).
- `gp_chart_builder.cpp:162-179` accumulates places (the sum of the bars before each change)
  instead of measure numbers; `:183` `grid_only` unchanged in role.
- `tempo_map_rules.cpp:33-61` the §2.1 rules; `:99-104` terminal downbeat rule deleted;
  `TempoMapErrorCode::TerminalOffDownbeat` deleted, `InvalidTimeSignature` widened or split (the
  builder's call; the message names the meter and the rule).

**The reign table and its readers**

- `tempo_map.cpp:94-147` the short-bar segment; `:304-349` `timeSignatureAt` and
  `timeSignatureAtSeconds` read segments; `reigns()` added.
- `tempo_mirror.cpp:90-110` iterates `reigns()`, each at its first downbeat's quarter position
  (`quarterPositionAt(measureDownbeat(reign.first_measure))`, the conversion it uses today), so a
  derived short bar reaches the host as a time signature too.
- `marker_row_handlers.cpp:51-59` row starts = `measureDownbeat(reign.first_measure)` for every reign;
  `:117-120, :159` unchanged in shape.
- `timeline_ruler.cpp:1233-1246` chips from `reigns()`, a derived one drawn as a ghost (the FHP
  chip's precedent, `FhpViewState` "the editor draws a derived position's chip as a ghost");
  `:472-477, :804` select by `reign.first_measure`.
- `time_signature_target.cpp` "a meter stands here" = a reign starts at this measure; a derived
  start publishes the restate target that authors (§2.6).
- `meter_handlers.cpp:144-180` the restate-or-author-or-insert over places; `:185-219` delete
  refuses a derived start; `:228-293` the move refuses a derived start, and its neighbour/end
  checks stay on the whole-note axis as written.
- `song_timeline_snapshot.cpp:24-34` `normalize` dedupes equal adjacent meters by place (as now,
  on the new field); `:50-69` `focusReplacing` keys the changed record by the reign's first measure.
- `meter_edits.cpp` `readdressUnderMeters` passes `tempo_map.timeSignatures()` directly;
  `placedMetersOf` deleted. `tempo_scale.cpp` scales `place`. `keep_notes_in_time.cpp:28-33`
  passes `tempo_map.timeSignatures()` (after the in-flight change lands).

**The terminal (ruling C, §5)**

- `tempo_map_edits.cpp:167-182` `reseatTerminalAnchor` deleted with its header block;
  `timeline_readdress.cpp:182-184` builds the map directly.
- `tempo_map.cpp:218-289` `terminalCovering`: the "a terminal left mid-measure always moves" clause
  and its `on_downbeat` gate go; growth still walks whole measures to the first downbeat covering
  the time (a grown terminal lands on a downbeat; one left mid-measure by a time-holding edit
  stays).
- `tempo_map.cpp:535-543` `terminalGlobalBeatIndex` → `terminalGridPosition` (already in
  `grid_arithmetic.h:182`) is the terminal's address; the three readers compare against it:
  `tempo_grid_geometry.cpp:59, 72-75, 211-217` (the walker's `valid()` compares
  `gridPosition() <= terminal` exactly instead of integer beat units; `moveToLastLine` finds the
  last line at or before the terminal inside its measure), `highway_projection.cpp:589-600` (every
  whole beat at or before the terminal), `test_chart_caret.cpp:269, 417`,
  `test_highway_projection.cpp:655`.
- `song_timeline_fixtures.h:67` `g_reseated_time_tolerance_seconds` deleted; every `reseated ?`
  branch in `test_meter_edits.cpp` and `test_tempo_scale.cpp` with it.

**Sections (ruling A)**

- `song_section_rules.h:26-27, 62-66`, `song_section_rules.cpp:42-54`: the downbeat rule deleted.
- `section_handlers.cpp:44-65, 110-126, 163-194`: the slot, the exact-position restate, the
  placement-quantum step. `timeline_ruler.cpp:583-603, 650-685`: the insert at the clicked slot.
- `timeline_readdress.cpp:189-195`: the snap deleted; `.sections` mapped like every slice.
- `highway_projection.cpp:602-649`: the camera-zone cut per §9 Q1.
- `timeline_ruler.cpp:344-349, 1260-1279`: the section leader runs to the ruler floor (§4).
- `chart_fret_hand.cpp:753-767` and the hand-position ruleset: unchanged — rule 3 breaks at bar
  lines only (ruled; measured 88.06% on bars alone), and sections were never an input. Plan 60a's
  Phase 1 text justifies that by "a section always starts on a downbeat"; the justification becomes
  the ruling, the behaviour does not change.

**Tests that flip** (named so the phase that owes each is clear): `test_meter_edits.cpp:550`
("refuses a reign that leaves a later meter mid-measure" → lands with a derived short bar), `:576`
("re-seats the terminal" → leaves it, the last bar short), `:604` ("snaps a section" → keeps it),
`:625` ("Sections snapping onto one downbeat" → deleted), `:653-682` (three terminal re-seat cases
→ deleted), `:295` (the property test's two exemptions → none; zero tolerance);
`test_tempo_scale.cpp:100, 168` (`MeterOffDownbeat` → §6's refusals); `test_editor_controller_meters.cpp:423`
("carries the cursor onto its note in a re-seated span" → deleted); `test_tempo_map_rules.cpp`
(terminal downbeat case → deleted; token and reign cases added); `test_rock_song_package.cpp`
(`measure` fixtures → `at`, and a `measure` key refused); the section-rule tests
(`NotAMeasureDownbeat` → a mid-measure section accepted); `test_highway_projection.cpp:683-730`
(zones with a mid-measure section per Q1); `test_tempo_mirror.cpp` (a short bar mirrored);
`test_tempo_grid_geometry.cpp` (lines inside a short bar; a mid-measure terminal).

## 4. The UI pieces

- **The derived short-bar chip.** The signature row draws a derived reign's chip as a ghost with
  its derived meter's text ("1/4", "4/8"), selectable like any chip; typing a meter on it authors
  it; `Delete` and `Alt+←/→` on it glow red. The look is the FHP ghost chip's
  (`drawTabSpanChip` / the ruler's chip alpha), so no new visual vocabulary; if the ruler's chip
  row cannot carry the ghost treatment legibly at chip size, the ui-design-expert decides the
  treatment before Phase 3 ships.
- **The off-downbeat section mark (2D).** Today a section's body mark is the promoted measure
  tick under its leader. A mid-measure section has none. The section leader runs through the
  ruler body to its floor in the section colour — one rule for every section, downbeat or not —
  and the measure tick stops being a section's mark by coincidence. The tab lane draws nothing
  new. Treatment (width, alpha against the promoted measure tick) is the ui-design-expert's; the
  position is the section's instant.
- **3D.** The label already stands at `section.seconds`, which is the section's instant — the
  same place as the 2D chip and leader. Nothing moves; the floor stays empty (backlog item).
- **The renumber flash** is unchanged: a short bar is a measure.

## 5. Ruling C evaluated: the terminal ends the song where it stands

Yes. With the reign table deriving every measure's length from places, a terminal mid-measure
needs no rule: the last reign's whole measures are counted, and the terminal stands inside the
measure after them. What that deletes: `reseatTerminalAnchor` and its header; the
`TerminalOffDownbeat` rule; the "always moves" clause of `terminalCovering`;
`terminalGlobalBeatIndex` and its honesty comment; `g_reseated_time_tolerance_seconds` and every
test branch keyed on a re-seat; the round-trip property's last exemption. What it means: a meter
edit, Scale Tempo and a Note Lock edit move NO time at all — the property test asserts every
slice's seconds equal to within floating noise, the terminal's included. What stays: typed-BPM
growth (`terminalCovering`) still extends by whole measures to a downbeat, so a grown terminal
is never mid-measure; a terminal left mid-measure by a time-holding edit stays there until the
charter moves it or grows it. Why the last bar is a CUT and not a derived segment is §6 item 2.

## 6. Where simplicity yielded to correctness

1. **The short bar before a meter is a derived SEGMENT, not merely a cut.** The simpler shape —
   "the bar simply ends where the next meter starts, no segment" — leaves every position in the
   cut-off part of bar 6 with two addresses (`6:2` and `7:1`), and the invariant that a place has
   one address is what every token comparison, every marker identity and the lattice rounding
   rest on. The derived segment makes `6:2` invalid (`beatsPerMeasureAt(6)` is 1), so the place has
   exactly one name.
2. **The final bar IS a cut, not a segment.** Nothing stands at or past the terminal, so no second
   address exists, and the terminal's own address (`n:3` in memory, read in the full grammar like every
   anchor) stays a valid position of its measure — whereas a derived final segment of two beats
   would make `n:3` unaddressable. The asymmetry is forced by the two facts, not chosen.
3. **Meter places live on the meter lattice** (a power-of-two denominator the tick quantum holds,
   today 1/256), because item 1's short bar must be a meter: a remainder of a third of a beat has
   no whole-beat count. The rule reuses `isRepresentableDenominator`, so it is one statement.
   Consequence for Scale Tempo: a factor whose reduced denominator is not a power of two (4/3,
   2/3) can carry a meter off that lattice; the factor is REFUSED naming the meter (replacing plan
   66 decision 6's refusal with a narrower one), because rounding an authored meter's place
   differently from the content beside it would leave a note that stood on the downbeat a tick
   off the barline. The TERMINAL is not a meter and has no short-bar meter (item 2), so it needs
   no lattice rule and Scale Tempo never refuses for it — which removes the regression that
   refusing for the terminal would be against today's re-seat.
4. **A reign must hold one whole measure.** The user's rule fills whole measures and leaves a
   short bar; a reign shorter than one measure of its meter has no whole measure to fill, and its
   authored chip and its derived short bar would stand at one downbeat — two chips, one place. It
   is refused, naming the next meter ("the 9/8 at 6:1 has no whole measure before the 3/4 at
   7:1"). It is the one refusal a meter edit keeps, and it is a tempo-map RULE (the validator and
   the verb ask the same function), not a transform special case.
5. **The file's address stops being musical** (§1a). The simple alternative — the file speaks the
   charter's own `m:b` — makes every meter edit rewrite every later line of the file, and the one
   position that defines the grammar after it (a meter) could only be read under a prefix. The
   whole-note place costs hand-editability, which is rare, to buy a one-line diff and one reading
   rule for every position.

Looking harder, as the charter asks: items 1–4 all descend from one datum, "a bar's length", being
derived from two places. The deeper shape that would remove them from the PROGRAM is §1a's B': a
grid with no measures at all in memory — positions as whole-note places, bars a display
projection — which this codebase has been moving toward (durations are note values; the transform
speaks places; this plan's file speaks places). It is recorded in §7 as the follow-on plan; this
plan takes the step that falls out of the rulings (meters placed, one reign table, places in the
file) and leaves `GridPosition` as the program's and the charter's address.

## 7. Dependencies and sequencing

- **After** the in-flight Note Lock change (origin and frozen content) commits: it edits
  `keep_notes_in_time.*` and `tempo_handlers.cpp`, which Phase 2 touches.
- **Plans 65 and 66** are built; this plan supersedes 65-Q3, 65-Q4, 66-Q2 and 66 decision 6, and
  their sightings run on this plan's result. Plan 65's "Non-goals: no chart format change"
  is overtaken by ruling A's approval of the format change.
- **The custom-song converter** (`custom-song-importer`, outside this repo) writes
  `timeSignatures[].measure` and tokens at five sites; it must write whole-note places (§1a, which
  it already computes) before the FHP scorer or the census reads its output again.
- **A follow-on plan — "a position is a number" (B' in §1a)** — moves the program itself onto
  places and makes a meter edit a map-only edit; it depends on this plan's format and is not a
  dependency of it. Record it in the roadmap when this plan's Phase 1 lands.
- **Plan 59 (count-in)** reads "the cursor's own measure downbeat"; a short bar is a measure, so
  nothing special is owed. **Plan 52 (range edits, 52-Q2)** decides its clipboard basis with the
  same place-not-measure philosophy.
- **Plan 64**: the transform does less; no new whole-song cost.

## 8. Phased implementation

Every phase: failing tests first, a simplicity review before its commit, a build and the touched
suites. New files follow the placement procedure; comments follow
`docs/design/documentation-conventions.md`.

### Phase 1 — Placed meters, places in the file, the reign table

- `TimeSignatureChange{place, meter}`; `PlacedMeter` deleted; `reigns()` with the derived short
  bar; `timeSignatureAt` / `timeSignatureAtSeconds` from segments; the §2.1 rules; GP import's
  meter places; the mirror over `reigns()`; the package reader and writer and the chart document
  speaking whole-note places for every `at` (§2.3), `measure` refused; the in-memory
  `TimeSignatureChange` fixtures rewritten (a fixture helper taking a token, `meterAt("12:1", 7, 8)`,
  resolved under the fixture's earlier meters, keeps the tests readable); the package and document
  tests' spellings moved to places; the number grammar's tests pin the canonical mixed spelling
  (`"11+3/4"` round-trips, `"47/4"` and `"0+3/4"` are refused, durations spell the same way).
- **Failing tests first:** a map with a meter at place `21/4` under 4/4 derives bar 6 as 1/4 and
  the 7/8 from bar 7, with `beatsPerMeasureAt(6) == 1` and `6:2` not a valid grid position;
  `4/4 → 7/8` after four whole notes derives 4/8, `7/8 → 3/4` after one bar derives 1/8; a reign
  with no whole measure is refused naming the meter; a meter place off the meter lattice is
  refused; a package round-trips every `at` place byte-identically, refuses `measure`, a place off
  the tick lattice and a descending list, and reads the same `GridPosition`s the token form read
  (a fixture of each kind); the GP census meter row is unchanged; the mirror carries a short bar as
  a time signature; the walker's lines inside a short bar stop at its end; a saved project's diff
  after a meter edit touches the meter line alone (a property over random songs: the written
  document differs only in `tempoMap.timeSignatures`).
- Behaviour change visible to the charter: none (nothing produces a short bar yet); the format
  changes once, here, so Q5's conversion runs after this phase.
- **As built (2026-10-09).** `timeSignatureAtSeconds` was dead and is deleted; `timeSignatureAt`
  returns the governing `MeterReign`. The short bar's beat is `lcm(d, den(r))`, the `max` above for
  powers of two. A reign under one measure refuses as its own `TempoMapErrorCode::ReignTooShort`.
  `parsePlaceToken` refuses an off-tick-lattice place at admission, because the normalizer runs
  before the rules. The grammar is the formatter's fixpoint, so grid tokens' `+n/d` is canonical
  too (`"4:1+2/4"` now refuses). The fixture helper is a list builder, `metersAt({...})`.
  `timeSignatureText` moved to common core for the rules' messages. The new document property
  still exempts section lines and the terminal line until Phase 2 deletes the snap and re-seat.
  The conversion script is `C:\__MAIN__\Coding\__scratch__\plan67-convert\convert_places.py`.

### Phase 2 — The transform moves no time; the terminal stands where it lands

- Delete `timeSignaturesOf`, `MeterOffDownbeat`, the section snap, `reseatTerminalAnchor`, the
  `terminalCovering` mid-measure clause, `TerminalOffDownbeat`, `terminalGlobalBeatIndex` (→
  `terminalGridPosition`), `g_reseated_time_tolerance_seconds`; Scale Tempo's refusal narrowed to
  §6 items 3 and 4.
- **Failing tests first:** "Meter edits keep every object's time over random songs" with ZERO
  tolerance and no exemptions; an A→B→A round trip restores every token, section and meter
  exactly; a non-dividing meter edit lands with a derived short bar and refuses nothing; a meter
  edit with no later meter leaves the terminal where it stands and the last bar short; nothing
  about any note's time changes (the controller test over both arrangements); Scale Tempo x2 /2
  round trips exactly, 4/3 with a meter off the lattice refuses naming it, 4/3 on a single-meter
  song lands; the Note Lock's `timeKeptTimeline` unchanged in behaviour (its suite green).
- **As built (2026-10-09).** `NotAMeasureDownbeat` went here, not in Phase 4: with the snap gone,
  the commit's section validation would otherwise refuse a non-dividing meter edit over a section,
  against ruling B. It folds with `SectionPastTerminalAnchor` into `OffGridOrPastTerminal`, the
  one place rule. The section VERBS still place on downbeats until Phase 4. `ReaddressError` is
  deleted (the transform refuses nothing); Scale Tempo's refusal is a message string, through
  `isOnMeterLattice`, which the validator shares. `runBase` is deleted, because a meter move's
  re-address is exactly invertible. The grid walker floors the terminal into its integer units,
  so a mid-measure terminal costs no per-line rational compare. Times now hold bit-exactly.

### Phase 3 — The derived chip and authoring onto it

- The ruler's signature row from `reigns()`, derived chips as ghosts; the row's objects every
  reign start; `Ctrl+/`, `Enter` and the right-click Set on a derived start author it; `Delete`
  and `Alt+←/→` on it glow red; `focusReplacing` by first measure.
- **Q2 (b), the meter verbs at the beat.** `Ctrl+/` and the ruler's Add read the BEAT the cursor
  is in rather than the measure downbeat, and `Alt+←/→` moves an authored meter by one beat of
  the meter before it. Off a downbeat the chord inserts a meter there and the bar it cuts becomes
  the derived short bar (cursor on `6:3` under 4/4, type 7/8: bar 6 is a derived 2/4, the 7/8
  begins at bar 7). On a reign start it restates as today. This reopens 65-Q2: the keymap
  matrix's "measure downbeat" quantum for the meter row becomes "beat", in the same change set.
- **Failing tests first:** the row walks a derived start; a stating verb on it inserts a stored
  meter at its place and the derived chip becomes authored (one undo entry); Delete and the move
  refuse it with a flash and no entry; changing the authored meter back makes the short bar
  vanish; the ghost pixels (the ruler's chip test pattern); `Ctrl+/` on `6:3` inserts at place
  `5+1/2` and derives bar 6 as 2/4; a cursor between beats inserts at the beat it is in;
  `Alt+→` steps an authored meter one beat and a step that leaves a reign under one measure
  refuses (`ReignTooShort`); the ruler's Add at a mid-measure click inserts at that beat.
- ui-design-expert consulted if the ghost treatment does not read at chip size (§4).
- **As built (2026-10-09).** The ghost is the hand-position ghost's own mechanism
  (`ScopedTransparencyLayer` at `g_ghost_opacity`, now exported from `tab_paint_core.h`) and reads
  at chip size, so no expert pass was needed; the selection ring and refusal glow stay solid. The
  meter targets and `SetTimeSignature` carry a `GridPosition`; `beatStart` is the one beat rule,
  and the action asks `timeSignatureTargetAt` for its verb, so the chord, the ruler's Add and the
  commit share one rule. Every refused meter verb on a selected chip flashes it, through a new
  `IEditorView::flashTimeSignatureRefusal`. The move keeps its neighbour check as the transform's
  precondition (an unordered list would trip the transform's place assertion before the rules
  run). Sighting: a derived reign's dotted leader draws in the full signature colour.

### Phase 4 — Sections off the downbeat

- Rules, chord at the slot, rename at the slot, the placement-quantum step, the ruler insert at
  the slot, the camera-zone cut per Q1, the section leader to the ruler floor.
- **Failing tests first:** the section rules accept `6:2+1/2` and still refuse the terminal and a
  repeat; `Ctrl+M` at a mid-measure slot inserts there and a second press renames it; `Alt+→`
  steps one placement line; a meter round trip restores a mid-measure section exactly; the zone
  walk cuts where Q1 rules; the ruler draws a section tick at a mid-measure instant (pixel test);
  the 3D label's seconds equal the section's.
- The keymap matrix, grammar and developer-guide rows (§10) in the same change set.
- **As built (2026-10-09).** The rules needed nothing (Phase 2's `OffGridOrPastTerminal`); the
  Phase 4 test is their first direct suite, `test_song_section_rules.cpp`. `Ctrl+M` reads
  `cursorPosition(placementQuantum())`, the tempo chord's read, and publishes
  `InsertSectionTarget{position}` (the `downbeat` field renamed); the insert stores the position it
  is handed. `Alt+←/→` steps through `steppedNudgePosition`, the tone-region start's step, and the
  verb refuses the origin collapse itself as the fret-hand move does, since the commit would record
  it as a move that changed nothing. The ruler's two slot inserts share one `insertSlotAt`
  (`tempoInsertPositionAt` renamed); `clickedGridPosition` keeps a tick branch for the METER insert,
  which Phase 3 put at the beat, so its comment was rewritten rather than cut. The zone walk cuts
  on `seconds < measure_end − ε`. The 2D mark needed no drawing change: the chip and its leader
  already stood at the section's seconds, and the measure tick was never a section's mark, so the
  pixel test pins the leader at a mid-measure column with nothing in the body beneath it. The 3D
  label needed none either. The random-timeline fixture now stands half its sections on beat 2, so
  the time-holding and round-trip properties cover mid-measure sections.

### Phase 5 — Docs, the converter, the user's projects, the sighting

- `docs/design/architecture.md` and the developer guide per §10; plans 65 and 66 marked
  superseded where this plan overrides them; the converter's `at` change (other repo); the user
  runs the §9 Q5 conversion of their projects.
- **Sighting:** author a non-dividing meter mid-song on a populated chart and watch the short bar
  appear grayed with nothing moving in sound; change it back and watch it vanish; type a meter on
  the ghost and see it turn solid; put a section on beat 3 and confirm the ruler mark and the 3D
  label agree; Scale Tempo x2 and /2 on a song with sections and meters round-trips.
- **As built (2026-10-09).** The design documents and the developer guide were brought to the
  current truth in the phase that changed each (§10's note); plans 65 and 66 carry superseded
  notes at 65-Q2/Q3/Q4, 66-Q2 and 66 decision 6; the roadmap marks this plan DONE with its Q
  list decided, records the §7 follow-on as `docs/plans/todo/a-position-is-a-number.md`, and
  corrects plans 65's and 66's status rows (both built 2026-10-04). The converter
  (`custom-song-importer`, working tree only) writes every `at` and every duration through ONE
  helper, `mixed_number`, the meter and anchor places read off the grid mapper's beat places;
  its `GridPosition` is gone (a place is the one address, so the class was a wrapper around one
  field); `validate-package.py` parses the canonical spelling through one fixpoint parser,
  refuses the `measure` key, an off-meter-lattice meter, a reign under one measure, and accepts a
  mid-measure section and terminal; the FHP harness's `rock_package.py` derives each measure from
  the placed meters exactly as the reign table does, short bar included; `timing-gate.py`
  re-derives note times from places. Hand-checked over a synthetic 3/4→4/4 grid (the §8 Phase 5
  report). The user's own projects are still to be converted (§9 Q5's script).

- **Sighting checklist (the user's).** Open a GP-imported package with sections and at least two
  meters, a populated chart, grid snap ON at 1/4, and the 3D preview (`F3`) beside the 2D view.
  1. **The derived short bar.** Pick a reign of several 4/4 bars that ends at a later meter. Walk
     onto its meter chip (`Ctrl+Shift+/`, or `↑` from the tab lane), `Enter`, type `3/4`. Expect:
     the measures after it renumber and flash once; a GHOST chip reading `1/4` stands on the last
     bar before the next authored meter, drawn at the hand-position ghost's opacity, while its
     dotted leader down to the ruler body is in the FULL signature colour (Phase 3's note — judge
     whether that reads as intended or the leader should ghost too); play through the reign: every
     note sounds exactly where it did, and the 3D preview's beat bars and measure labels agree
     with the ruler.
  2. **It vanishes.** `Enter` on the authored chip, type `4/4`: the ghost is gone, the numbers are
     back, and every section in the reign stands where it stood (read its token in the readout
     before and after; an A→B→A round trip restores it exactly). Undo and redo both ways.
  3. **Typing onto the ghost authors it.** Redo step 1, walk or click onto the ghost chip, `Enter`,
     type `1/4`: the chip turns solid, one undo entry; save and open `song.json`: one more
     `timeSignatures` entry whose `at` is a mixed-number place (`"16+3/4"`-style), no `measure`
     key anywhere. `Delete` and `Alt+→` on a ghost chip glow it red and change nothing.
  4. **A mid-measure meter typed on a beat.** Put the cursor on beat 3 of a 4/4 bar (`→` at 1/4,
     or a ruler click), `Ctrl+/`, type `7/8`. Expect: that bar becomes a ghost `2/4`, the `7/8`
     chip stands at beat 3's instant and the next bar is the first full 7/8 bar; nothing moves in
     sound. Right-click the signature row at a mid-measure beat: "Add Time Signature..." is
     offered there. `Alt+→` on the new chip steps it one beat of the 4/4 before it. Undo.
  5. **A section on beat 3.** Cursor on beat 3 of a bar holding no section, `Ctrl+M`, type a
     name. Expect: the section chip stands at beat 3 with its dotted leader to the ruler body at
     that column, nothing drawn in the body beneath it; pause the 3D preview there: the floating
     name is at the same instant as the 2D cursor. `Alt+→` moves it ONE placement line (a beat at
     1/4, an eighth at 1/8 — never a measure); `Alt+←` back; `Ctrl+M` on it again opens the
     rename; `Alt+→` onto another section refuses with the flash.
  6. **Camera framing at a mid-measure section.** In the 3D preview, play across the beat-3
     section: the framing zone changes at the DOWNBEAT of the bar the section starts in (67-Q1
     (a)), so the damped camera has settled when the section's first notes arrive; there is no
     second cut at the section's own instant.
  7. **A meter round trip over that section.** With the beat-3 section inside a reign, do steps
     1–2 on that reign: the section's token is identical afterwards.
  8. **Scale Tempo x2 then x1/2.** Edit → Scale Tempo..., `2`, then `1/2`: every meter chip,
     anchor, section and note is where it was (tokens and playback identical). Then `4/3`: it is
     refused naming the first authored meter whose place is not a multiple of 3 whole notes (that
     place × 4/3 leaves the meter lattice); on a single-meter song it lands.
  9. **The terminal mid-measure.** On the LAST reign (no later meter), change its meter so the
     terminal falls inside a bar (4/4 → 3/4 on a reign whose length is not a multiple of 3/4):
     the song ends on a short final bar with no ghost chip, the grid lines stop at the terminal,
     nothing moves in sound. Undo.
  10. **Save, reopen, diff.** Save; reopen: identical. In `song.json` and the chart documents every
      `at`, `sustain` and `offset` is a mixed number (`"0"`, `"12"`, `"3/8"`, `"11+3/4"`); a
      `git diff` of a saved project after one meter edit touches only the `timeSignatures` lines.

## 9. Open questions for the user

- **Q1 — the camera zone for a mid-measure section.** (a) Cut at the downbeat of the measure the
  section starts IN, so the framing is in place when its first notes arrive (one comparison
  changes: `section.seconds < measure_end`); (b) keep today's cut at the NEXT downbeat (the
  section's first partial bar framed under the old zone); (c) cut at the section's own instant
  (zones leave the measure walk; more machinery). **R: (a).** **DECIDED 2026-10-08: (a)** — also
  the better framing: the damped camera has settled by the time the pickup's notes arrive.
- **Q2 — where the meter chord reads the cursor.** (a) Keep the measure downbeat (`Ctrl+/` and
  the ruler's Add floor to the measure; `Alt+←/→` one measure, 65-Q2 as ruled); (b) the BEAT the
  cursor is in, and `Alt+←/→` by one beat of the meter before it — a meter starts a bar and a bar
  is whole beats, so this never leaves the meter lattice, and it is the pickup-bar workflow (put
  the cursor on beat 3, `Ctrl+/`, type 7/8: bar 6 is a derived 2/4 and the 7/8 begins on 7);
  (c) the placement slot like every other chord, refusing a slot off the meter lattice (a triplet
  slot). **R: (b)**; it reopens 65-Q2 and the matrix's "measure downbeat" quantum for the meter
  row, so it is the user's. **DECIDED 2026-10-08: (b)** — the exact slot would refuse a triplet
  slot and mint bars like 9/16 nobody meant; the beat keeps every short bar a meter.
- **Q3 — the derived chip's text.** The remainder over the governing beat where whole ("4/8"),
  else the finer power of two ("1/8") — §2.2's formula — or always lowest terms ("1/2" for 4/8)?
  **R: the formula**; it keeps the beat the charter was counting. **DECIDED 2026-10-08: the
  formula** — lowest terms would misstate the beat the bar is counted in.
- **Q4 — the 2D section mark's treatment** (the leader through the ruler body, §4): sign the
  position rule here and leave the look to the ui-design-expert, or sight a mock first?
  **R: sign the rule, sight the look in Phase 4.** **DECIDED 2026-10-08:** the section chip
  stands at its own place with the dotted leader drawn down from it exactly as the other marker
  rows already draw theirs (the tempo row places anchors off the downbeat today); no new treatment.
- **Q5 — the user's existing projects** (`timeSignatures[].measure`, which converts exactly to a
  downbeat token). (a) Re-import from GP — loses any authored sections, positions or chart edits
  those projects carry; (b) a one-off script in the scratch root, run by the USER on their own
  files (agents never touch the folder), rewriting `"measure": n` to `"at": "n:1"` — exact under
  any meters, no product code, the pattern plan 60a used for its key rename; (c) the reader
  accepts both keys for one release — legacy code, against the standing rule. **R: (b).**
  **DECIDED 2026-10-08: (a) or (b), and the user asked the agent to run the script for them** — so
  in Phase 5 the agent writes it, confirms with the user, backs up every file it rewrites, and runs
  it; no dual-key reader. Under §1a the script converts EVERY `at` token (anchors, sections, notes,
  fret-hand positions, tone changes, automation points) and the `measure` key to whole-note places,
  resolving each token through the file's own time signatures — the converter repository's
  `TempoGrid` arithmetic, which already computes places, is the natural home for it.
- **Q6 — Scale Tempo's lattice refusal** (§6 item 3): refuse the factor naming the meter (R), or
  round the meter's place onto the meter lattice and report it like a lattice rounding? **R:
  refuse**; a silently moved authored meter is the dishonest shape. **DECIDED 2026-10-08:
  refuse**, and the build adds a watch item (`docs/tracking/watch-items.md`) whose trigger is a
  refusal that reads as wrong in use.
- **Q7 — a reign shorter than one measure of its meter** (§6 item 4), the one refusal a meter
  edit keeps and a departure from the literal "never refuses": (a) refuse, naming the next meter;
  (b) allow it, the whole reign being one derived short bar whose ghost chip stands at the same
  downbeat as the authored chip it belongs to (two chips, one place, the authored meter governing
  no whole measure). **R: (a).** **DECIDED 2026-10-08: (a)** — the case is an authored meter whose
  own reign, up to the next authored meter, is shorter than one of its bars.

## 10. Documentation to change (proposed wording, written before the build)

**As built (Phases 1–5, 2026-10-09).** Every document below was edited in the phase that changed
what it describes, and the design documents state the current truth without the history. Where
the proposals below disagree with the build, the build is right: the file's number grammar is the
canonical MIXED number (`"16+3/16"`, never the improper `"131/8"` or `"47/4"` written below); the
anchor example's `"33:1"` is `"31"`, not `"30"` (bars 1–16 of 4/4 reach 16, bars 17–24 of 7/8
reach 23, and bar 33 is the ninth 4/4 bar after that: 23 + 8 = 31); the 2D section
mark needed no drawing change (the chip and its leader already stood at the section's instant);
and the sections' rules are a non-blank name, on the grid strictly before the terminal
(`OffGridOrPastTerminal`), strictly ascending. Plan 65's Q2–Q4 and plan 66's Q2 and decision 6
carry superseded notes; the roadmap marks this plan DONE and its Q list decided.

**`docs/design/architecture.md`, Song Data Model** — replace the warp-anchor paragraph's "time
signatures are stored as changes (carried forward) ... A start anchor (measure 1, beat 1) and a
terminal anchor at the one-past-content downbeat are always required" with:

> The tempo map is a **warp-anchor grid**: time signatures are stored as changes at places on the
> whole-note axis (carried forward), and time is pinned only on a sparse set of **anchors** (a
> place — any tick — with an absolute time). A start anchor at `0` and a terminal anchor after
> every piece of content are always required; the terminal may stand mid-measure, in which case the
> song ends on a short final measure. A time signature may begin mid-measure of the meter it
> replaces; the measure it cuts is then one short measure whose meter is derived from the remainder
> and never stored. Every stored position — a note's onset, a section, a tone change — is a place
> on the same axis, written as whole notes in the one rational spelling durations use (`"47/4"`),
> so a meter edit rewrites nothing but the time signature itself; measures and beats are how the
> editor shows a place, never how the file stores one.

and the JSON example's `"measure": 17` → `"at": "16"`, `"measure": 25` → `"at": "23"` (7/8 bars
from 16: 16 + 8 × 7/8 = 23), anchors `"1:1"` → `"0"`, `"17:1"` → `"16"`, `"17:2+1/2"` →
`"131/8"`, `"33:1"` → `"30"`, and the chart paragraph's "Positions ... are bar addresses in the
grid-token grammar" → "Positions and durations are both whole-note rationals; the grid-token
grammar (`"m:b+n/d"`) is the editor's address of a place, not the file's".

**`docs/developer/file-formats.md`, tempoMap** — the rows:

> | `timeSignatures[].at` | string | req | Whole-note place, a canonical mixed number (`"16"`,
> `"11+3/4"`) of the first downbeat it governs; the first is `"0"`; strictly ascending; on the meter
> lattice (a whole number of the finest beat a meter may state, 1/256 note), so the measure it cuts
> short is itself a meter; each time signature governs at least one whole measure. |
> | `anchors[].at` | string | req | Whole-note place on the tick lattice (a denominator dividing
> 3840); the first is `"0"`; strictly ascending with the seconds. |

and the anchor invariants lose "the terminal anchor must land on a downbeat with no sub-beat
offset". Every other `at` in the format (sections, notes, fret-hand positions, tone changes,
automation points) reads "whole-note place on the tick lattice" in place of "grid token", with one
paragraph under the tempoMap table stating the spelling, the lattice and that a diff of a meter
edit touches only the meter. The sections row: strictly before the terminal anchor; the paragraph
below drops the downbeat clause.

**`docs/developer/musical-time.md`** §"The TempoMap": time-signature changes "at grid positions,
each read under the changes before it"; a new short paragraph on reigns and the derived short bar
as the one derivation; the transform paragraph loses "a reign that is no longer a whole number of
measures refuses ... a section left mid-measure snaps ... a terminal left mid-measure re-seats"
and gains "a reign that is not a whole number of measures ends in a derived short bar; nothing
moves in time".

**`docs/developer/the-editor-2d-views.md`** `:95`, `:136-148`, `:217-223`; **`the-3d-highway.md`**
`:293-304` ("Sections snap to measure downbeats" → "A section stands at any grid position; its
name marks it on the board and the ruler marks it in the body"); **`keyboard-input.md`**
`:271-279`; **`docs/plans/in-progress/keymap-matrix.md`** rows `:338` (quantum → grid slot),
`:340` (per Q2), `:369` (one grid step), `:397`; **`marker-verb-grammar.md`** `:56` and
`:133-137`. **`docs/plans/roadmap/60a-fret-hand-positions.md`** Phase 1's "a section always starts
on a downbeat" sentence reads as "the port breaks at bar lines only (ruled)".

## Final acceptance

As separate invocations: a build, the touched suites, `pre-commit run --all-files`; clang-tidy
when the user asks. The property suite green with zero time tolerance and no slice exempt. Manual
script (local corpus, never CI): open a GP-imported package with sections and meters; change a
meter to one that does not divide its reign; confirm playback is unchanged, the short bar is
grayed, the measures renumber, and a section inside the reign has not moved; undo; change the
meter back and confirm the short bar is gone; save, reopen, and confirm the package reads back
with `at` places and a meter edit's diff touches only the meter line. The census's meter row and
the FHP scorer re-run on the converter's place-based output.

## Rollback/abort notes

- Phase 1 is the format change: once a package is saved with `at`, the previous reader refuses it.
  Land Phase 1 only with the user's §9 Q5 answer in hand.
- Phases 2–4 are format-neutral; reverting them restores the refusal, the snap and the re-seat
  without touching saved packages.
- If the one-address invariant (§6 items 1–2) cannot be made provably green in Phase 1's tests,
  STOP and re-raise the model rather than ship a short bar two tokens can name.
