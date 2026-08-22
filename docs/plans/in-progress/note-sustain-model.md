# Note sustain model — actual durations stored, presentation derived

**Status:** In progress on branch `note-sustain-experiment`. Stage A building 2026-08-21; stages
B–D are decided only after A is sighted.

**Authored** 2026-08-21 out of the span-end discussion (review item 13), the short-tail hole the
user found in the 2D lane, and the MIDI-playback requirement.

## The model in one paragraph

`ChartNote::sustain` is the **actual duration the string rings** — Guitar Pro's notated duration at
import, what the editor's verbs author. It is strictly positive for every note (a dead note's
damped stroke has a duration too). What a surface **draws** and what the game will **score** is the
**presented** form, derived once per chart revision by `presentedChartNotes` in common/core from
the import policy's three tail rules, which stop being import-time destruction and become a pure
read-side derivation. The legato resolver reads the actual duration under strict adjacency. The
span-implied hold the 3D board pins heads for is derived from the presented and actual forms
together. Nothing that is drawn is stored; nothing that is stored is a guess.

## Why

Today the importer destroys the notated durations (rules 1–3 trim and drop), salvages part of the
information into the stored shape span (rule 11 reads the *pre-trim* ends), and every later reader
that needs a duration the chart no longer has guesses independently: the legato hold test assumes
a hold under the kept bound, the span-implied hold invents one for tail-less strums, the span's end
floors onto its own last strum, and MIDI playback would need a fourth guess. That is the
rule-stated-in-N-places defect class. Storing the truth once moves the only unavoidable guess to
where the information genuinely does not exist — a lossy source format's import — and deletes
the rest.

## Three forms of a note

| Form | Produced by | Read by |
|---|---|---|
| **memory** | the editor's verbs, the importer | everything below |
| **saved** (`savedChartNote`) | memory with a scrape's latent overrides stripped | the document writer, the validator, the resolver |
| **presented** (`presentedChartNotes`) | saved notes through the tail rules | both painters, hit testing, the future scorer |

`ChartResolutions` carries `saved_notes`, `presented_notes`, and the per-note `holds`, computed
together once per chart revision; the projection builds `NoteViewState` from the presented note
and `display_hold_ends` from the holds.

## The presentation rules (moved from import, stated once in core)

Input: the saved stream in chart order; the tempo map. Output: one presented note per input note.

