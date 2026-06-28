# Tempo Map and Chart Event Format — Implementation Plan

Status: in progress (planning). Defines the durable storage model for the **tempo map**, **chart
events** (notes and chords), and **chord templates**. This revision supersedes the earlier
`note_events` model (`measure`/`beat`/`offset`/`durationBeats` flat notes). The tempo-map warp-anchor
model itself is unchanged; what changed is how positions are spelled, how sustains are expressed, and
how notes and chords are modeled. The design was reached across an extended format discussion; this
document is now the source of truth for the format.

## Goal

Make the **tempo map (a beat grid) the source of truth for note positioning.** The grid is authored
to match the fixed backing recording, and chart events are positioned relative to it (bar / beat /
fractional offset) — the Guitar-Pro-against-a-backing-track model. Events bake to absolute seconds at
load for the highway and scoring, but the **stored truth is grid-relative**.

## The format at a glance

Song-level musical truth lives in `song.json`; the playable chart lives in each per-arrangement
document.

`song.json` (shared by all arrangements):

```json
{
  "tempoMap": {
    "timeSignatures": [
      { "measure": 1,  "numerator": 4, "denominator": 4 },
      { "measure": 17, "numerator": 7, "denominator": 8 },
      { "measure": 25, "numerator": 4, "denominator": 4 }
    ],
    "anchors": [
      { "position": "1:1",  "seconds": 1.840 },
      { "position": "17:1", "seconds": 28.057 },
      { "position": "33:1", "seconds": 48.500 }
    ]
  }
}
```

Per-arrangement document (templates + chart):

```json
{
  "chordTemplates": [
    { "id": "E5", "name": "E5", "voicing": [
        { "string": 6, "fret": 0 },
        { "string": 5, "fret": 2, "finger": 1 },
        { "string": 4, "fret": 2, "finger": 1 } ] },

    { "id": "Am-1", "name": "Am", "voicing": [
        { "string": 5, "fret": 0 },
        { "string": 4, "fret": 2, "finger": 2 },
        { "string": 3, "fret": 2, "finger": 3 },
        { "string": 2, "fret": 1, "finger": 1 },
        { "string": 1, "fret": 0 } ] },

    { "id": "Am-2", "name": "Am", "voicing": [
        { "string": 6, "fret": 5, "finger": 1 },
        { "string": 5, "fret": 7, "finger": 3 },
        { "string": 4, "fret": 7, "finger": 4 },
        { "string": 3, "fret": 5, "finger": 1 },
        { "string": 2, "fret": 5, "finger": 1 },
        { "string": 1, "fret": 5, "finger": 1 } ] },

    { "id": "Am7", "name": "Am7", "voicing": [
        { "string": 5, "fret": 0 },
        { "string": 4, "fret": 2, "finger": 2 },
        { "string": 3, "fret": 0 },
        { "string": 2, "fret": 1, "finger": 1 },
        { "string": 1, "fret": 0 } ] },

    { "id": "C6", "name": "C6", "voicing": [
        { "string": 5, "fret": 0 },
        { "string": 4, "fret": 2, "finger": 2 },
        { "string": 3, "fret": 0 },
        { "string": 2, "fret": 1, "finger": 1 },
        { "string": 1, "fret": 0 } ] }
  ],

  "events": [
    { "start": "1:1", "chord": "E5" },
    { "start": "1:2+1/2", "chord": "E5" },
    { "start": "1:4", "end": "2:1", "chord": "G5" },

    { "start": "2:1",     "string": 1, "fret": 12, "note": "E5" },
    { "start": "2:1+1/3", "string": 1, "fret": 15, "note": "G5" },
    { "start": "2:1+2/3", "string": 1, "fret": 12, "note": "E5" },

    { "start": "3:1", "end": "3:4", "chord": "Am-1",
      "strings": [ { "string": 2, "end": "3:2+1/2", "techniques": { "vibrato": true } } ] },

    { "start": "3:4", "end": "4:2", "chord": "Am-2" },

    { "start": "5:1",  "end": "5:3",  "chord": "Am7" },
    { "start": "13:1", "end": "13:3", "chord": "C6" },

    { "start": "16:1", "end": "18:3", "chord": "E5",
      "strings": [ { "string": 6, "techniques": { "palmMute": true } } ] },

    { "start": "17:7", "end": "18:1", "string": 1, "fret": 7, "note": "B4" },

    { "start": "20:1", "end": "20:5", "string": 2, "fret": 9, "note": "G#/Ab4",
      "techniques": { "bend": "1" } }
  ]
}
```

