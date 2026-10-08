# Incremental Chart Derivation

Deferred plan: make an edit re-derive only the stretch of the chart it can have changed, instead of
the whole note stream. Re-verify against the code before executing; nothing here is built.

## Why

Every note edit derives the chart's notes layer (`chartResolutions`) once — the controller memo and
the plan gate's seed already removed the second derivation — but that once is O(N) in the song.
Measured at about 0.85 µs per note for the derivation, and the scene projection
(`makeChartViewState`) is the larger cost: about 95 ms per press at 36k notes, past a 16.7 ms frame
long before that. A held-key gesture pays both on every press.

## Shape

The expensive pass is the shapes walk (`deriveChartShapes`) and everything that reads its spans.
Its state at any slot is a function of the stream before it, and an edit changes the stream only
from its earliest affected note on (the plan gate already computes that set: `affectedIndices` in
`chart_edits.cpp`).

1. **Checkpoint** the walk's state per slot (or per bar, if per slot costs too much memory) in the
   notes layer.
2. **Resume** from the checkpoint just before the earliest affected note, on the candidate stream.
3. **Walk until the state re-converges** with the old checkpoints at the same slot: from there on
   the old derivation is the new one, shifted by the edit's change in note count.
4. **Splice** the re-derived range into the old layer: spans, postures, holds, held stops, ink ends,
   held lengths, and the connections.
5. **Judge only the re-derived range**: the coverage gate asks only the positions standing inside
   it, and the projection rebuilds only the notes and spans in it (the scene would need the same
   checkpoint-and-splice shape over seconds).

## Obstacles

- **Comparable walk state.** Re-convergence needs the walk's state to be a value that compares
  equal exactly when the rest of the walk would be identical: the open spans, the carried rings,
  the pending landings. Today that state is spread across locals of one function.
- **Renumbering the posture table.** `ChartResolutions::shapes` indexes `postures` in
  first-appearance order, so a splice that adds or drops a posture renumbers every later index.
  Either the table is keyed by value, or the splice renumbers the tail.
- **The tail law and the hand layer read across the range's edges** (a ring's ink end reads the next
  head; a hand event's front reads the previous figure), so the re-derived range must widen until
  those reads are inside it.

## Pin

Incremental equals full: replay a corpus of edits (every verb, at every slot of a corpus chart)
and assert the spliced layer equals `chartResolutions` of the candidate stream, field by field,
and the spliced scene equals `makeChartViewState` of the result. The derivation's existing suites
stay as they are; this is an equivalence pin, not new behaviour.
