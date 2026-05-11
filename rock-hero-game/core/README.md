# `rock-hero-game/core`

Game-specific pure gameplay behavior belongs here.

Allowed dependencies: `common/core` and pure standard-library logic.

Do not place audio analysis, rendering, JUCE windows, SDL/bgfx integration, app startup, or editor
workflow here.

The include namespace currently contains only `.gitkeep` so Git preserves the empty public include
root. Remove `.gitkeep` in the same change that adds the first real public header.

First real code should be note matching, scoring, combo/streak rules, calibration math, gameplay
simulation, or state machines.