## Design decisions (durable)

1. **The grid is the source of truth for note positioning.** Notes are authored grid-relative; a
   misaligned grid is a *defective chart* fixed by aligning the grid (events follow). Scoring accuracy
   is gated on grid accuracy — an accepted trade, paid back by editor grid-alignment tooling, not by
   runtime decoupling.

2. **Positions are tokens.** A grid position is a single string token, not an object:
   `"<measure>:<beat>"` or `"<measure>:<beat>+<fraction>"`. `measure` and `beat` are 1-based integers;
   the optional sub-beat `offset` is an exact reduced `Fraction` in the open interval `(0, 1)`, joined
   with `+` ("beat 2 plus a half"). A zero offset is omitted (`"12:1"`, never `"12:1+0"`). `:`
   separates the integer grid address; `+` attaches the fractional refinement. This follows the
   existing fraction-as-string convention (`"3/16"`): an address is one atomic musical value, so it is
   one token.

3. **Events carry `start` and an optional `end`, both grid positions.** A sustain endpoint is just
   another grid address resolved by the same `secondsAt` path as the start — start and end are the
   same kind of object, mirroring how every beat in the grid resolves through one function. This
   replaces `durationBeats`: an `end` is meter-agnostic (it reads cleanly across a meter change),
   whereas a beat-count duration entangles the endpoint with the intervening meter. **`end` omitted =
   non-sustained** (the previous `"0"`-duration sentinel is gone).

4. **One `events` array holds notes *and* chords.** Each entry is one timed playable event at one
   onset, discriminated by form:
   - **single note** — inline `string` + `fret` (+ derived `note` label).
   - **chord instance** — a `chord` reference to a template id, plus an optional `strings` array of
     per-string deviations.
   An entry is not a "note" in the `.chart` sense (one fret per row); it is a note-or-chord composite,
   so the array is named `events` and the core type is `ChartEvent`.

5. **A chord is an explicit template + instance**, not an emergent same-onset cluster (this reverses
   the earlier emergent-chord rule). A `chordTemplates` library defines reusable voicings; a chord
   instance references one by id and stores only what deviates per occurrence. Fret/finger come
   entirely from the template — never duplicated on the instance.

6. **Per-string deviations are `end` and `techniques` only.** A chord instance's `end` is the default
   for every struck string; a `strings[]` entry overrides `end` and/or adds `techniques` for one
   string. There is **no** per-instance refingering and **no** per-instance name override — a
   different fingering or a different name is a different template (see decision 8).

7. **Chord templates are per-arrangement.** Templates reference instrument-relative string/fret
   addresses, so they belong to one arrangement's instrument context, not the song. Import collisions
   across arrangements are impossible by construction. Cross-arrangement sharing (a future editor
   palette, or an optional song-level library) is a deferred, additive option, not built now.

8. **A template's identity is `(name, frets, fingering)`** — two templates are distinct iff any of the
   three differ. Same frets + different name → two templates (e.g. `Am7` and `C6`). Same name + frets +
   different fingering → two templates. Each is a first-class reusable library entry because such
   variants recur within a song.

