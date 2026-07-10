# `rock-hero-game/ui`

Game-specific presentation and rendering belongs here.

Allowed dependencies: `game/core`, `game/audio`, `common/audio`, and bgfx/SDL dependencies when
real rendering code requires them.

Do not place scoring rules, audio timing policy, editor UI, shared JUCE windows, or app startup
here.

Real code starts with the `surface/` feature: the SDL3 game window, the bgfx render device, and
the `GameShell` frame-loop composition point (plan 20 loop model L2). Note highway rendering,
HUD, visual feedback, and gameplay-surface input handling build on top of it.
