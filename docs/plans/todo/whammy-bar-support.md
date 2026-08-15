# Whammy Bar Support — deferred seed

Status: **DEFERRED** — design directions user-ruled 2026-08-13 in session; no execution plan yet.
Per the todo-lifecycle rule, re-verify everything here against the current code before executing.

## Model direction (user-ruled 2026-08-13)

- The whammy bar is its **own channel**, deliberately NOT folded into bend (that proposal was
  examined and rejected): the player must see distinct notation for bar use, the bar and a finger
  bend can act **simultaneously** so the two need independent display, and vibrato coexists with a
  held bend, so vibrato stays its own model field as well.
- **Consequence pre-answering W9-K** (`technique-review-walkthrough.md`): with dips owned by the
  whammy channel, a BEND amount can never be negative — a finger cannot lower pitch — so W9-K's
  missing validation resolves as "bend amounts ≥ 0" when ruled formally in the W9 comb.

## Display seed (user idea, unresolved)

- Whammy **TWISTS the note tail** (and possibly the head with it) rather than deflecting it
  bend-style — which also keeps a dive-bomb from plunging below the highway floor (the
  floor-is-origin law makes bend-style notation structurally wrong for deep dives).
- Open: how a twist reads on a **wide tail** (open strings) — no good answer yet.
- A 2D idiom is required too: whammy is notation, so the no-surface-divergence law demands both
  surfaces carry it.

## Keymap

- `W` is reserved (`keymap-matrix.md`, 2026-08-13); `Shift+V` is the recorded alternative if wide
  vibrato is dropped or vibrato strength becomes tunable data. The official chord call is made
  when this work starts.