9. **Chord template ids are the chord name, with ordinal suffixes on collision.** `id` = the chord's
   `name`; if two templates share a name, all members of that name-group take `-1`, `-2`, … in order of
   first appearance in the events. `name` stays the display label; the ordinal lives only in the id.
   The id is a **file-internal link, regenerated on every whole-document write** — it is not a durable
   external reference. This trades durable-id stability for readability, and it is acceptable precisely
   because regenerate-on-write makes any rename/reorder churn a consistent whole-file rewrite rather
   than a broken reference. (Validation enforces unique ids within an arrangement and that every event
   `chord` resolves to a template.)

10. **Single-note events carry a derived `note` label** (display only). `note` is a pure function of
    `(string, fret, tuning)`, **writer-owned and regenerated on every write**, never read back as
    authoritative — `string`/`fret` remain the single source of truth and what scoring uses. The label
    makes single notes as self-describing as named chord events. Conventions:
    - octave included, scientific pitch, **sounding** pitch (middle C = C4);
    - the five accidental pitch classes show **both** enharmonic spellings with one shared octave
      (`C#/Db`, `D#/Eb`, `F#/Gb`, `G#/Ab`, `A#/Bb`); naturals stay single (`E5`, `B4`). All five
      accidental pairs sit in the same octave, so one trailing octave number is unambiguous.
    Chord events do not carry `note` — their template `name` already labels them.

11. **Tuning lives on the arrangement.** `note` derivation is tuning-relative, so each arrangement
    declares its tuning (open-string pitch per string). A re-tune regenerates every `note` label on the
    next write. This is a new prerequisite field on `Arrangement`.

12. **The tempo map is a warp-anchor grid (unchanged), but anchors serialize as position tokens.**
    `timeSignatures` are meter changes carried forward, addressed by `measure` (a meter change is
    measure-scoped, so it keeps `measure`, not a `position`). `anchors` pin sparse beats to absolute
    seconds and now address the beat with a `position` token (`{ "position": "188:1", "seconds": …
    }`) — anchors are always on a beat, so the token never carries a `+offset`. Seconds remain the
    only absolute time stored, on a fixed three-decimal grid. Interpolation, the start/terminal anchor
    requirement, and global-beat-index resolution are unchanged.

13. **The tempo map lives on `Song`; chord templates and events live on each `Arrangement`.**

## Target core model

Types under `rock-hero-common/core/include/rock_hero/common/core/`:

```cpp
struct Fraction { int numerator{0}; int denominator{1}; };   // existing; reduced, denominator > 0

struct GridPosition          // a grid-relative address; token "<measure>:<beat>[+<fraction>]"
{
    int measure{1};          // 1-based
    int beat{1};             // 1-based
    Fraction offset{};       // exact fraction in [0,1); 0 = on the beat
};

struct TimeSignatureChange { int measure{1}; int numerator{4}; int denominator{4}; };  // existing
struct BeatAnchor { int measure{1}; int beat{1}; double seconds{0.0}; };               // existing

struct Tuning                // open-string note names, used only to derive note labels
{
    // Scientific-pitch note names, indexed by (string_number - 1); string 1 first.
    std::vector<std::string> open_strings;
};

struct ChordVoicingString { int string_number{}; int fret{}; std::optional<int> finger; };

struct ChordTemplate
{
    std::string id;          // chord name, with -1/-2 ordinal on name collision
    std::string name;        // display label
    std::vector<ChordVoicingString> voicing;
};

struct Techniques            // minimal typed set, extensible as technique vocabulary grows
{
    bool vibrato{false};
    bool palm_mute{false};
    std::optional<Fraction> bend;   // bend amount in whole steps, absent = none
};

struct ChartEventStringDeviation     // one struck string of a chord that deviates
{
    int string_number{};
    std::optional<GridPosition> end; // overrides the event end for this string
    Techniques techniques;
};

// A single timed playable event: either a single note or a chord instance (Open decision E).
struct ChartEvent
{
    GridPosition start;
    std::optional<GridPosition> end;  // absent = non-sustained
    // single-note form: string + fret (+ derived note label) + techniques
    // chord form: chord template id + per-string deviations
    // exact representation pending Open decision E
};
```

