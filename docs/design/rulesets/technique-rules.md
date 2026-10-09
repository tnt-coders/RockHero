\page design_rulesets_techniques Technique Rules

# Scope

These rules decide which techniques may combine on a note, and what a combination draws, refuses or
normalizes. A combination is forbidden only when it cannot be executed — the motion contradicts
itself — or when its stored value would describe something that does not exist, such as a pitch
offset on a note with no pitch. The merely unusual is allowed, and an allowance needs no code: a
rule below that allows a combination is kept so that nobody adds a refusal for it later.

Code cites a rule as **technique rule N**. The rules are implemented in three places, by what a
broken rule becomes:

- **Repairs and refusals** read one note. `normalizeChartNote` (`chart_rules.h`) owns every rule a
  repair can express — it strips the technique a note cannot carry, flattens a stranded strike
  (`flattenStrandedStrike`) and demotes a scrape that no longer travels — and
  `validateChartNoteAlone` refuses what no repair can express, then asks that the note already
  equal its normal form. `validateChartNotes` runs both over a stream, so the reader, the importer
  and every editor plan meet the same rules. What a scrape may carry is `savedChartNote`
  (`chart.h`).
- **Resolver clauses** read a note and its same-string predecessor. `resolveLegato`
  (`chart_legato.h`) answers what a legato claim resolves to, so a claim these rules refuse plays
  and draws as the plain pick it sounds like, and refuses no document.
- **The drawn tail** of a dead note is tail rule 3 (\ref design_rulesets_tails), implemented by
  `chartPresentation`.

Numbers are stable: a rule is never renumbered, a retired rule keeps its number, and a new rule
takes the next one.

## Disabled harmonic forms

Only the natural and the pinch harmonic are supported for now. The normalizer reduces a node over a
pressed stop under any attack but `Pinch`, and any node under the `Tap` attack, to a plain note at
the pressed stop, the onset of a tap becoming a pick (`ChartRepair::DisabledHarmonic`), so no chart
holds an artificial or a tapped harmonic until their display is settled. Rules 4, 13, 21 and 23
state what holds for those forms; until they are enabled, the reduction runs before any of them.

# Vocabulary

- **Node** — `ChartNote::harmonic_node`, in fret units; its presence alone makes a note a
  harmonic.
- **Pressed stop** — a stop above the open string: `fret > 0`.
- **Physical stop** — the stop the string speaks from: the note's fret, or the capo where the fret
  is 0 (`physicalStopFret`).
- **Fret-hand harmonic** — a node over the open string under any attack but `Pinch` and
  `PickSlide`: nothing is pressed, and a finger touches the node (`fretHandHarmonic`). The tap
  harmonic over the open string is one.
- **Pinch** — the attack whose thumb graze damps a node off the neck, over the body
  (`nodeIsOnNeck`).
- **Tap harmonic** — a node under the `Tap` attack: the picking hand touches the node.
- **Scrape** — the `PickSlide` attack: the pick dragged along the string, unpitched end to end.
- **Path** — every fret a note's keyframes state. A **slide payload** is a path that states at
  least one fret (`anyKeyframeStatesFret`); the **slide-out** is the fret statement at the ring's
  end.
- **Hammer motion**, **pull motion** — what `resolveLegato` derives for a `Legato` claim
  (`LegatoMotion`). `LeftTap` is the hammer motion stated outright.
- **Sounding position** — where a striking finger meets the string: its fret, or its node.

# The numbered rules

1. **A node lies on the string.** It is strictly positive and at most `g_max_harmonic_node`. A
   refusal.
2. **A scrape carries its path and nothing pitched.** `PickSlide` excludes both mutes, a node,
   vibrato, tremolo and bend, the keyframes' bend and vibrato channels included: the attack keeps
   them in memory so switching it back restores them, and a saved scrape carries none
   (`savedChartNote`; a document carrying one is refused). It requires the slide-out as its
   unpitched terminal, a refusal where it is missing, with any earlier fret statements as optional
   turnarounds. The whole path travels — consecutive frets strictly differ — and a scrape whose
   path stopped travelling becomes the plain pick it sounds like. Emphasis is the scrape's own
   dynamics and is allowed.
3. **Retired.** The pinch's exclusion of every other attack is unviolatable: the pinch is itself an
   attack (`NoteAttack::Pinch`), and a note holds one attack.
4. **A strike from nowhere needs somewhere to land.** `LeftTap` and `Tap` require a positive
   sounding position: a pressed stop, or a node (`nothingToStrike`). An open string with neither
   is a note nothing produced, and its attack flattens to a pick (`flattenStrandedStrike`). A
   `Legato` claim is not bound by this: whether it strikes at all is the resolver's answer.