1. **Trim to the margin.** The next *binding* onset is the first later note at a different grid
   position on any string. The presented tail ends at least one minimum-sustain margin (at the
   note's own measure) before it. **Deliberate hold**: a ring that runs *strictly past* that first
   binding onset is presented in full, however many later onsets it crosses (a tie merged across a
   neighbour, a cross-voice hold).
2. **Payload floors the trim.** The tail extends to the last payload point that *changes* something
   — a bend point differing from its predecessor, a waypoint differing from the previous fret — and
   stops exactly there; trailing non-changing points leave with the tail (clipped in the presented
   note, never rescaled: GP's bend curve is anchored to the notated ring). A slide-out is not
   protected payload: its presented terminal compresses back with the tail, floored at the minimum
   slide window and kept strictly after the last surviving waypoint. A scrape's terminal is the
   gesture's end and compresses by the leg rule: a leg starting before the margin line ends on it;
   one starting on or after it halves its distance to the onset.
3. **Drop short effect-free tails, per onset group.** A group that carries no sustain technique
   (bend, slide, slide-out, vibrato, tremolo) on any member, no deliberate hold, and no member whose
   *actual* ring reaches the kept-sustain bound (a quarter note) presents no tail on any member.
   Any member earning a tail keeps every member's.
4. **A dead note presents no tail** unless tremolo or a slide payload keeps it making noise or
   travelling (E25, unchanged in substance, now a presentation rule rather than a stored-field
   repair).

Rules are applied in the order written; rule 3's verdict is shared per group, every other rule is
per note.

## Invariants on the stored form

- `sustain > 0` for every note. Structural refusal (no repair can invent a duration); a missing
  `sustain` key is a malformed document. This doubles as the format tripwire: a chart written
  before this model has zero-sustain notes and refuses with "re-import the package".
- A tail never crosses the next onset on its own string (40-Q2-B). Now a rule of the one
  normalizer (`ChartRepair::OverlappingTail`), run at load, by the importer before its synthesis
  passes, and by the editor's plan gate — one authority instead of an editor-only trim.
- Payload offsets lie within the actual ring (unchanged; the meaning widens).
- A scrape's terminal sits exactly at its actual end (unchanged).

## Legato

`predecessorHoldReaches` is strict adjacency: the predecessor's **actual** ring reaches the
successor's onset. The kept-bound assumption and the margin slack — both compensations for the
trimmed encoding — are deleted, and the resolver reads the predecessor's stored sustain directly
(no span-extended parameter). A chug chained to its restrike (GP tiles durations) justifies its
hammer-on exactly as before; a note followed by a rest no longer does, and the settle sweep
flattens that claim at load and reports it.

## The hold (3D pinned heads, 2D range culling)

`holds[i]` is the presented end, except that a member of a 2+ onset group under a covering
shape span whose presented tail is empty holds for its actual ring, capped at the span's end and at
the next onset on its own string. An all-dead group is choked, as today. Singles hold for their
presented tail.

Structurally this is today's span rule unchanged: `chartHolds` *asks* `chartEffectiveSustains` —
handing it the presented stream, so it extends exactly the members presentation emptied — and caps
each answer at the stored ring. The rule is composed over, never restated.

The cap is a real change of value, though, not a rename, and it is the same change the 2D ribbons
show on the other surface: today's hold is the span's remainder whatever the strum rang for, so a
chugged chord under a long shape shortens from the span's end to its own eighth, and the 3D board
pins its heads for that much less. That is the intended reading — the shape says the hand stays
down, the ring says how long the string sounds — but it must be counted in the stage-A report
rather than described as a no-op.

## What the importer keeps deciding (meaning, not presentation)

Tie merges and legato merges (the canonical long actual durations), grace leads and on-beat shifts
(sounding truth, including the before-beat steal), the scrape gesture's path, the shift-slide
arrival waypoint at `gap − margin` (synthesis: GP states no arrival time), the slide-in scoop
window, dead notes, open-string floors. It stops trimming, dropping, clipping payload, and
ASSIGNING the arrival window to the sustain. Synthesis may still GROW a ring too short to carry the
waypoint it just fabricated — payload has to lie inside the ring, and the note sounds while it
travels — but never shorten one; growing to the landing instead would be inventing a ring the
source never notated, and would turn every such origin into a rule-1 deliberate hold wherever
another string sounds inside the gap. It runs the same-string clamp after every pass that can
lengthen a ring (a re-strike stops the ring) and feeds its two presentation-riding passes —
hand-placement generation at trail-off ends, and shape-span derivation — the presented notes, so
their outputs do not change in stage A.

## Accepted deviations from pixel identity, measured

A golden harness (local only, corpus firewall) dumps every note of every local GP score in the
pre-change saved form and in the post-change presented form; the diff is the proof. Known, accepted
residuals, each counted in the stage-A report:

- A grace note's own lead-length tail (≈60 ms) no longer presents: its principal binds it at the
  sounding position, which the notated grouping used to exempt. Its hold goes with it (9 notes
  measured at A2).
- An on-beat grace that delays only some members of a strum leaves the other members presented in
  full rather than margin-trimmed (their ring strictly passes the fabricated onset).
- Claims after a rest flatten under strict adjacency.
- The 2D lane's derived sub-quarter hold ribbons disappear, and a chug's 3D pinned head shortens
  from its shape span's end to its own ring — the one intended visible change, on both surfaces.

Two more the A1 review measured, both from the same root — the presented rules partition and bind
on the SOUNDING position because the saved form is all they have, where the import policy read each
event's NOTATED beat. Ruled 2026-08-21:

- **A ring ending on a graced beat is an importer defect, not a rule-1 case.** A before-beat grace
  fabricates an onset one lead (a 32nd) ahead of its principal, and the importer left the preceding
  beat's notes ringing to the principal — so the stored ring overlapped the grace, which is not
  what the score sounds like: Guitar Pro plays a before-beat grace by stealing its lead from the
  preceding beat. The fix is the importer's, in stage A2: the notes of the voice's preceding beat
  end at the run's first onset. The predecessor then ends exactly on the grace's onset, rule 1
  trims it to the margin as the import policy did, and a tied predecessor still merges past the
  principal (the tie merge keys on the tie flag, not adjacency) and presents as the genuine hold
  it is. Rule 1 keeps its wording; the margin-wide overrun threshold considered as an alternative
  was a heuristic over a wrong datum and is declined.
- **Rule 3's tail verdict is per sounding position, accepted.** The import policy keyed the verdict
  by notated beat, which put a grace and its principal's whole strum in one bucket. Now a
  before-beat grace carrying a slide no longer earns its chord's tails (the chord drops them, the
  grace keeps its own), and an on-beat grace splits the delayed member out of its strum. Both are
  the sounding grouping, which is the only one the stored form has, and the grace's own head
  separates it from the chord on both surfaces; counted in the stage-A report.

Two more the A2 golden diff measured, ruled 2026-08-22:

- **A quarter note before a graced beat loses its tail (57 notes in 5 of 113 songs).** The grace
  steal leaves it ringing 7/8 of a beat, under rule 3's kept bound, where the import policy read
  the notated quarter and kept a 5/8 tail. Accepted: the ring is the truth and rule 3 is doing what
  it states; dropping the steal reintroduces the ring-through-the-grace defect and exempting stolen
  leads from the bound states the rule twice. The cost is BOTH surfaces, not 2D tail ink alone: 19
  of the 57 sit under no covering shape span, so nothing re-extends them and the 3D pinned head
  goes with the tail; the other 38 keep their span's hold. Flagged for the sighting: a quarter
  before a grace with no tail beside quarters that keep theirs may read as an error in a melodic
  line, and on the highway 19 of them stop being pinned at all.
- **A tie-merged shift-slide origin crossing another string's onset presents to its landing (2
  notes in 1 song).** Its stored ring reaches the landing that re-picks it; that ring runs strictly
  past an intervening onset on another string, so rule 1 presents it whole and the tail runs the
  last margin into the landing's head instead of stopping at the arrival waypoint (the waypoint
  itself is unmoved). The same rule as any cross-voice hold; accepted.

