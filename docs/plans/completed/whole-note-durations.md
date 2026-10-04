# Whole-Note Durations

**Status:** COMPLETE 2026-10-04. Approved by the user the same day.
- Phase 0 is `99e4e7e5`.
- The one-time sweep converted 10 packages, verified exact under the new reader: every ring end
  and keyframe instant at the identical position and second, and repairs identical where the old
  reader already repaired. 54 other packages were already unloadable and need a re-import
  anyway. The backups are under `__scratch__/whole-note-sweep-backup/`.
- Phase 1, with Phases 2 and 3 folded in, is `987b62cf`.
- The `ChartShape` places commit is `f5769734` and `76460d58`.
- Phase 4 (prose) is `52334932`.
- Phase 5: the external converter was brought fully current in its working tree (uncommitted, per
  the rule). All six sample conversions load cleanly.
- The no-trace checklist returns zero outside history. Every remaining "beat" in code is an
  address, a time, or an explanation of why durations left beats.

A note's ring (`sustain`) and a
keyframe's distance from its onset (`offset`) change from BEAT counts to WHOLE-NOTE fractions, in
memory and in the chart document together. The change must be COMPLETE: no reader, writer, name,
comment, test or document keeps the beat meaning. Every phase passes a simplicity-expert review
before its commit, and the expert guides the implementation.

## Why

- **Correctness, ruled twice.** A first music-notation review and a second, adversarial one (Fable,
  2026-10-04) both rule whole notes MORE correct. Every notation format stores a duration as a
  meter-independent note value and a position as an address: MusicXML divisions per quarter, MIDI
  PPQ, Guitar Pro note values, MuseScore whole-note fractions. MEI does exactly this split,
  `@tstamp` in beats beside `@dur` as note values. A beat count re-means itself when a denominator
  changes: one dotted quarter is `"3/2"` in 4/4 but `"3"` in 6/8, and a ring crossing 4/4 → 7/8 has
  no single unit at all.
- **The codebase already routes around beats.** The lattice, the move verb, Scale Tempo and the
  tempo axis are whole-note proportional (`musical-time.md`, `grid_arithmetic.h`). Several sites
  exist only to convert: `carriedOffset`, `authoredRing`, `minimumSlideWindowRing`,
  `tailRevealLeadBeats`, `reexpress`'s beat body, and the importer's second axis. Two axes agreeing
  by hand is the project's named defect class.
- **Performance (measured, 2026-10-04, relwithdebinfo, ~48k notes):**
  - per edit and per revision: none, within ±6 ms;
  - meter edits and Scale Tempo: ≈ −60 % per whole-song verb (≈ 60 → 20 ms per arrangement);
  - Keep Notes in Time: ≈ −23 %.
  
  Two guard rails keep the rebuild from regressing: whole-note places are computed once per pass,
  never per pair, and `TempoMap` gets a direct whole-note → seconds query.

## The rule this writes into the format

Positions are bar addresses (`"m:b+n/d"`, n/d a fraction of the beat). Durations are note values
in whole notes (`1/4` a quarter, `3/8` a dotted quarter, `1/6` a quarter triplet), whatever the
meter. Things inside a note are relative to the note. Time is seconds. After this change the whole
format holds exactly those four kinds and states each unit (audited 2026-10-04: song.json carries
positions and seconds only; the chart carries positions plus these two durations).

## Rulings (user, 2026-10-04)

- **Keep the key names.** `sustain` and `offset` still tell the truth. A sustain is the ring, and
  an offset is a displacement from the onset; neither names a unit. Code identifiers that would lie
  are renamed or deleted (Phase 0 / Phase 2).
- **Old packages are converted ONCE by a throwaway sweep** that is never committed. No version
  bump: `formatVersion` stays `1`, and no legacy reading path is left in the code.
