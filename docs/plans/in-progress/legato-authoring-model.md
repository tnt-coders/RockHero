# Legato Authoring Model — hammer-on, pull-off, and the left-hand tap

Status: **OPEN DESIGN, parked 2026-08-07** at the user's direction — *"I want to decide on the note
head shape for pick slides first and then maybe come back to this conversation and have Fable take an
in-depth look at it to really analyze the potential pitfalls."* Plan 40 Phase 5's first verb (`H`)
shipped in `4a98da55` with the naive derivation; this document records what that verb got wrong, what
is settled, and the two questions that are genuinely open. Nothing here is implemented yet beyond
what that commit contains.

Scope note: this concerns **legato only**, and that is a finding rather than an assumption — see
[Does this generalize?](#does-this-generalize) at the end.

## Why this exists

`planSetLegato` derives hammer-versus-pull from the fret relationship to the previous note on the
same string, once, at authoring time. Every input to that derivation stays editable afterwards, so
the stored attack can drift out of agreement with the data that justified it. The user also spotted
that the derivation's *refusals* are wrong, because `NoteAttack::Hammer` is overloaded.

## The load-bearing fact: Hammer and Pull are not symmetric

Already recorded in the importer (`gp_chart_builder.cpp`, the `left_hand_tapped` branch):

> A left-hand tap is the fretting hand hammering the note from nowhere (no pick stroke), which the
> hammer-on states accurately — no separate notation. **Always a hammer, never a pull: nothing is
> released to sound it.**

So in this project's model a left-hand tap **is** a hammer-on, `NoteAttack::Tap` is the right-hand
tap only, and the two readings of `Hammer` are deliberately indistinguishable. Therefore:

- **`Hammer` needs no predecessor.** Its only precondition is **`fret > 0`** (user, 2026-08-07: "any
  note above fret 0") — you cannot hammer onto, or left-hand tap, an open string.
- **`Pull` requires a preceding note on the same string at a higher fret**, because something must
  be released to sound it. Its *target* may be fret 0: pulling off to an open string is ordinary.

`chart_rules.cpp` validates neither today.

## Settled

### 1. The derivation, and a force verb (user: "I think I agree")

| Situation | Result |
|---|---|
| predecessor on the same string at a **higher** fret | **Pull** |
| predecessor lower, equal, or absent — and `fret > 0` | **Hammer** (hammer-on or left-hand tap, indistinguishable by design) |
| `fret == 0` with no higher predecessor | **refuse** — neither articulation is physically possible |

One refusal survives, and it is a genuinely impossible note rather than a gap in the inference. The
shipped verb refuses three cases; two of those are legitimate left-hand taps it should be authoring.

**`Ctrl+H` forces `Hammer`** (user's proposal), valid iff `fret > 0`, covering the rare descending
left-hand tap. It fits the grammar: `Ctrl` means *precision* when placing, and "I will state it
exactly, do not infer" is that idea applied to a typed verb. There is deliberately **no force-Pull** —
pulling off to a higher fret is physically impossible, so its absence is principled.

Setting `Hammer` overwrites a conflicting attack for free, since `attack` is a single field; a
scrape's path already leaves with the attack.

### 2. The toggle must measure the eligible subset (user: "I also think I agree")

The shipped toggle computes `all_legato` over the whole selection, so a selection containing a note
that can never be legato (an open string) can never satisfy it — the second press keeps trying to
*set*, and the legato it applied on the first press becomes unclearable by `H`.

Fix: compute the toggle over the **eligible subset**. If every note that *can* be legato already is,
clear. That guarantees a second press undoes the first, which is what makes a toggle a toggle.
Rejected alternative: "if any is legato, clear" — it breaks extending legato across a group.

### 4. No distance bound, and other strings are already handled

The scan takes the last earlier note on the **same string**, so an intervening note on another string
is correctly ignored (user's case 4). Do **not** bound the derivation on time distance: a far
descending predecessor is probably a left-hand tap, `Ctrl+H` states that in one keystroke, and a
threshold would be a magic constant guessing at what the user can say exactly.

**Trap to avoid:** do not require `Pull`'s predecessor to still be *sounding*. Charts legitimately
carry zero-sustain notes where a pull-off is correct — `sustain` is the drawn tail, not the physical
ring — so that check would break ordinary charts.

## Open — question A: what normalization does on an invalidating edit

The user's correction to the first proposal, which had `Pull` degrade to `Hammer`:

> "Pull would only become hammer if the preceeding note becomes a lower fret than the pulled off
> note. Moving a of a note preceding a pull off to the SAME fret as the pull off should not
> automatically assume that the pull off is now a left hand tap."

So the principle is **do not invent an articulation the user did not assert**. Equal frets mean *no
direction*, which is not the same as *a left-hand tap*:

| Edit | Repair |
|---|---|
| `Pull`, predecessor becomes **lower** | → `Hammer` (a genuine hammer-on now) |
| `Pull`, predecessor becomes **equal** | → clear to `Pick` (no direction; do **not** infer a left-hand tap) |
| `Pull`, predecessor deleted or moved off the string | → clear to `Pick`, by the same principle |
| `Hammer` retyped to `fret == 0` | → `Pull` if a higher predecessor exists, else clear to `Pick` |
| `Hammer` on a descending pair | **untouched** — possible as a left-hand tap |

That last row is why the earlier framing was "normalize impossibility, never preference": it lets a
deliberate `Ctrl+H` survive later edits without the format recording that it was forced.

### The tension the user identified, stated plainly

> "IF you have 5 followed by 3 on the same string and 3 is marked as a pull off, changing the 5 to 3
> would clear the pull off status of the 2nd note... But then if you change the first note AGAIN down
> to 2 I would think you would want the 2nd note to be converted to a hammer on. But this could get
> dicey because what if you change it, clearing the pull off, then 10 minutes later change it again
> and it generates an unexpected hammer on? That would feel strange to the user."

These two wishes are in **direct conflict**, and no tuning reconciles them:

- *Restore the legato when the relationship becomes valid again* requires re-derivation to resurrect
  a cleared note.
- *No surprises later* requires that a cleared note stay cleared.

Under "normalize impossibility only", the conflict resolves toward the second wish for free: once the
note is `Pick`, nothing resurrects it, because `Pick` is never impossible. The 10-minute surprise
cannot happen. But the user's first wish is then unmet — the 5→3→2 sequence leaves a plain pick where
they expected a hammer-on.

Options, none chosen:

- **A1 — impossibility only (no new state).** Cleared stays cleared. No surprises, no resurrection.
  Cheapest; the 5→3→2 case disappoints.
- **A2 — a settle timer**, the user's own suggestion, reusing the multi-digit fret-entry window
  (`EditorUndoHistory::replaceTop`, 1.5 s) so an in-flight retype does not commit the clear. This
  fixes 5→3→2 *within* the window and does nothing for it after, which arguably makes the behavior
  harder to predict rather than easier — a rule whose outcome depends on how fast you typed.
- **A3 — store the legato INTENT separately from the derived direction.** A field meaning "the user
  asserted legato here", with hammer/pull derived from it at projection time. Intent persists across
  edits, direction re-derives whenever the relationship changes, clearing is an explicit user action,
  and no surprise is possible because the direction only ever flips *within* a note already marked
  legato. This is the only option that satisfies both wishes. Cost: a format change routed through
  `docs/plans/roadmap/10-format-versioning-and-chart-identity.md`, and it makes the stored
  hammer/pull advisory — which is the derived-over-authored direction the project already prefers.

## Open — question B: should the left-hand tap be its own concept?

The user, on being told that `Hammer`'s overloading is what makes normalization awkward:

> "Your last line that you say is important on #3 is what is making me consider maybe we SHOULD treat
> left hand tap as its own concept here... but then it would probably need to be displayed slightly
> different in both the 2D and 3D view so the user is aware. (White box with a T in it for 2D and ....
> I really don't know for 3D?) It's something worth considering if it genuinely simplifies the editing
> workflow."

What separating it would buy:

- `Hammer` regains a real precondition (an ascending predecessor), so its validity is checkable and
  normalization becomes symmetric with `Pull` — one rule instead of an asymmetry.
- The refusals become honest: a note with no predecessor is not a hammer-on, it is a left-hand tap,
  and the editor would say so.
- `Ctrl+H` may become unnecessary, since the rare descending left-hand tap would be its own verb.

What it costs:

- A format change (a new `NoteAttack` value) through plan 10, plus importer work — the GP importer
  currently *collapses* left-hand taps into `Hammer` deliberately, with a recorded rationale that the
  hammer attack "gives the right downstream behavior automatically — the note anchors the fret hand,
  closes chord spans, and never floats above the window, all of which are Tap-attack special cases."
  A new value must reproduce all of that or it regresses the hand-window and chord-span behavior.
- New notation on **both** surfaces. The user's 2D sketch is a white box with a `T`, which collides
  with the existing lettered-plate `T` for the right-hand tap — the plates are black, so a white box
  distinguishes them, but "two T marks meaning different hands" is a legibility question for the
  texture/notation pass, not an assumption. 3D is genuinely unknown and would need an atlas cell.
- One more chord.

**The decision test the user set:** adopt it only "if it genuinely simplifies the editing workflow."
Note that A3 and B interact — if intent is stored (A3), a left-hand tap is simply "legato intent with
no valid predecessor", which may deliver B's clarity without a new attack value.

## Does this generalize?

Checked against every remaining Phase 5 field: **no, and that is load-bearing for sequencing.**

| Field | Neighbour-dependent? |
|---|---|
| `mute`, `vibrato`, `tremolo`, `accent` | no — unconditional |
| `harmonic` + `touch` | no — `touch` only with a harmonic is an *intra-note* constraint |
| `attack`: `Tap` / `Slap` / `Pop` / `Pick` | no |
| `attack`: `PickSlide` | no — its constraints are intra-note and already enforced by the chart rules |
| `attack`: `Hammer` / `Pull` | **yes — the only one** |

Legato is the only articulation whose meaning references another note, so this blocks none of the
other seven verbs. It **does** land on Phase 6 (L-link merge/split is explicitly relational) and
Phase 7 (slide waypoints reference target frets), which is the argument for settling it here as
precedent rather than later under pressure.

## For the deep review

The user wants Fable to analyze the pitfalls in depth. The questions worth putting to that review:

1. Does A3 (stored intent, derived direction) actually satisfy both of the user's wishes, or does it
   move the surprise somewhere else — for instance, a note whose *displayed* articulation changes
   while its stored intent did not?
2. Is there a fourth option neither of us saw, particularly one that avoids a format change?
3. Does B collapse into A3, or are they genuinely independent?
4. What breaks in the FHP generator, chord-span derivation and the 3D hand window if a left-hand tap
   stops being a `Hammer`? The importer comment claims those behaviors ride on the hammer attack.
5. Enumerate the invalidating edits exhaustively — the six known are delete, move-off-string,
   retype-to-equal, retype-to-invert, move-earlier-than-source, and insert-between — and check
   whether any *other* verb in the family can invalidate a legato note as a side effect.
