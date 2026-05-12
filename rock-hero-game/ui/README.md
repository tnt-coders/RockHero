# `rock-hero-game/ui`

Game-specific presentation and rendering belongs here.

Allowed dependencies: `game/core`, `game/audio`, `common/audio`, and bgfx/SDL dependencies when
real rendering code requires them.

Do not place scoring rules, audio timing policy, editor UI, shared JUCE windows, or app startup
here.

The include namespace currently contains only `.gitkeep` so Git preserves the empty public include
root. Remove `.gitkeep` in the same change that adds the first real public header.

First real code should be bgfx surface integration, note highway rendering, HUD, visual feedback,
or gameplay-surface input handling.