- **A threshold's unit follows what it protects** (ruled 2026-10-04, revising the first answer of
  "4/4 equivalents" for one of the two). A limit on what a player can see or play is in SECONDS:
  `g_minimum_kept_sustain_seconds` 0.25, `g_minimum_sustain_distance_seconds` 0.05, and now the slide
  window. A musical quantity is a note value: stored durations, `g_pick_slide_default_sustain_whole_note`,
  and the sustain score.
  - `g_minimum_slide_window` (the shortest span a glide, slide-out or scrape leg may take) becomes
    a seconds minimum. Like `marginBefore`, it is converted to a lattice length at the place it
    applies and rounded UP onto the tick lattice so the stored offset stays valid. Its value is set
    from the corpus in Phase 1, near today's length at typical tempos (≈ 50–60 ms), and the import
    census records what moved.
  - `sustain_score_per_beat{25}` becomes `sustain_score_per_whole_note{100}`, which is 25 per
    quarter note in every meter. It stays musical, because it rewards held music rather than
    elapsed time: per-second scoring would pay a slow song more for the same note. This matches the
    per-beat sustain scoring of the baseline game feel.
- **The external custom-song converter is brought fully current**: every spelling RockHero refuses
  today, plus the new unit. This is a working-tree change only and is never committed.

## Phases (every commit green)

The site-by-site inventory is in the impact analysis (session scratchpad
`whole-note-durations-impact.md`). Re-derive it with `rg` if it is lost.

### Phase 0 — unit-neutral names (no behaviour change)

- `parseBeatFractionToken` / `formatBeatFractionToken` become `parseFractionToken` /
  `formatFractionToken`. One grammar now serves two units: a grid token's `+n/d` is a beat
  fraction, a duration a whole-note fraction.
- One `g_chart_format_version` constant is read by both the reader's gate and the writer (today two
  literals agree by hand). Its value stays 1.
- Pins: every suite unchanged.

### The sweep (between Phase 0 and Phase 1; uncommitted; the editor is closed)