The rest of the A2 diff, for the record (113 songs, 245,866 notes, 22,218 spans):

- **Fret-hand positions are byte-identical** in every arrangement of every song, and **span
  geometry** — every span's position and sustain — is byte-identical too.
- **Three chord TEMPLATES merged**, in one song, in each of its three arrangements (54→51, 53→50,
  48→45 distinct postures), so four to nine spans per arrangement now draw a chord box with one
  string FEWER. The cause is the grace steal, not a grown ring: a note the steal ends exactly on
  an ornament's onset is no longer ringing THROUGH it (the ring-through test is strict), so its
  string leaves the posture and that posture becomes equal to a neighbouring one. Traced case: a
  legato-attacked open string ending exactly on a before-beat grace's onset used to fold fret 0
  into that grace's posture, which is what made it distinct from the identical two-string posture
  on the next beat. Accepted for the same reason the steal is — the string genuinely stopped
  sounding there.
- **3,146 holds change in 50 songs.** 2,775 shorten and 29 end — the intended A1 hold cap, the
  fourth deviation above — and 10 lengthen, the shift-slide case above.
- **342 of those hold changes are a defect, not a deviation.** A DEAD note inside a live chord
  under a covering span loses its pinned head entirely, because `normalizeChart` still applies
  E25 to the STORED ring (7,835 notes ship with `sustain` zero, and every one of them is dead)
  and the hold caps at that zero. That contradicts ruling 5 and the positive-sustain invariant.
  A3 moves E25 out of `normalizeChartNote`; this residual is to be RE-MEASURED there, not signed
  off here.
