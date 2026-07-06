# Note Format and 2D Tablature Plan

Status: IMPLEMENTED 2026-07-06. The format (slice 1), the read-only tab lane (slice 2), and the
full technique/chord/FHP rendering (slice 3) all shipped: chart domain model and document IO in
`rock-hero-common/core/chart/`, package wiring through `Arrangement`/`rock_song_package_format`,
the seconds-resolved `TabViewState` projection in editor-core, and the `TabView` overlay in the
waveform lane with the View menu's Show Waveform toggle and Tablature Strings selector (both
app-wide persisted settings). The 39-package reference corpus carries linked charts and loads
through the production reader. Remaining from this plan: only slice 4 (authoring/import), which
stays future work. The 3D display has its own plan at `docs/todo/3d-highway-plan.md`.

## Goal

Two deliverables, in order:

1. **A clean project-owned note format.** Notes, chords, arpeggios, fret-hand positions, and
   techniques stored in the song package with the same feel as the tempo map: musical positions as
   readable tokens, scan-heavy arrays at one compact object per line, validation shared by reader
   and writer. The format must carry everything a full 3D highway view will eventually need, even
   where the 2D view renders only a subset — the format must never be the reason a 3D feature
   can't ship.
2. **A 2D tablature lane rendered over the waveform.** The tab draws in the same lane as the
   backing waveform (not a new row), with the waveform optionally hideable behind it. The look and
   feel of the tab icons should feel very similar to Charter's 2D editor view, even though the
   implementation will differ.

## Reference: Charter (MIT)

