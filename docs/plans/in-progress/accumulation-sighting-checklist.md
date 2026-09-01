# The Accumulation Close-Out Sighting Checklist

The one list for closing the accumulation arc (seam `52c5b755` + the clip/tail mini-seam) before
the span-marker Phase 1 begins. Everything below is EITHER a sighting item (look at it in the
editor, on real material), a re-signing item (numbers), or a known-absence (do not hunt for it).
Check items off here; anything that sights wrong gets a note beside it, and the arc is closed
when every box is ticked or consciously carried.

## A. The #141 gate — the accumulation law itself (2D lane, on corpus material)

- [ ] **The absorbed-landing figure** — the original gate: a landing absorbed into a standing
      span draws as ONE statement (no phantom bracket at the landing; the deferred bracket at the
      first interior onset).
- [ ] **The successor-bracket population** — the single largest new-ink change (~2,631 arpeggio
      spans, was 736): successors opened by a member's death or a landing now draw their own
      brackets at their first interior sounding. Scan a few dense songs: does the extra bracket
      ink read as structure or as clutter?
- [ ] **Drone-under-stabs** — repeated stabs over a ringing drone: ONE bracket the whole way,
      the stabs wearing their chord boxes and repeat boxes INSIDE it (the founding flip made the
      boxes-within-arpeggio ruling actually reachable).
- [ ] **Slide-heavy material** (Q7's narrowing): a staggered landing beside a non-travelling
      ring opens a span — grouping only; NOTHING new should draw at the landing itself. Confirm
      slide passages stay visually calm.
- [ ] **A lone re-pick figure** — one glance that a re-picked member still rides its span
      (population 1,926 corpus-wide; any sustained-chord-with-re-picks passage shows it).

## B. Bracket digits (the walk's display rulings)

- [ ] **Accumulated members' digits print in the bracket** — the missing-digit bug that started
      the #3 walk: a member that piles in late shows its number in the opening bracket.
- [ ] **Held digits print in the bracket** as membership digits (derived-held taps that joined a
      span).

## C. Satellites (the final authorship-keyed law)

- [ ] **A derived-held tap fronting a bracket**: standing satellite beside the bracket (the
      displaced posture digit), visible with nothing selected.
- [ ] **A mid-span derived-held tap**: NO standing ink; select the note (or Alt-reveal) and the
      satellite appears with the derived fret; press it — the caret arms the held channel, typing
      is REFUSED (red pending box), the chart untouched.
- [ ] **Selection survival**: select a tap, then press its satellite — the selection stands.
- [ ] **Legibility at real zoom**: satellites readable, not colliding with heads.
- KNOWN-ABSENT (do not hunt): a lone span-less AUTHORED claim's standing satellite is unbuilt —
      deferred to the span-marker redesign; imports produce none, so the corpus cannot show one.

## D. Let-ring (the clip; the A2 gate is REVERTED 2026-09-01)

- [ ] **The motivating figure** (the user's cited opening measures): the texture's rings end at
      the new chords exactly as measured — no tails drifting into the next span.
- [ ] **Drone-under-melody figures**: a let-ring drone struck ALONE under a MOVING melody rings
      through it (growth is never foreign). A drone CO-STRUCK with a note on the melody's string
      CLIPS at the melody's first move — the accepted, watch-itemed defect (the A2 gate that
      spared ~800 such rings was reverted the day it was ruled, acquitted of the sighted spill
      but unvalidated on real material). A wrongly-cut drone sighted here IS that watch item's
      trigger (docs/tracking/watch-items.md), and its remedy menu is pre-measured in the chart
      ruleset's A2 entry — do not tune the clip ad hoc.
- [ ] **THE WATCH-ITEM TRIGGER** (docs/tracking/watch-items.md): if let-ring figures still read
      wrong after all of this, the diagnosis is that arpeggio notation cannot notate "let ring"
      — the explicit let-ring notation question (#131) activates.

## E. Arpeggio staircase tails (the mini-seam's display change)

- [ ] **Tails visible inside arpeggio spans**, each clipped at the next onset — the staircase.
- [ ] **A span ENDING on a long hold shows its tail** (the motivating oddity), and a mid-span
      long hold shows its clipped one.
- [ ] **A chord absorbed inside an arpeggio span shows a BLOCK of parallel tails** ending
      together at the next later onset — co-struck members never clip each other (the "next
      onset" is strictly later by definition), so no zero-length nubs. Stairs for runs, blocks
      for chords.
- [ ] **A gliding member inside a bracket now shows a tail ending AT ITS LANDING** (the clip +
      the payload floor compose there) — a visible change on slide-heavy arpeggio material the
      build flagged for sighting.
- [ ] **Expect brackets to look substantially BUSIER than pre-seam** — the old rule hid more
      than the motivating oddity suggested (whole figures drew nothing); judge the new density
      deliberately.
- [ ] **Sub-1/4 members show NO tail** — the clip composes with the standard short-note display
      rules; in-span behaviour identical to out-of-span for equal presented lengths (sighted
      wrong 2026-09-01, fixed, re-sight).
- [ ] **A member ringing PAST THE SPAN'S END always shows its tail** — the explicit exception
      (user-ruled 2026-09-01): the ring outliving the held shape is the information; it is
      exempt from the staircase clip.
- [ ] **The standard suppression rules still apply on top** (muted members etc. unchanged).
- [ ] **2D and 3D agree** — the highway's tails match the lane's presented ends.

## F. 3D highway cross-checks (surfaces must not diverge)

- [ ] The successor brackets/boxes and the staircase tails read correctly on the highway, not
      only in the lane.
- [ ] One playback pass over a dense corpus song: nothing new flickers, nothing obviously
      mis-grouped in motion.

## G. Numbers to re-sign (not visual)

- [ ] The census after the A2 revert — RUN 2026-09-01: the clip counters are back EXACTLY
      (2,321 rings / 5,254 beats), and of the three rows the A2-era run had flagged, two
      returned to their signed values to the unit (arpeggio spans 2,631; strike-less floor 2).
      ONE ROW STAYS RED, and it is the bracket law's, not A2's: trigger-4-only flips reads 6
      against the signed 69 — the counter asks whether an earlier PRESENTED tail crosses the
      span start, and `clipArpeggioTails` now ends bracketed members' presented tails at the
      next onset, so the crossing population collapsed with the display law. Nothing was
      re-pinned; the row needs the user's re-signature (or a deliberate counter re-aim) when
      the bracket-law sighting is judged.
- [ ] The mini-seam commit itself, after its verification.

## H. Carried consciously into Phase 1 (not sighted now)

- The span-marker Phase 1 items (Shift+S, the `"span"` record, `None` ripped out, the opening
  slot published on ChartShape).
- The scoring rider (arpeggio hold bonus, minimum-distance clipping) — the note-detection plan
  revisits DEFINITIVELY.
- The tap-harmonics open area, the template system, and everything else in
  docs/plans/todo/span-marker-redesign.md.