- **One sus drop, in one song, is unexplained.** An open string storing a half-beat ring, alone at
  its position, which the import policy gave a quarter-beat tail. Under the presented rules the
  drop is correct by inspection (a sub-quarter ring, no technique, no partner and no hold), and
  neither the clamp nor the grace machinery is involved: that arrangement contains no grace at
  all, and the no-clamp control dump is byte-identical to the shipped one. What earned the note a
  tail under the notated-strum key could not be reconstructed from the dumps.

## Stages

- **A** — the data model, pixel-identical: A1 core (`chart_presentation.h/.cpp`, the hold, the
  validator and document rules, tests); A2 importer (emit actual, trimming deleted, tests
  migrated); A3 readers (projection on presented, `display_hold_ends` on holds, resolver strict;
  golden diff); A4 editor verbs (growth clamps at adjacency, shrink refuses zero, insert default one
  grid step, assist to the onset); D1 the 2D ribbon change as its own commit.
- **B** — the editor's Alt reveal: while Alt is held every visible note draws its actual ring as a
  dimmed outline; release snaps back to the presented form.
- **C** — shape spans derived from the notes (rule 12 as today: a posture starts at a ≥2-note
  onset, ringing notes join); `shapes` leaves the format.
- **D** — sighting experiments behind diagnostics toggles: a 3D floor band for the actual ring.

## Rulings recorded (2026-08-21)

1. Held = strict adjacency.
2. No back-compat: every existing chart is re-imported.
3. The 2D hold ribbons change lands as its own commit after A.
4. Scored = presented, a contract recorded here and exposed through the projection; the scorer
   reads it when it exists.
5. Every note's sustain is positive; a dead note carries its notated duration and E25 is a
   presentation rule (the "except dead" wording was serving the same end, and pinning dead at zero
   would have re-broken every legato claim after a muted cluck — the E26 regression reversed on
   2026-08-20). Confirmed 2026-08-21. A quarter-note cap on a dead note's stored sustain was
   considered and declined: nothing visible or audible changes with the length (rule 4 presents no
   tail, playback clucks), the length is exactly the timing information and the legato adjacency
   the stored form exists to carry, and the cap would be one more bound stated in three places for
   a value no surface shows. The same-string clamp is the only bound on any note's sustain.
6. Insert default = one grid step, clamped at the next onset on the string. Rule 12 unchanged.
7. **Proposed 2026-08-21, awaiting the user's confirmation — the lossy-source default.** A
   converted package from the commercial source format stores no duration for a note the charter
   did not mark as held. The plan puts the one unavoidable guess at that import, in the external
   converter: *a source note with no sustain rings to the next onset on ANY string, capped at half
   the kept-sustain bound; a note with a sustain rings for it.* Any string, not its own: rule 1
   presents a ring running strictly past the first binding onset in full, so a chug defaulting to
   its own string's re-strike would draw through every alternating-string riff. Capped strictly
   below the kept bound: a default landing exactly on a quarter would earn a tail under rule 3
   that the source never showed. Consequence to accept with eyes open: the source's sub-quarter
   holds stop drawing, because presentation is one rule for every chart where the package path
   used to skip the Guitar Pro rules. Folds into the stale-package re-export (task #78).
   No accent exception: charters of that format often read an accent as staccato, but the stored
   ring of an accented sub-quarter note changes nothing drawn (the accent glow is the staccato
   read), a shorter default would flatten a hammer-on the charter marked after it, and playback
   can honour the accent itself when it exists rather than the duration storing a convention.
