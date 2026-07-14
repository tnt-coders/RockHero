# Deferred plan — 2D side-scrolling tab view in the game

**Status: ABSORBED into docs/roadmap/30-game-2d-tab-view.md (2026-07-11).**

This todo doc's analysis was superseded and promoted after a multi-angle design review. The
roadmap plan carries the full decision record: the adopted architecture (shared JUCE notation
paint core + layout manifest in common, game-side tile-strip renderer over the proven
JUCE-image→bgfx seam, feedback via the overlay/sprite animation ladder), the rejected options
with evidence (style-token dual renderers, the renderer-agnostic draw-command layer,
NanoVG-for-bgfx, orthographic highway reuse — including why this doc's original Option A tiling
concerns and its "wait for plan 40" sequencing advice were revised by the code audit), and the
open questions (30-Q1 design-doc amendment, 30-Q2 sequencing, 30-Q3 setting home).

Do not execute from this file; read docs/roadmap/30-game-2d-tab-view.md.