- **Roots:**
  - the `.rhp` suffixes in `%APPDATA%\Rock Hero\Rock Hero Editor.settings`;
  - `C:\Users\tjwro\OneDrive\Desktop\` recursively (53 `.rhp`, 4 `.rock` found);
  - `C:\__MAIN__\Coding\__scratch__\rockhero-*` (4 `.rhp`, 4 `.rock`).
  
  Converter output is re-converted by the updated converter instead.
- **Conversion, exact under the old meaning:** `s' = wholeNotePositionAt(old_end) −
  wholeNotePositionAt(onset)`, where `old_end` is today's `advanceGridPosition(onset, s)`. The same
  applies to every keyframe offset. Every stored duration is a difference of two lattice places, so
  nothing rounds.
- **The rewrite edits text in place.** The writer's one-note-per-line shape is rewritten token by
  token; any line that doesn't match it is refused and reported, never guessed. Every other entry
  is copied byte for byte, through a staged `.saving` file renamed over the original.
- **Backups:** every touched package and the settings file are copied first, to
  `C:\__MAIN__\Coding\__scratch__\whole-note-sweep-backup\`, keeping each original path. The sweep
  refuses to run if a backup already exists.
- **Verification:**
  - The old build writes per-note sidecars holding the old ring-end and keyframe positions and
    their seconds.
  - After Phase 1, a throwaway test loads every converted package through the NEW reader and
    asserts exact position equality and seconds equality (`WithinULP`), with no repair notes.
    Nothing opens in the editor before it passes.

### Phase 1 — the unit flip, atomic

ONE commit across common/core, editor/core and game/core. A surviving beat-axis reader would
misread silently, so none may be left in between.

- **Docs:** the `chart.h` and `fraction.h` docs, `ChartShape`, `ChartPresentation`, `ChartPathTail`,
  and every `\param` that says beats.
- **Readers:** every one moves to `wholeNoteDistance` / `advanceGridPositionByWholeNotes`:
  - rules, including the lattice check, which simplifies to a denominator test on the duration;
  - presentation, shapes, span cover, fret hand, legato, the editor verbs and selection keys.
- **Projection seconds:** these resolve through whole-note places via the new direct query. They
  are not a beat-axis double add, which would be wrong across a meter change.
- **The reveal lead** goes onto the whole-note axis. This also fixes today's lead, which reads the
  onset's denominator across a meter change.
- **Deleted:** `carriedOffset`, `authoredRing` and `minimumSlideWindowRing`. A whole-note duration
  is invariant under a whole-note move.
- **`timeline_readdress`:** `reexpress` becomes pure whole-note content arithmetic, so a meter edit
  no longer touches durations.
- **Importer:** its exit writes whole notes. Its internal axis waits for Phase 3.
- **Score math and slide window:** the 4/4-equivalent constants.
- **One minimum-sustain-distance authority:** `HeadMargins` and `minimumSustainDistanceBeats`
  currently compute the same expression; keep one.
- **Fixtures and pins:** all of them. The meter-edit tests flip to "durations untouched".
- **New pins:**
  - a ring and a keyframe across a 4/4 → 6/8 change keep their whole-note value under a meter edit,
    and their seconds;
  - `ring_end_seconds` across a meter change;
  - `1/3840` is accepted and `1/7` refused in every meter.
- **Developer docs** land with the code: `file-formats.md`, which states both units and the rule
  above and fixes the stale `slideOut` row; `musical-time.md`; `the-editor-2d-views.md`; and the
  agent briefs that say "in beats".

**Structural correction (simplicity guidance, 2026-10-04):** Phases 2 and 3 fold INTO Phase 1.
- **The importer and the census have to flip in Phase 1.** The importer hands its notes to shared
  predicates mid-build (`arrivesIntoNextHead`, the scoop, the slide window) while its own
  `ringEndOf` adds a beat place to `sustain`, so no exit point lets one unit hand over to the other.
- **Phase 1's FIRST edit deletes the beat-duration functions and renames every beat-axis field**,
  so the compiler lists every reader. The key names stay, so a mixed-unit sum is otherwise
  invisible to it.

The target shape:
- **Passes reason in whole-note PLACES:** one `chartNotePlaces` column, computed once per pass as
  the first sub-pass of the chart resolutions. A duration is a place difference, never a per-pair
  `wholeNoteDistance`.
- **`TempoMap`** gains `gridPositionAtWholeNote`, `secondsAtWholeNote` and
  `wholeNotePositionAtSeconds`, plus one quarter-position query. Every chart instant, onsets
  included, resolves its seconds through `secondsAtWholeNote`.
- **`marginBefore` generalizes into one seconds→lattice function,** `tickPlaceReached`, which
  rounds away from its origin. The head margin and the slide window are one-line bindings of it.
- The full brief is in the session scratchpad, `whole-note-durations-design-brief.md`. Its
  no-trace checklist is the plan's final sweep.

### Phase 2 — delete the beat duration axis (folded into Phase 1)

- **Delete:** `advanceGridPosition` and `beatDistance` (each internal use moves to its whole-note
  twin), `tailRevealLeadBeats`, and their tests.
- **Keep:** the beat-ADDRESS machinery (`MeasureLattice`, `beatsFromDownbeat`, `globalBeatIndex`,
  the seconds-interpolation beat axis). Those are positions and times, not durations.

### Phase 3 — the importer on one axis

- Guitar Pro's native unit is whole notes, so `duration_whole` flows straight into `sustain`.
- **Delete:**
  - `globalBeatAtWhole`, `wholeAtGlobalBeat`, `measureIndexAtGlobalBeat` and `first_global_beat`;
  - the per-denominator conversions and scalings;
  - `roundOntoTickLattice`'s re-measurement.
- Mirror the change in the census's `MeasureTable`.
- Pins: `test_gp_song_importer` re-expressed. The census pins are re-signed by a user-run corpus pass.

### Phase 4 — plans and registries (prose only)

Update the plans and registries that still describe beat durations:
- plans 65 and 66 (the re-expression);
- plan 52 (its cross-signature paste problem dissolves);
- plan 40 and plan 10;
- `watch-items.md`, `backlog.md`, `editing-interaction-model.md`;
- the stale example in `note-format-and-tablature-plan.md`.

The architecture doc's Song Data Model states the unit (user sign-off: this approval).

### Phase 5 — the converter (sibling repo, working tree only)

Bring `custom-song-converter.py` and `validate-package.py` fully current:
- whole-note `sustain` and keyframe `offset`, with `sustain` always written;
- the `keyframes[]` payload in place of `slides` / bend arrays;
- `palmMute` / `dead` in place of `mute`;
- `emphasis` in place of `accent`;
- the `vibrato` width words;
- no `chords` / `shapes`.

Then re-convert its samples and confirm RockHero loads them.

### Final sweep for traces

`rg -i "beat"` over every touched area, with each remaining hit justified as a POSITION or TIME
meaning (or deleted), plus `rg "in beats|beat count|beat-fraction|duration_beats|global_beat"` over
code, docs, tests and `.claude/`. The plan closes only when that list is empty or every hit is an
address.
