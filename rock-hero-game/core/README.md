# `rock-hero-game/core`

Game-specific pure gameplay behavior belongs here.

Allowed dependencies: `common/core` and pure standard-library logic.

Do not place audio analysis, rendering, JUCE windows, SDL/bgfx integration, app startup, or editor
workflow here.

First real code should be note matching, scoring, combo/streak rules, calibration math, gameplay
simulation, or state machines.