`TempoMap`, `globalBeatIndex`, `secondsAtBeat`, `secondsAtNote`, and the interpolation math are
unchanged. Add `secondsAt(const GridPosition&)` as the single resolver used for both `start` and `end`.

## Validation rules

Tempo map (unchanged except anchor addressing): `timeSignatures` non-empty, starts at measure 1,
power-of-two denominators, strictly increasing measures; `anchors` strictly increasing in
`(measure, beat)` and `seconds`, start anchor at `1:1`, terminal anchor on a one-past-content downbeat,
each anchor's seconds on the three-decimal grid.

Chart events and templates:

- Every event `start` is within a real content bar (`measure` before the terminal boundary, `beat` in
  `1..numerator`); `offset` an exact fraction in `[0, 1)` reducing to denominator ≤ 1024.
- If `end` is present it resolves at or before the terminal anchor and is **after** `start`.
- A single-note event has a positive `string` and non-negative `fret`; a chord event's `chord`
  resolves to a template id; a `strings[]` deviation names a string present in the template, with no
  duplicate strings.
- No two struck strings share an onset (a chord is one event; two events must not strike the same
  string at the same `start`). Compared on `(global beat, reduced offset, string)`.
- `chordTemplates` ids are unique within the arrangement; names are non-empty; `voicing` strings are
  unique, positive, and present in the arrangement tuning; frets are non-negative.
- `note` on a single note, if present on read, must equal the value derived from `(string, fret,
  tuning)` (writer-owned; a mismatch is rejected rather than trusted).

## Settled representation decisions

These decisions are pinned for this implementation slice and reflected in the staged core model:

- **Event content:** `ChartEvent` uses `std::variant<SingleNote, ChordInstance>` so a playable
  row is either a single note or a chord reference, never both.
- **Techniques:** techniques use a typed struct with the currently known vocabulary (`vibrato`,
  `palmMute`, `bend`) and can grow as more techniques are specified.
- **Tuning:** tuning persists explicit per-string open note names on each arrangement. These names
  are parsed for validation and used to derive single-note `note` labels.

## Implementation steps (staged; build/test between stages)

1. **Core model** — add `GridPosition`, `Tuning`, `ChordVoicingString`, `ChordTemplate`,
   `Techniques`, `ChartEventStringDeviation`, `ChartEvent`; replace `NoteEvent` on
   `Arrangement` with `events`, add `chord_templates` and `tuning`. Pure tests for `secondsAt`.
2. **Serialization read** — position-token parse, `events` (single note + chord),
   `chordTemplates`, anchor `position`, tuning; validation per the rules above.
3. **Serialization write + formatting** — token + `events` + `chordTemplates` emission,
   derived `note` labels, regenerated chord ids; keep the one-line-per-row readable column
   formatting.
4. **Tests** — round-trip and malformed-input tests for the new shapes; writer-output formatting
   assertion (the open finding from the format review).
5. **Cleanup** — retire now-dead code (`durationBeats` paths, the old flat-note
   formatter/validator, `secondsGridUnits` stays for anchors), and align
   `docs/design/architecture.md` Song Data Model once the code lands.

## Testing

- `test_tempo_map.cpp`: interpolation, meter walking, `secondsAt(GridPosition)`, warp invariance
  (unchanged behavior).
- `test_rock_song_package.cpp`: tempo map + events + chord templates round-trip; malformed token,
  malformed chord ref, duplicate same-string onset, derived-`note` mismatch all fail with
  `InvalidArrangement`/`InvalidSongDocument`; direct writer-output formatting assertion.
- `test_arrangement.cpp` / `test_song.cpp`: model equality and construction for the new types.

## Non-goals

- No tempo/note editing UI; no grid-alignment tooling in this slice.
- No tone-change events or plugin-automation authoring.
- No cross-arrangement shared chord library (deferred, additive).