[Charter](https://github.com/Lordszynencja/Charter) is an MIT-licensed RS-format chart
editor whose notation model was reviewed at source level (2026-07-05) to enumerate what a complete
guitar notation format needs. Its layout also validates this plan's display direction: Charter
draws the waveform inside the same editor lane as the notes (`WaveFormDrawer` sits among its chart
drawers) and renders all notation glyphs programmatically from shape primitives (ovals, triangles,
sine strips for vibrato) rather than image assets.

### Notation inventory from the Charter data model

Positions (`FractionalPosition`): every chart object sits at (beat index, exact rational fraction
within the beat) — never milliseconds. Sustains are position + end position.

Per note (`Note`/`GuitarSound`):

- string, fret
- sustain end position
- mute: none | palm | full
- HOPO: none | hammer-on | pull-off | tap
- harmonic: none | normal | pinch
- bass picking: none | pop | slap
- vibrato, tremolo, accent flags
- link-next (note ties into the following note)
- slide: target fret + pitched/unpitched flag
- bend: a list of (position, bend value) points across the sustain
- charting-only flags: ignore, pass-other-notes

Chords (`Chord` + `ChordTemplate` + `ChordNote`):

- chord templates are an arrangement-level table: name, arpeggio flag, per-string frets,
  per-string fingering (thumb + 1-4)
- chord instances reference a template by id and carry per-string `ChordNote`s, each with the same
  technique set as a single note (mute/HOPO/harmonic/vibrato/tremolo/slide/bends/link-next)
- display flags: split-into-notes, force-no-notes

Fret-hand positions (`FHP`): position, fret, width (default 4) — where the hand sits on the neck.

Handshapes (`HandShape`): (start, end, template id) spans marking held chord shapes/arpeggios.

Structure and metadata:

- sections (31 types: verse, chorus, solo, breakdown, ...), phrases (name → max difficulty, solo
  flag), event points, misc events (ticks, crowd)
- arrangement-level: type (Lead/Rhythm/Bass + subtype), tuning, capo, cent offset, picked-bass,
  tone changes, chord template table, difficulty levels
- difficulty levels (`Level`): per-difficulty lists of FHPs, sounds (notes-or-chords), handshapes
- vocals and showlights exist in Charter; out of scope for RockHero's format now

## Format Direction

- **Positions extend the tempo-map token grammar.** Whole-beat positions stay `"<measure>:<beat>"`;
  sub-beat positions extend it with an exact fraction, e.g. `"12:3+1/2"` (grammar to be finalized
  in slice 1). This resolves the token question deferred from the tempo-map slice, and Charter's
  rational-fraction positions confirm exact fractions (never floats, never milliseconds) are the
  right substrate. Seconds are always derived through the tempo map, exactly like tone regions.
- **Chords are templates plus shape spans, never chord events.** Decision 2026-07-06 (superseding
  the earlier templates-plus-instances direction): the template table (name, frets, fingers)
  lives per arrangement, and `shapes` spans reference templates over the notes the hand plays —
  one mechanism for strummed chords, chugged riffs, and arpeggios alike. Fingering and FHP data
  are load-bearing for the difficulty calculator
  (`docs/todo/arrangement-difficulty-derivation-plan.md`), so the format keeps them first-class
  even though the 2D view may not render fingerings initially.
- **One true tab per arrangement — no difficulty levels.** Decision 2026-07-05: Charter carries
  RS-style per-difficulty level lists (pared-down variants of each arrangement); RockHero
  deliberately does not. Each arrangement stores a single TRUE tab — what is actually played — and
  difficulty is a per-arrangement **derived rating**
  (`docs/todo/arrangement-difficulty-derivation-plan.md`), never authored. This avoids manually
  curating multiple fake-feeling difficulty variants per song. The format therefore has one
  `notes` stream per arrangement, every entry one string sounding (one object per line in JSON;
  strummed chords are simultaneous entries under a shape span). Charter's
  `Level`/phrase-max-difficulty machinery is explicitly not adopted.
- **One physical onset = one note event; no link-next flag.** Decision 2026-07-05.
  Charter and the RS format model tied notes as separate events joined by `linkNext`; 26 Charter files
  handle it, its editors must keep chain endpoints/strings consistent, and Charter's own
  `ArrangementFixer` contains a repair pass that merges technique-free linked notes back into one
  note — evidence the flag makes invalid states representable. RockHero notes own their whole
  sustain, and techniques that evolve during the sustain are positioned payloads inside the note —
  Charter already does exactly this for bends (`BendValue` points inside the note). A linked
  slide chain becomes fret waypoints `(position, fret, pitched|unpitched)` within one note; bend
  releases across what importers see as tie chains become one bend curve. Importers flatten
  tie/link chains at the boundary. Hammer-ons/pull-offs/taps are new onsets by definition and
  stay separate notes. Measure-crossing sustains stay one note; a barline tie is a rendering
  glyph, not data. Whole-note vibrato/tremolo flags to start; positioned technique spans only if
  a real chart demands a mid-sustain change the payloads cannot express.
- **Technique fields are optional with defaults.** A plain quarter note should serialize as a tiny
  one-line object; techniques appear only when present (same style as tone regions omitting empty
  names).
- **Domain types live in `rock-hero-common/core`** (a `chart/` or `notation/` feature folder,
  naming decided at implementation), serialized through `rock_song_package_format` with shared
  validation rules, mirroring the tone-track structure (public rules unit + package translation).
- Tuning/capo/cent-offset live on the arrangement. Pitch labels are derived in UI, not persisted.

## Display Direction (2D tab lane)

- The tab renders **inside the waveform lane** (`ArrangementView`'s row), not as a new track row;
  the waveform becomes an optionally hidden backdrop (a view toggle, no format impact). This is
  Charter's own layout.
- Icon/glyph look and feel should track Charter's 2D view closely: string-colored note heads with
  fret numbers, sustain tails, slide arrows, bend curves with value labels, technique glyphs
  (palm-mute, harmonic, HOPO, tremolo, vibrato sine strips), chord boxes with template names, FHP
  markers along the bottom. Because Charter is MIT, its drawing code may be studied and its visual
  language ported; our implementation is JUCE `Graphics` primitives like the rest of the editor.
- Rendering reuses the shared timeline mapping (`timelineXForPosition`, visible-range plumbing) and
  the track-row layering contract (row band → tempo grid → content → cursor overlay).
- The existing playback-follow behavior is unchanged. Once the tab renders over the waveform,
  re-raise the smooth-scroll evaluation per
  `docs/todo/smooth-scroll-follow-evaluation.md` (settled trigger).

## The Two Hurdles

1. **The format.** Getting notes/chords/arpeggios/FHPs/techniques into a shape as clean as the
   tempo map, exhaustive enough for 3D, without over-modeling. Slice 1 is a format-design pass
   with a written spec and package tests before any rendering work.
2. **The tab rendering.** Matching Charter's readable-at-speed visual language inside our
   viewport/zoom/grid system.

## Implementation Slices

1. **Format design + domain model.** Finalize the sub-beat token grammar; define domain value
   types and validation rules in `rock-hero-common/core`; extend `rock_song_package_format`
   read/write; package tests including legacy songs without charts (no forced rewrite, same
   pattern as `toneTrack`).
2. **Read-only tab lane: notes and sustains.** Render single notes (string color, fret number,
   sustain tail) over the waveform from view state; waveform hide toggle.
3. **Techniques and chords.** Chord boxes and template names, HOPO/mute/harmonic glyphs, slides,
   bends, vibrato/tremolo; FHP markers.
4. **Authoring comes later.** Note editing and import (Guitar Pro / RS XML) are separate
   future plans; the format must not preclude them (Charter's GP import list is the reference for
   what importers need). Difficulty is never authored — only the derived rating.

## Format Specification (v2, corpus-validated 2026-07-06)

The chart is an arrangement-owned sidecar, like `toneDocument`: the arrangement entry in
`song.json` gains `"chart": "charts/<uuid>.chart.json"`, keeping thousands of note lines out of
the song document. Everything below lives in that chart file. Design rules that keep it clean:

- **Every fact is stated once.** Durations are beat-fraction tokens, never end positions (moving
  a note touches one field). Intra-note payloads (bends, slides) use *note-relative* beat
  offsets, so moving a note never rewrites its payloads. Techniques appear only when present.
- **Positions reuse the tempo-map token grammar and its key.** `"position"` names an absolute
  timeline position everywhere, exactly like tempo anchors: `"27:3"` is a whole beat,
  `"27:3+1/2"` an exact rational sub-beat. Note-relative payload offsets are deliberately named
  `"offset"` instead — same fraction grammar, different meaning, different word. Beat-fraction
  durations are bare fraction tokens (`"3/2"`).
- **Notes say what sounds; shapes say what the hand holds; templates are reusable postures.**
  Every `notes` entry is one string sounding — there is no note-versus-chord sum type. A strummed
  chord is simultaneous notes at one position. A `shapes` span `{position, sustain, chord}` marks
  the hand holding a template posture, and covers strummed chords, chugged riffs on one shape,
  and arpeggios with a single mechanism (RS corpus data confirmed chords and handshapes are ~1:1
  duplicates when modeled separately). Whether a shape is a chord box or an arpeggio bracket is
  derivable from whether its notes arrive together or sequentially — no stored flag.
- **One `attack` field for how an onset is produced.** `hammer | pull | tap | pop | slap`
  (absent = plain pick). These are mutually exclusive by nature, so one field makes illegal
  combinations unrepresentable. Timbre modifiers that genuinely combine with any attack stay
  separate: `harmonic` (tap harmonics are real) and `mute` (palm-muted slap is real).
- **Curve payloads are two-column pairs.** `[offset, value]`, like coordinates — self-evident
  and dense where entries are many.

```json
{
  "formatVersion": 1,
  "tuning": { "strings": ["E2", "A2", "D3", "G3", "B3", "E4"], "capo": 0, "centOffset": 0.0 },
  "chords": [
    { "name": "F5", "frets": [1, 3, 3, null, null, null], "fingers": [1, 3, 4, null, null, null] },
    { "name": "Am", "frets": [null, 0, 2, 2, 1, 0], "fingers": [null, null, 2, 3, 1, null] }
  ],
  "notes": [
    { "position": "12:1", "string": 3, "fret": 5 },
    { "position": "12:1+1/2", "string": 3, "fret": 7, "attack": "hammer" },
    { "position": "12:2", "string": 3, "fret": 5, "attack": "pull", "sustain": "1/2" },
    { "position": "12:3", "string": 2, "fret": 9, "attack": "tap", "accent": true },
    { "position": "13:1", "string": 4, "fret": 5, "sustain": "2", "vibrato": true },
    { "position": "13:3", "string": 5, "fret": 0, "mute": "palm", "tremolo": true, "sustain": "1" },
    { "position": "14:1", "string": 5, "fret": 3, "mute": "full" },
    { "position": "14:2", "string": 2, "fret": 12, "harmonic": "natural" },
    { "position": "14:3", "string": 5, "fret": 5, "harmonic": "pinch", "sustain": "1" },
    { "position": "15:1", "string": 6, "fret": 3, "attack": "slap" },
    { "position": "15:1+1/2", "string": 6, "fret": 5, "attack": "pop" },
    { "position": "16:1", "string": 4, "fret": 7, "sustain": "4",
      "bend": [["1/2", 1.0], ["1", 2.0], ["3", 2.0], ["4", 0.0]] },
    { "position": "18:1", "string": 4, "fret": 5, "sustain": "3",
      "slides": [{ "offset": "1", "fret": 9 }, { "offset": "2", "fret": 7 }, { "offset": "3", "fret": 12, "unpitched": true }] },
    { "position": "20:1", "string": 6, "fret": 1, "sustain": "1", "mute": "palm" },
    { "position": "20:1", "string": 5, "fret": 3, "sustain": "1", "mute": "palm" },
    { "position": "20:1", "string": 4, "fret": 3, "sustain": "2", "vibrato": true },
    { "position": "20:3", "string": 5, "fret": 0 },
    { "position": "20:3+1/2", "string": 4, "fret": 2 },
    { "position": "21:1", "string": 3, "fret": 2 },
    { "position": "21:1+1/2", "string": 2, "fret": 1 },
    { "position": "22:4", "string": 3, "fret": 14, "sustain": "5/2" }
  ],
  "shapes": [
    { "position": "20:1", "sustain": "2", "chord": 0 },
    { "position": "20:3", "sustain": "6", "chord": 1 }
  ],
  "fhps": [
    { "position": "12:1", "fret": 5 },
    { "position": "16:1", "fret": 7, "width": 5 }
  ],
  "sections": [
    { "position": "12:1", "type": "verse" },
    { "position": "20:1", "type": "chorus" }
  ]
}
```

What each piece encodes, and the edge cases it covers:

- **Onset model.** Each entry is one string sounding once. The `"22:4"` note sustains `5/2`
  beats across the measure 23 barline — one note; the renderer draws any tie glyph.
  Hammer/pull/tap are new onsets with an attack value, never links. The slide-chain example
  replaces a three-note Charter link chain with one note and three waypoints (the last one
  unpitched).
- **Bends.** Note-relative `[offset, semitones]` pairs across the sustain: up to 1, full bend
  held to beat 3, released by 4. A pre-bend is a pair at offset `"0"`.
- **Chords and arpeggios.** The `"20:1"` F5 strum is three simultaneous notes under a shape
  span — each string carries its own true sustain and techniques (the bass strings chug palm-
  muted for one beat; the top string rings two with vibrato), with no override machinery. The
  `"20:3"` Am shape spans sequential notes: an arpeggio, recognized from the same data. Sounding
  truth never depends on shapes; strip the `shapes` array and the audio content is unchanged —
  shapes add the notation layer (chord boxes, names, brackets, fingerings via the template).
  Validation can advise when sounding notes under a span disagree with the template posture.
- **FHPs.** Position + fret, `width` only when not 4.
- **Sections.** Navigation/practice markers; the type vocabulary starts small and grows as
  needed (no commitment to Charter's 31 kinds; the RS format's name+number pairs collapse to type,
  with repeat numbering derivable).
- **Deliberately absent.** Difficulty levels and phrase-max-difficulty (true-tab decision),
  link-next (onset model), end positions (durations), chord-instance events and per-string
  override lists (notes+shapes factoring), a template `arpeggio` flag (derivable from note
  timing under the span), separate hopo/picking fields (single `attack`), charting-only flags
  (ignore/pass-other-notes), vocals/showlights/crowd events.

### String count, micro-bends, and forward extensions (2026-07-06)

- **The format is string-count-generic; the target is at least 10 strings.** `tuning.strings`
  length is the single authority: `string` values range 1..N, template arrays are N long, and
  nothing anywhere assumes 6. The renderer divides the lane height by N and draws from a palette.
  String colors for extended-range strings extrapolate the standard six rather than adopting
  Charter's extras: the standard six are the RYB painter's wheel — primaries in order (red, yellow, blue for E A D) then
  secondaries in derivation order (orange, green, purple for G B e) — interleaved so adjacent
  strings always contrast strongly. Strings below low E continue with the wheel's tertiary tier,
  ordered for the same adjacent contrast going down: 7th (low B) **teal** `(0, 181, 160)`, 8th
  (low F#) **magenta** `(255, 0, 144)`, 9th (low C#) **chartreuse** `(170, 220, 0)`, 10th (low
  G#/A) **indigo** `(88, 84, 255)`. Teal beside the red low E mirrors the original set's
  blue-beside-orange complementarity. Exact RGB values are display configuration, finalized
  visually during the tab-rendering slice; the format carries no colors.
- **Quarter bends (and finer) are already representable.** Bend pair values are plain numbers in
  semitones; the corpus already contains 0.5 (quarter-tone curls), and 0.25 or any other
  granularity needs no format change. the RS format's coarseness is an importer limitation, not a
  format one.
- **Whammy bar (future, additive).** Sketch: a `whammy` payload on the note using the same
  two-column pair encoding as `bend` — `[[offset, semitones]]` — with *signed* values (dives go
  negative). Kept distinct from `bend` because a bend is a finger on one string (non-negative,
  per string) while the bar bends every sounding string and dives; consumers that treat them
  identically may merge the curves at render time. Purely additive when it lands.
- **Between-fret harmonics (future, additive).** Natural harmonics sound at node points that are
  not fret positions (the 3.2 / 2.7 / 2.2 family). Sketch: an optional `touch` number on
  harmonic notes carrying the precise fractional touch position (`"fret": 3, "harmonic":
  "natural", "touch": 3.18`), where `fret` stays the integer display anchor. Purely additive; no
  editor authors it yet, but the format will not need to change when one does.

### Corpus validation (2026-07-06)

The format was pressure-tested against a 39-song RS-format corpus (BTBAM epics, Opeth, Yes,
Dire Straits, Protest The Hero, Funkadelic, Periphery — official DLC and CDLC, DD and non-DD),
fully converted into reference packages under
`Rock Hero Stuff/Chart References/*.rhp`. Each package is openable in the editor today
(current-format `song.json`, warp-anchor tempo map derived from the source ebeats, converted
audio, tone regions generated from the source charts' tone changes with empty tone documents ready for
authoring) and carries the full converted chart per arrangement at
`song/charts/<arrangement-uuid>.chart.json` (unreferenced until the chart reader lands).
`_conversion_report.json` in the same folder records per-song statistics.

What ~260k converted notes across the corpus established:

- **Every construct in the corpus is representable.** Audit found zero unrepresentable cases.
  All five `attack` values, both mutes, both harmonics, bends from 0.5 (quarter-tone curl)
  through 3.0 semitones, tap-slide licks, linked slide runs, arpeggio shape spans, per-string
  chord technique variation, capo, drop tunings, and 4-string bass all convert cleanly.
- **Rational positions earn their generality.** Real charts use denominators 5, 7, 9, 12, and 16
  (tuplets), not just powers of two. Sustains are even richer (2.7k /5 entries, 2.3k /7).
- **The tempo map compresses hostile grids.** Hand-warped charts (Marigold: 943 individually
  timed ebeats, meters alternating 7/4 and 4/4 per measure) reduce to ~135 anchors + ~29
  signature entries.
- **Validation rules confirmed by audit:** notes sorted by (position, string) with no duplicate
  (position, string) pairs; `sustain` > 0 when present; slide offsets strictly positive,
  ascending, and ≤ sustain (a slide needs a window to glide in — the audit's one flagged defect
  class); bend offsets ≥ 0 and ≤ sustain; shape `chord` indexes in range; shape sustain > 0;
  template `frets`/`fingers` array length equals the tuning's string count (4 for bass).

Importer findings recorded for the future import plan: RS `bendValue.step` is already in
semitones; `linkNext` semantically means "the next note on this string continues" (follow by
next-onset-within-tolerance, not exact end-time match — official DLC has millisecond gaps);
source charts contain dangling links and zero-sustain instant slides that need repair on import;
measure numbering must be renormalized sequentially; the RS `ignore` scoring flag is dropped;
RS section name+number pairs collapse to `type`.

## Relationship to other plans

- Owns the deferred chart-storage questions (grid tokens, chord modeling, technique
  representation, tuning placement) previously parked in
  `docs/todo/chart-note-storage-future-work.md`, which was removed 2026-07-05 when this plan
  absorbed it.
- Feeds `docs/todo/arrangement-difficulty-derivation-plan.md`: the format deliberately carries
  fingerings, FHPs, and techniques the calculator needs.
- Sequenced after the tone plan's slice 5; user-facing parameter automation (and the tone row's
  expandable sub-lanes) queue behind this work per the tone plan's Non-Goals.

## Non-Goals

- No 3D highway view (the format serves it; the view is its own project).
- No gameplay scoring/detection work.
- No vocals or showlights storage.
- No chart importers yet.
- No note editing UI in the first pass.

## Open Questions

- Exact sub-beat token spelling (`"12:3+1/2"` vs. alternatives) and whether whole-beat positions
  keep the bare `"12:3"` form (they should).
- How much of Charter's charting-only metadata (ignore, pass-other-notes, phrase/section
  taxonomy) RockHero adopts versus simplifies.
- Where the waveform-hidden toggle persists (app settings vs. per-project resume state).
