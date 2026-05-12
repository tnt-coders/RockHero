# `rock-hero-game/ui`

Game-specific presentation and rendering belongs here.

Allowed dependencies: `game/core`, `game/audio`, `common/audio::api`, and bgfx/SDL dependencies
when real rendering code requires them.

Do not place scoring rules, audio timing policy, editor UI, shared JUCE windows, or app startup
here.

First real code should be bgfx surface integration, note highway rendering, HUD, visual feedback,
or gameplay-surface input handling.
