\page guide_file_formats File Formats

*Applies to: Repo-wide. Complete field-level reference for every serialized format the project
owns.*

All formats are `formatVersion` 1 and **change freely pre-release** — there is no
backward-compatibility obligation, and no migration code exists by decision. The serializers in
`rock-hero-common/core/src/package/` and `rock-hero-common/audio/src/live_rig/` are the source of
truth; per the guide's maintenance rule, a commit that changes any field updates this page in the
same commit. Field tables mark keys **req** (read rejects absence) or *opt* (absence gets the
stated default — the normalize-don't-reject rule in action).

# The containers

```text
.rock  (song package — flat ZIP)          .rhp  (editor project — ZIP)
  song.json                                 project.json          {"formatVersion": 1}
  audio/<name>.flac                         song/
  charts/<uuid>.chart.json                    ... byte-identical .rock content ...
  tones/<uuid>/tone.json
  tones/<uuid>/state/plugin-<n>.tracktion-plugin

.tone  (standalone tone interchange — ZIP)
  tone.json                                 same document shape as the in-package tone.json
  state/plugin-<n>.tracktion-plugin
```

Container rules: ZIP entries with `..`, absolute paths, drive colons, empty segments, symlinks,
or case-insensitive duplicate names are rejected on read. Audio filenames are sanitized
(`[alnum . - _]` only) and uniquified with `-2`, `-3`, ... suffixes. There is no cover-art file
yet (roadmap plan 43). Archives are rewritten in place on save (no atomic temp-then-rename).

**The FLAC rule is read-side only** — the writer copies audio with its source extension; the
reader rejects anything that is not `.flac`. Editor flows transcode before this point, but the
asymmetry is real: the writer can produce a package the reader refuses.

# song.json

Written by `songDocumentContents` (`rock_song_package_write.cpp`); read by the full reader and,
for a subset, the game's peek reader (`package_description.cpp` — reads `formatVersion`,
`metadata`, audio-asset `id`/`path`, arrangement `id`/`part`/`audio`/`chart` only).

| key | type | req | meaning (default) |
|---|---|---|---|
| `formatVersion` | int | req | Must be `1` — the single song-document version gate. |
| `metadata` | object | opt | Absent → all-blank. |
| `metadata.title` / `.artist` / `.album` | string | opt | (`""`) |
| `metadata.year` | int | opt | (`0`) |
| `tempoMap` | object | req | See below. Not read by the peek reader. |
| `audioAssets` | array ≥1 | req | The audio files; arrangements reference by id. |
| `arrangements` | array ≥1 | req | See below. |

## tempoMap

| key | type | req | meaning |
|---|---|---|---|
| `timeSignatures[].measure` | int > 0 | req | 1-based effect measure; strictly increasing. |
| `timeSignatures[].numerator` | int > 0 | req | Beats per measure. |
| `timeSignatures[].denominator` | int | req | Beat unit — must be a power of two. |
| `anchors[].position` | string | req | `"measure:beat"` on-beat token (no sub-beat). |
| `anchors[].seconds` | number | req | Absolute seconds, fixed 3-decimal precision. |

Anchor invariants: at least two anchors (start + terminal); the first must be `1:1` but its
seconds **may be non-zero**; strictly increasing in both beats and seconds; the terminal anchor
must land on a downbeat.

## audioAssets[]

| key | type | req | meaning (default) |
|---|---|---|---|
| `id` | string | req | Unique; writer generates `audio-<n>`, deduped by path. |
| `path` | string | req | Package-relative, safe, existing, `.flac`. |
| `normalization` | object | opt | Absent/incomplete → dropped and re-analyzed on load. |
| `normalization.gainDb` | number | req* | Loudness gain in dB (*required within the object). |
| `normalization.validationSha256` | string | req* | Hash tying the gain to the analyzed audio. |
| `startOffset` | number | opt | Seconds audio begins after beat 1 (`0`; omitted on write when 0). |

## arrangements[]

| key | type | req | meaning (default) |
|---|---|---|---|
| `id` | string | req | Canonical lowercase UUIDv4; unique; minted on save if empty. |
| `part` | string | req | `"Lead"` \| `"Rhythm"` \| `"Bass"` — closed enum. |
| `audio` | string | req | An `audioAssets[].id`. |
| `chart` | string | opt | `charts/<uuid>.chart.json` ref; must exist if present. |
| `tones` | array | opt | Tone catalog `{id: <uuid>, name}`; paths derived. |
| `toneChanges` | array | opt | Tone schedule: `{start: <grid token>, tone: <uuid>}`. |
| `toneAutomation` | array | opt | Parameter automation — see below. |

Deliberately **not** persisted: difficulty (derived at runtime), audio duration (read from the
decoded audio), and any per-arrangement paths — `song.json` speaks only UUIDs and asset ids.

`toneChanges` stores only region **starts** (grid tokens allow `+n/d` sub-beat); each region ends
at the next start, the last at the tempo map's terminal beat — gaps are structurally
unrepresentable. Region ids are session-scoped and never persisted. A `toneChanges` tone missing
from `tones[]` is normalized in as an unnamed catalog entry.

`toneAutomation[]`: `{plugin, param, points[]}` with at most one entry per (plugin, param);
points are `{position: <grid token>, value: <normalized number>, shape}` (`shape` optional,
`0` = linear, omitted on write when 0). Musical positions are the persisted truth; seconds are
derived caches.

*Design in flux: `toneAutomation` (and its interaction with `toneChanges`) belongs to the active
automation plan (`docs/plans/in-progress/tone-parameter-automation-plan.md`).*

