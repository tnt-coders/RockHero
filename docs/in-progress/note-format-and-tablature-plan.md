# Note Format and 2D Tablature Plan

Status: drafted 2026-07-05, queued behind the tone plan's slice 5 (runtime tone switching). This
becomes the active work after that tone pass lands.

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

[Charter](https://github.com/Lordszynencja/Charter) is an MIT-licensed Rocksmith-style chart
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
- **Chords are explicit templates plus instances.** The template table (name, frets, fingers,
  arpeggio) lives per arrangement; instances reference templates by index/id. Fingering and FHP
  data are load-bearing for the difficulty calculator
  (`docs/todo/arrangement-difficulty-derivation-plan.md`), so the format keeps them first-class
  even though the 2D view may not render fingerings initially.
- **One true tab per arrangement — no difficulty levels.** Decision 2026-07-05: Charter carries
  Rocksmith-style per-difficulty level lists (pared-down variants of each arrangement); RockHero
  deliberately does not. Each arrangement stores a single TRUE tab — what is actually played — and
  difficulty is a per-arrangement **derived rating**
  (`docs/todo/arrangement-difficulty-derivation-plan.md`), never authored. This avoids manually
  curating multiple fake-feeling difficulty variants per song. The format therefore has one
  `sounds` stream per arrangement: a sound is a note or a chord instance (sum type in the domain
  model, one object per line in JSON). Charter's `Level`/phrase-max-difficulty machinery is
  explicitly not adopted.
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
4. **Authoring comes later.** Note editing and import (Guitar Pro / Rocksmith XML) are separate
   future plans; the format must not preclude them (Charter's GP import list is the reference for
   what importers need). Difficulty is never authored — only the derived rating.

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
- **Link-next replacement (proposed direction, pending confirmation).** Charter/Rocksmith model
  tied notes as separate events joined by a `linkNext` flag; 26 Charter files handle it, its
  editors must keep chain endpoints/strings consistent, and Charter's own `ArrangementFixer`
  contains a repair pass that merges technique-free linked notes back into one note — evidence
  the flag makes invalid states representable. Proposed RockHero model: **one physical onset =
  one note event; no link flag.** A note's sustain carries positioned payloads instead — Charter
  already does exactly this for bends (`BendValue` points inside the note) — so a linked slide
  chain becomes fret waypoints `(position, fret, pitched|unpitched)` within one note, and bend
  releases across what importers see as tie chains become one bend curve. Importers flatten
  tie/link chains at the boundary. Hammer-ons/pull-offs/taps are new onsets by definition and
  stay separate notes. Measure-crossing sustains stay one note; a barline tie is a rendering
  glyph, not data. Whole-note vibrato/tremolo flags to start; positioned technique spans only if
  a real chart demands a mid-sustain change the payloads cannot express.
- How much of Charter's charting-only metadata (ignore, pass-other-notes, phrase/section
  taxonomy) RockHero adopts versus simplifies.
- Where the waveform-hidden toggle persists (app settings vs. per-project resume state).