5. **A connection needs a held predecessor, and a pull-off one above it.** The same-string
   predecessor's stored ring must reach the claim's onset (`predecessorHoldReaches`): a string
   that stopped sounding is a released string. A pull-off needs the fret the predecessor's finger
   ends on — an arrival's fret, else the last fret stated inside its ring (`fretAtRingEnd`) — to
   lie above the note's fret. A resolver clause.
6. **Legato direction derives from that relationship.** Nothing stores it: a predecessor ending
   above the note resolves the claim to the pull motion, one ending below to the hammer motion,
   and an equal fret to nothing. `resolveLegato` is the one authority every surface, the gameplay
   build, the reader and the `L` planner ask.
7. **A fret-hand harmonic excludes the path.** A touch at a node presses nothing, and moving it off
   the node stops the harmonic, so the normalizer clears every keyframe, the slide-out included.
8. **A dead note keeps a node on the neck and loses the pinch's.** A hand standing on a node goes
   on naming where it is once the pitch is gone, as a dead note's own fret does, so the node
   survives and the note stays dead. The pinch's node lies off the neck and names no position, so
   a dead pinch becomes a plain pick and loses its node.
9. **A fret-hand harmonic excludes bend and vibrato.** A touch cannot press the string, so the
   fretting hand cannot modulate its pitch; the whole vibrato width goes, wide and narrow alike. A
   pinch presses a real fret, so a bent or vibrated pinch is allowed.
10. **A dead note excludes bend, and keeps its path.** A bend is an offset from a pitch a dead note
    does not have, so the onset bend — the pre-bend — and every keyframe's bend go. A path names
    places, not pitches, so keyframe frets and the slide-out stay.
11. **A dead note excludes vibrato.** The whole width axis goes, on the note and on every keyframe,
    for the reason bend does.
12. **A pull-off sounds no harmonic.** It releases onto a stopped pitch that rings over the whole
    speaking length, so a claim onto a note carrying a node never resolves to the pull motion. A
    resolver clause.
13. **A fret-hand harmonic allows the hammer motion and `Tap`, and that pairing is the tap
    harmonic.** A finger striking over a node both excites and damps it. The fretting-hand form is
    `LeftTap` on a node; a `Legato` claim never resolves onto an open-string node, since no
    predecessor ends below fret 0. The picking-hand form is `Tap` on a node, disabled for now. No
    harmonic kind is needed for either: the attack already says which hand struck.
14. **`Slap` and `Pop` allow a fret-hand harmonic.**
15. **`Slap` and `Pop` allow every mute**, the slapped dead note included.
16. **Vibrato and bend compose.** Vibrato oscillates around whatever the bend reaches.
17. **Every attack allows a slide payload** except where rules 2, 7 and 28 decide the path: the
    attack names the onset and the path the sustain, so a note hammered onto and then slid is two
    moments, not two claims about one transition.
18. **Tremolo allows a path and a bend.**
19. **A fret-hand harmonic predecessor justifies no connection.** A touch holds nothing to hand
    over, so neither motion resolves from one. A pinch presses a real fret, so a connection from a
    pinch is allowed. A resolver clause (`fretHandHarmonic`).
20. **A pinch requires its node.** The thumb's landing decides which overtone squeals, so a pinch
    without a node is missing data. A refusal, and what lets a node's presence alone assert the
    harmonic.
21. **A node lies beyond the physical stop**, strictly: nothing vibrates at or behind the stop, and
    a node at the stop is the stop. The rule never asks which hand made the onset. A refusal.
22. **A fretting finger on a node stays on the neck.** Where the fretting hand touches the node — a
    fret-hand harmonic under any attack but `Tap` (`frettingFingerOnNode`) — the node lies at or
    below `g_max_fret`, which also keeps the derived hand window on the board. A pinch and a tap
    harmonic are bounded by rule 1 alone (`harmonicNodeCeiling`). A refusal.
23. **A tap harmonic excludes tremolo.** The tapping finger leaves the string, so nothing holds the
    node under re-picking, and the normalizer drops the tremolo. A natural or pinch harmonic keeps
    tremolo. Tap harmonics are disabled for now, so the reduction removes the node first.
24. **A dead note allows the hammer motion, the pull motion and `Tap`** — the muted legato. Rule 4
    still binds the strike forms.
25. **A dead note's tail is tail rule 3** (\ref design_rulesets_tails), stated there once; the
    combination keeps its number here.
26. **Retired.** A dead predecessor is an ordinary one: rule 5's hold test bounds it like any other.
27. **A scrape predecessor justifies no connection**, by either motion: its travel is the pick's
    position, so no finger waits at its end. The note after a scrape is picked. A resolver clause.
28. **An open string allows a bend** — the behind-the-nut bend, read as semitones above the open
    pitch. Only the path goes: nothing stops an open string to travel, so the normalizer drops its
    fret statements (`ChartRepair::OpenStringSlide`) and leaves its bend. The hand sits at the first
    playable fret (hand-position rule 13).