# Chart document — `charts/<uuid>.chart.json`

Owned by `common/core/chart/chart_document.cpp`. Grid tokens are `"m:b"` or `"m:b+n/d"`;
fractions are `"n/d"` (or `"n"`). One caveat: `formatVersion: 1` is **written but never checked
on read** — the chart document currently has no version gate.

| key | type | req | meaning (default) |
|---|---|---|---|
| `tuning.strings` | string[] | req | Per-string tuning labels. |
| `tuning.capo` | int | opt | (`0`) |
| `tuning.centOffset` | number | opt | (`0.0`) |
| `chords[].name` | string | opt | Chord template name (`""`). |
| `chords[].frets` / `.fingers` | (int\|null)[] | req | Per-string; `null` = not played. |
| `notes[].position` | grid token | req | Note location. |
| `notes[].string` | int | opt | String index (`0`). |
| `notes[].fret` | int | opt | (`-1` = unset). |
| `notes[].sustain` | fraction | opt | Omitted when zero. |
| `notes[].attack` | string | opt | `hammer`\|`pull`\|`tap`\|`pop`\|`slap`; absent = pick. |
| `notes[].mute` | string | opt | `palm`\|`full`. |
| `notes[].harmonic` | string | opt | `natural`\|`pinch`. |
| `notes[].touch` | number | opt | Touch-harmonic fret point. |
| `notes[].vibrato` / `.tremolo` / `.accent` | bool | opt | Written only when true. |
| `notes[].bend` | [fraction, number][] | opt | Offset + semitone pairs. |
| `notes[].slides[]` | object[] | opt | `{offset: <fraction> req, fret (-1), unpitched (false)}`. |
| `shapes[]` | object[] | opt | `{position, sustain, chord}` all req; chord indexes `chords[]`. |
| `fhps[]` | object[] | opt | `{position req, fret (0), width (4; omitted when 4)}`. |
| `sections[]` | object[] | opt | `{position req, type ("")}`. |

Unknown enum tokens are hard read errors; chart *rules* (ordering against the tempo map) are
validated after load, not by the parser.

# Tone document — `tones/<uuid>/tone.json`

Owned by `common/audio` (`tone_document.cpp`); core treats it as opaque. Gate: `formatVersion`
must be `1`.

| key | type | req | meaning (default) |
|---|---|---|---|
| `slots` | array ≥1 | req | **Only `slots[0]` is read.** |
| `slots[0].chain[]` | array | req | Ordered plugin records; capped at the signal-chain maximum. |
| `slots[0].outputGainDb` | number | opt | Clamped on read (default gain). |
| `chain[].id` | string | req | Plugin record id. |
| `chain[].tracktionState` | string | req | Canonical sidecar ref under `state/`; must exist. |
| `chain[].blockIndex` | int | opt | Editor-owned visual block (`0`); opaque to audio. |
| `chain[].displayTypeOverride` | string | opt | Editor-owned display token, carried opaquely. |
| `chain[].stableId` | string | opt | Durable identity for automation binding. |
| `chain[].identity` | object | opt | Plugin descriptor hints; see list below. |

Write-only (inert) fields the reader ignores: `slots[0].id`/`.name`, `slots[0].automation`
(an empty placeholder reserved by the automation plan — today's automation lives in `song.json`),
and the entire `toneClips` array. The `identity` hint keys (all optional): `format`, `name`,
`descriptiveName`, `manufacturer`, `version`, `uniqueId`, `deprecatedUid`, `isInstrument`,
`originalFileOrIdentifier`, `juceIdentifierHint`, `tracktionIdentifierHint`. Plugin state
sidecars are not JSON: they are Tracktion
ValueTrees serialized as XML, named `plugin-<n>.tracktion-plugin`; live item ids are stripped on
read so restored trees cannot collide.

# Standalone `.tone` file

The extension is `.tone` (`g_tone_file_extension`) — the interchange format for sharing a tone
between users. Same `tone.json` document shape and version gate as the in-package tone document,
with deliberate differences:

1. Sidecars live at archive-root `state/` and refs are re-derived from chain order on write.
2. **No durable identity travels**: `stableId` is force-cleared on both write and read —
   importers mint fresh ids.
3. **No automation travels**: plugin state is scrubbed (`stripAutomationCurves`,
   `stripTempoRemapFlag`) on both paths.
4. The reader parses sidecar XML into ValueTrees up front and fails on any missing state entry.

# project.json

The `.rhp` manifest in its entirety: `{"formatVersion": 1}` — one key, one gate, nothing else.
Editor view state (cursor, zoom, selected arrangement) deliberately lives in per-user settings,
never in the manifest (see \ref guide_project_lifecycle).

# Cross-format invariants

- **One version gate per format**, each in exactly one function (`song_document_json.cpp` for
  song.json; the tone-document parser; `project_io.cpp`) — no other call site may test a
  version. The migration ladder that replaces the hard gates is roadmap plan 10. The chart
  document is the known exception (writes a version, checks nothing).
- **Normalize, don't reject**: save is publish, so readers repair what they can (blank metadata,
  dropped-incomplete normalization, missing catalogs, defaulted fields) and reject only
  structural violations (bad ids, missing referenced files, malformed tokens, unknown enums,
  tempo-map rule breaks, non-FLAC audio).
- **Round-trip stability**: default-valued optionals (`startOffset` 0, automation `shape` 0, FHP
  `width` 4, false booleans, pick attacks) are omitted on write, so packages that predate a
  feature round-trip byte-for-byte.
- Save validates every chart/tone reference *before* any side effect, so a bad reference fails
  cleanly with nothing half-written.
