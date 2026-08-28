\page guide_file_formats File Formats

*Applies to: Repo-wide. Complete field-level reference for every serialized format the project
owns.*

All formats are `formatVersion` 1 and **change freely pre-release** — there is no
backward-compatibility obligation, and no migration code exists by decision. The serializers in
`rock-hero-common/core/src/package/` and `rock-hero-common/audio/src/live_rig/` are the source of
truth; per the guide's maintenance rule, a commit that changes any field updates this page in the
same commit. Field tables mark keys **req** (read rejects absence) or *opt* (absence gets the
stated default — the normalize-don't-reject rule in action).

**One authority writes a JSON number**: `Json::numberText` (`common/core/shared/json.h`), the
shortest text that reads back as the same `double`, with a `.0` kept on an integral value so a
field that means a double never reads back as an integer. A fixed precision cannot be that
authority — it rounds silently, so a document stops equalling itself across a save, and rounding
*launders*: a value the rules refuse can round to one they accept, turning a validation error into
corruption. The one deliberate exception is tempo-map timing, which keeps its fixed package
precision (`anchors[].seconds` below).

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

**The FLAC rule is enforced on both sides** with one shared predicate (`hasFlacExtension` in
`rock_song_package_format.cpp`): the writer rejects a non-`.flac` audio asset before copying
anything, and the reader rejects it on load. Editor flows transcode to FLAC upstream; this rule
is the loud failure if anything slips past.

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
| `sections` | array | opt | Song-structure markers; see below. Not read by the peek reader. |
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

## sections[]

Song-level structure markers (moved out of the chart documents: sections describe the song, not
one arrangement's tab). Every arrangement and the 3D highway share this one list.

| key | type | req | meaning |
|---|---|---|---|
| `position` | string | req | Grid token `"m:b"` / `"m:b+n/d"`; must be on the tempo-map grid. |
| `name` | string | req | Free-form non-empty label, verbatim from import. |

Sections must be sorted STRICTLY ascending by position; the reader rejects unsorted or unnamed
entries, and also rejects two sections claiming one position (which would leave every later
which-section-governs-this-moment question answering arbitrarily).

## audioAssets[]

| key | type | req | meaning (default) |
|---|---|---|---|
| `id` | string | req | Unique; writer generates `audio-<n>`, deduped by path. |
| `path` | string | req | Package-relative, safe, existing, `.flac`. |
| `normalization` | object | opt | Absent/incomplete → dropped and re-analyzed on load. Staying absent after that analysis is a legitimate answer, not a failure: audio with no measurable loudness (digital silence, or under libebur128's -70 LUFS gate) has no gain to compute, so it plays at its raw level and the editor says so once at open. |
| `normalization.gainDb` | number | req* | Loudness gain in dB (*required within the object). |
| `normalization.validationSha256` | string | req* | Hash tying the gain to the analyzed audio. |
| `startOffset` | number | opt | Signed seconds of the file's first sample from beat 1: positive delays the audio, negative means its head precedes the score and is skipped at playback (`0`; omitted on write when 0). Present-but-not-a-finite-number is REFUSED rather than defaulted — silently reading 0 would shift the whole backing track against the score, and a non-finite value would be written back as a bare `nan`, permanently breaking the document. |

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
points are `{position: <grid token>, value: <normalized number>}`. Musical positions are the
persisted truth; seconds are derived caches. A point carries **no segment shape**: the shape
between two points is derived at the Tracktion write seam from the parameter itself (stepped
holds, continuous ramps linearly), so there is nothing per point to author or persist. The
removed `shape` key is the one removed key that is **ignored** rather than refused — a refusal
tripwire exists to stop a package whose authored data a reader would now silently discard, and
`shape` was never authorable, so no package can carry one; nothing to re-import means nothing to
fail loudly about. Authored shapes are a planned continuous-parameter feature
(`docs/plans/todo/authored-curve-shapes.md`).

*Design in flux: `toneAutomation` (and its interaction with `toneChanges`) belongs to the active
automation plan (`docs/plans/completed/tone-parameter-automation-plan.md`).*

# Chart document — `charts/<uuid>.chart.json`

Owned by `common/core/chart/chart_document.cpp`. Grid tokens are `"m:b"` or `"m:b+n/d"`;
fractions are `"n/d"` (or `"n"`). `formatVersion` must be `1` — the parser's single version gate,
like every other format.

| key | type | req | meaning (default) |
|---|---|---|---|
| `tuning.strings` | string[] | req | Per-string tuning labels. |
| `tuning.capo` | int | opt | (`0`) Capo fret, `0..12`. Frets are **absolute**, and `0` means the open string capo'd or not — so the capo never appears as a fret number, and frets `1..capo` are invalid on notes, postures, keyframe fret statements, and fret-hand positions. |
| `tuning.centOffset` | number | opt | (`0.0`) |
| `notes[].position` | grid token | req | Note location. |
| `notes[].string` | int | opt | String index (`0`). |
| `notes[].fret` | int | opt | (`-1` = unset). |
| `notes[].sustain` | fraction | **req**, except on `attack: none` | The actual duration the string rings, strictly positive and always written on every attack that SOUNDS; **absent, and refused if written**, on `attack: none`, which has no ring of its own. Not what is drawn: every surface derives the tail it shows from this (`presentedChartNotes`). A missing key is a malformed document and a non-positive value is refused. The MISSING-key path is the tripwire that actually fires on an old package — the pre-model writer elided the key on every tail-less note rather than writing a zero — so that message is the one carrying the re-import remedy. |
| `notes[].attack` | string | opt | `pinch`\|`legato`\|`leftTap`\|`tap`\|`pop`\|`slap`\|`pickSlide`\|`none`; absent = pick (there is no `pick` token — an explicit one is a read error). **`none` is the one attack that does not sound**: the fretting hand takes this stop SILENTLY, which is the one posture fact a stream of strokes cannot carry (a hand holding six strings and picking four streams identically to a hand holding four). It is a POINT record — `position`, `string` and `fret` are the whole of it — so `sustain` is refused with it and so is every technique key, both through the one fixpoint that says a saved note already equals its own saved form. It is never a STRIKE (it closes no span, ends no posture, bounds no neighbour's ring) but it IS a member, so two members at one slot open a shape whichever kind they are. What a document may NOT carry is one that states nothing: a `none` note the chart's own shapes leave belonging to no posture is removed on load and reported (`sweepInertClaimedStops`, the normalizer's last stage), because such a record draws nowhere on any surface and would be a note the charter can neither see nor select. Design record: `docs/plans/todo/arpeggio-authoring.md`. **No direction is ever stored.** `legato` is the relational claim "this onset connects to its same-string predecessor"; which way it runs — hammer-on or pull-off — is read back from the predecessor at load (`resolveLegato`), and a claim the chart does not justify plays as a plain pick. The writer emits the RESOLVED form, so an unjustifiable claim serializes as a pick and no written document can carry one; the reader settles what it reads anyway, and reports what it converted. `leftTap` is the local claim "the fretting hand strikes this from nowhere" — it needs no predecessor, resolves to the hammer motion always, and (like `tap`) needs a fret or a node to strike. A `pickSlide` note is a right-hand scrape: `fret` is where the scrape starts, `slideOut` is the **required** unpitched terminal, which ends the ring by definition (nothing rings past a scrape), and its `keyframes` state **optional** direction-turnaround frets — pick coordinates, never fingerings, with the whole path always traveling (consecutive neck positions strictly differ, the start fret included). The writer omits the pitched keys on such notes — `mute`, `harmonicNode`, `vibrato`, `tremolo`, `bend`, and any bend or vibrato channel riding a turnaround (in-memory values are session-only overrides) — and the rules reject a document carrying them; `emphasis` is a scrape's own dynamics and IS written, at either end of the axis. |
| `notes[].held` | int | opt | The fretting-hand stop UNDER a right-hand onset — what the OTHER hand is holding on this string while the picking hand sounds it. Legal only where `attack` is `tap` or `pickSlide`, because only there is `fret` not the fretting hand's own stop; on every other attack it is **refused**, through the same fixpoint that refuses a silent hold's techniques. Absence is a MEANING (the hand states no stop of its own) so the key is elided, and `0` is a real statement — the open string a voicing deliberately leaves — not that absence. Refused anywhere inside the note's TRAVELED range — its own `fret`, every keyframe fret statement, and the `slideOut` terminal (`travelsThroughFret`): the planted finger is on the string the whole time, so the sounding path cannot start on it, end on it, or pass through it; the equal-fret case is only the degenerate end of that rule. The board and the capo bind it exactly as they bind `fret` (past the board clamps, on or below the capo is refused). It is a CLAIM at this note's slot, read through the same one query as a `none` note's fret (`claimedStop`): the string becomes a posture string, it counts toward the two-member threshold, it justifies a shape the hand alone stated — a tap carrying one is ONE record at ONE slot, which is what makes the same-instant case expressible at all — and a stop on a new string mid-shape splits that shape. A `held` the chart's own shapes leave stating nothing is cleared on load and reported (`sweepInertClaimedStops`), taking the field and never the note: the onset under it is a sound the charter wrote. Design record: `docs/plans/todo/arpeggio-authoring.md`. |
| `notes[].palmMute` / `.dead` | bool | opt | Written only when true, and **independent** rather than exclusive: the two hands are doing two different things and can do them at once — a dead string inside a palm-muted chord is ordinary charting, and one mute axis could not write it down. `palmMute` is the picking hand's palm on the strings (still pitched, damped); `dead` is the string deadened into an unpitched click, named for the technique because either hand can be the one deadening. A note carrying both sounds and scores as a dead note — the palm flag on it is charting truth about where the hand is, not a third sounding state. The removed one-axis `mute` key (`palm`\|`full`) is **refused** rather than ignored, so an un-reimported package fails to load with a message naming the fix. |
| `notes[].harmonicNode` | number | opt | Harmonic node position in fret units, **and the assertion that the note is a harmonic** — there is no separate harmonic key. In `(0, 48]`, strictly beyond the physical stop (the fret, or the capo when `fret` is 0), and additionally on the neck (`≤ 30`) when the fretting finger is the one touching it. A `pinch` attack must carry one. The removed `harmonic` and `touch` keys are **refused** rather than ignored, so an un-reimported package fails to load with a message naming the fix. |
| `notes[].vibrato` | string | opt | `narrow`\|`wide`; absent = not shaking (there is no `off` token HERE — an explicit one is a read error, the same shape the absent `pick` attack has, and the writer can never produce it). The shake at the ONSET — the vibrato channel's opening statement, which holds until the first keyframe that states the channel again, so a note shaking end to end simply states it here and never restates it. The two words are a WIDTH: **`narrow` is the ordinary vibrato every player uses**, named for what it physically is — a fraction of a semitone of excursion, which is what the board's own drawn depth says — and never an instruction to hold back; `wide` is the deliberate exaggeration above it, the opposition published notation draws with two different squiggles. The removed boolean spelling is **refused** rather than read, with the re-import remedy. |
| `notes[].tremolo` | bool | opt | Written only when true. Means UNMEASURED noise picking (as fast as possible, no real timing) — the charting standard spells out measured fast repetition as discrete notes instead. Deliberately NOT a channel like `vibrato`: re-picking has no mid-ring "start", because a change of picking would be new onsets rather than a state change. |
| `notes[].emphasis` | string | opt | `accent` \| `ghost` — how hard the note is struck. The third value, `normal`, is the implied default and **never serializes**, so spelling it is a read error exactly like an explicit `"pick"` attack. One axis rather than two flags, which makes loud-and-quiet-at-once unrepresentable rather than a rule to enforce. Composes with everything, scrapes included (an accented scrape is an aggressively played one). The removed `accent` bool is **refused** rather than ignored, so an un-reimported package fails to load with a message naming the fix instead of silently losing every accent. |
| `notes[].bend` | number | opt | (`0.0`) How far the string is already pushed at the ONSET, in semitones — the bend channel's opening value, and the whole of what a pre-bend is. Never negative (a finger cannot lower a stopped string; dips and dives belong to the whammy bar's own model). Zero is the unbent onset and never writes. The removed bend-CURVE array is **refused** rather than read, so an un-reimported package fails to load with a message naming the fix. |
| `notes[].keyframes[]` | object[] | opt | The note's ONE interval payload: `{offset: <fraction> req, fret opt, bend opt, vibrato opt}`, ascending, each offset strictly inside `(0, sustain]`. A keyframe fixes a MOMENT and carries any SUBSET of the channels that can change while a string sounds, so a glide target, a bend value and a vibrato change authored at one instant are one record rather than three coincident copies of an offset (`docs/plans/todo/unified-waypoint-model.md`). Each channel reads independently: `fret` interpolates between fret-STATING keyframes (equal frets = a hold, different = travel) and a fret-less keyframe is pass-through for position; `bend` interpolates from the onset value and holds flat past the last statement; `vibrato` holds from each statement until the next. The vibrato channel spells `off`\|`narrow`\|`wide` — all THREE are legal here, because a keyframe is the one place the chart can say the shake ENDS, and a step between the two widths is as much a statement as a start or a stop. Absence of a channel is a MEANING, so a stated `0` bend (a release) and a stated `"off"` vibrato (a shake ending) are both written out; the removed boolean spelling is refused here too, with the same re-import remedy the onset key gives. A keyframe stating NO channel is refused. A stated `fret` never sits on a later onset of the string (a shift glide ends the minimum sustain distance before its re-picked landing) and lies strictly before the ring's end when `slideOut` is present. The removed `slides[]` and `waypoints[]` keys are **refused** rather than read: `slides[]` was the fret-only array this replaced, and `waypoints[]` was this same array under its earlier NAME (renamed 2026-08-27, the shape untouched). |
| `notes[].slideOut` | int | opt | The fret an unpitched slide-out gestures toward: pressure releases and the pitch falls away off the note's END. It stores no offset, because it needs none — the release happens at the ring's end by definition, so there is no second coordinate to drift from it. Absent = the tail just ends. The removed object form (`{offset, fret}`) is **refused** rather than read. |
| `fhps[]` | object[] | opt | `{position req, fret (0), width (4; omitted when 4)}`. |

The removed `chords[]`, `shapes[]` and `holdMarkers[]` keys are **refused** rather than ignored, so
an un-reimported package fails to load with a message naming the fix. Hand-posture spans and the
postures they held are derived from the notes wherever they are read (`deriveChartShapes`), because
a span is a statement about the notes under it and a stored one could only ever disagree with them.
Chord names and fingerings went with them — nothing authored either; when they are authored they
become a dictionary keyed by a posture rather than fields on one. The one posture fact no function
of the note stream can distinguish rides in the stream itself as `attack: none`, which is why there
is no second array for it to live in.

Unknown enum tokens are hard read errors; chart *rules* (ordering against the tempo map) are
validated after load, not by the parser. The one exception is the note array's own ORDER: `notes[]`
must be sorted by (position, string), and the parser refuses a stream that is not — rather than
reordering it — because the normalizer that runs before validation binary-searches the stream for
each note's next same-string onset. Duplicate onsets stay validation's, which is where the rest of
the same rule lives.

A `(position, string)` pair is a **slot**, and `notes[]` is the one array keyed by it — silently
held stops included, which is what turned the old two-array disjointness rule into plain slot
uniqueness. One authority spells the order (`chartSlotOrderLess`), read by the editor's selection
keys and hit resolution as well as by the stream itself.

**No note references** (updated 2026-07-23, superseding the short-lived `slideEnd: "next"`
adjacency terminal): payloads never reference other notes — not by ID (which would make
nonsensical targets representable) and not by a dedicated terminal shape. A shift-slide glide
stores the fret it glides toward as ordinary pitch-curve data: chart truth of the gesture
itself, ending the minimum sustain distance before the re-picked landing rather than pointing at
it. linkNext identity-fragmentation stays rejected.

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
  song.json; the chart parser; the tone-document parser; `project_io.cpp`) — no other call site
  may test a version. The migration ladder that replaces the hard gates is roadmap plan 10.
- **Normalize, don't reject**: save is publish, so readers repair what they can (blank metadata,
  dropped-incomplete normalization, missing catalogs, defaulted fields) and reject only
  structural violations (bad ids, missing referenced files, malformed tokens, unknown enums,
  tempo-map rule breaks, non-FLAC audio).
- **Round-trip stability**: default-valued optionals (`startOffset` 0, FHP `width` 4, false
  booleans, pick attacks) are omitted on write, so packages that predate a feature round-trip
  byte-for-byte. ABSENT is what defaults — a `startOffset` present with the wrong type is
  refused, not silently defaulted, because a wrong type is a malformed document rather than an
  old one.
- Save validates every chart/tone reference *before* any side effect, so a bad reference fails
  cleanly with nothing half-written.
