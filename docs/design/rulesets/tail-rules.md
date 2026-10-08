\page design_rulesets_tails Tail Rules

# Scope

These rules decide what a surface DRAWS of each stored ring: where its ink stops, and where its tail
rests. They move nothing. `ChartNote::sustain` stores the actual duration the string rings, every
surface draws that stored note with every keyframe at its stored instant, and the rules publish only
where each ring's ink stops and where its tail rests. They are presentation and nothing else: no
import, load path or editor verb applies them to the stored note, so the same chart drawn from a
`.rock` file and from a fresh import shows the same tails.

Code cites a rule as **tail rule N**. The rules are implemented once, in the order written, by
`chartPresentation` (`chart_presentation.h` in common/core), which publishes the ink ends and the
tail law's verdict as `ChartPresentation`. Durations here are
note values in whole notes — a quarter is `1/4` in every meter — matching the chart model's own
sustain unit.

Numbers are stable: a rule is never renumbered, a retired rule keeps its number, and a new rule
takes the next one.

# Vocabulary

- **Ink end** — where a surface stops drawing a ring, as an offset from the note's onset; always
  within the stored ring.
- **Binding onset** — the later onset a ring's ink is cropped against (tail rule 1).
- **Ending zone** — the stretch of a bound ring from its crop to the binding head.
- **Onset group** — every note sounding at one grid position: one stroke, across voices.
- **Kept-sustain bound** — the ring duration that earns a tail, `g_minimum_kept_sustain_seconds` in
  `grid_arithmetic.h`, the one place its value is stated.
- **Rests** — a tail the curtain owns past its last always-visible landmark (tail rule 4).

# The numbered rules

1. **Crop to the margin.** The *binding* onset is the first later sounding onset — a different
   grid position, on any string — that the ring does not *pass*, passing meaning running *strictly*
   past it. A ring ending exactly *on* an onset passes nothing and binds there, the common let-ring
   collision. Notes sharing a position — chord members — never bind each other, and binding is
   decided on the sounding position, so a grace note's lead is a real onset. The ink stops one
   minimum sustain distance (`minimumSustainDistance`, the margin read at the binding onset through
   the tempo map and floored onto the tick lattice) before that onset, and never before the note's
   own onset. A ring no later onset binds, or one that ends at or before that crop, draws in full.
   Passing an onset — a tie merged across a neighbour, a cross-voice hold — earns the group its
   tails under tail rule 2 but does not exempt the ring from the crop.

   The margin bounds tails only, never onsets: a run of 32nds keeps every onset as written, its
   tails cropped toward zero and then dropped by tail rule 2. Repeated chords crop too; their
   held-to-the-restrike reading lives in the shape span over them. The crop is the ink's alone, and
   no payload floors it: keyframes standing in the ENDING ZONE stay stored and are neither drawn nor
   judged except under a reveal, and the leg crossing the crop is drawn on its true path toward the
   zone's first keyframe and stops at the crop. So a stored chart may end a slide-out or a bend
   exactly on the next head of its own string. The margin is no authoring clamp: the editor's
   duration verb stops at exact adjacency with the next onset on the note's own string
   (`sustainBoundOf`).
2. **Drop short effect-free tails, per onset group.** A group whose members carry no sustain
   technique (bend, slide, vibrato, tremolo), no deliberate hold, and no *stored* ring lasting
   LONGER than the kept-sustain bound draws no tail on any member. The bound is a duration: each
   ring is measured in seconds through the tempo map, so the same written value earns a tail below
   a crossover tempo and drops it above, and the meter never enters. The verdict is the group's,
   because every string of a chord rings from one stroke and a lone tail beside partners that look
   unsounded is a picture no strum makes: any member earning a tail keeps every member's, while
   each member keeps its own ink end from tail rule 1. The comparison reads the stored ring, never
   the ink end, so a ring held past the bound keeps its tail even where the crop leaves it shorter.
   Vibrato and tremolo protect a tail from dropping but not from cropping.
3. **A dead note draws no tail** unless tremolo or a slide payload keeps it making noise or
   travelling (E25). Its stored ring is untouched: it is the timing the legato adjacency test reads.
4. **The tail law — a tail that shows no technique information RESTS.** The curtain owns everything
   past a note's last always-visible landmark. A verdict-only filter, applied LAST: it reads the
   stored stream, judges, and marks where each tail rests (`ChartPresentation::rested_from`),
   skipping every tail that tail rules 2 and 3 already emptied, and it writes no length — every landmark
   it marks lies at or inside the ink end.
   - **The curtain is universal.** Span furniture is no part of the question: every fretting-hand
     tail rests unless it is still stating at its own end, over open board exactly as under a
     bracket. A right-hand onset is neither a member nor a witness.
   - **The atom is the member.** A plain member rests; a member still stating at its end — a bend
     held to the end, vibrato that never stops, tremolo, a slide-out's travel — never rests.
   - **A handover finishes.** A ring whose string a later strike takes over
     (`ChartConnections::hands_over`), or whose end arrives into the next head, is a transfer that
     completes at the takeover: it rests from its ink end with an empty remainder, every pixel of
     its ribbon stated.
   - **Where the verdict binds.** Resting is the 3D board's form: chug chains, dry arpeggios, plain
     sustained chords and lone plain notes rest ribbonless there, with the rails, boxes and
     hold-pinned heads stating the tenure where furniture exists, and a resting remainder draws only
     inside the sliding reveal window at the hit line (`g_tail_reveal_lead_whole_note`). The 2D lane
     draws every tail to its ink end.
