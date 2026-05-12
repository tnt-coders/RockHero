# `rock-hero-common/ui`

Shared UI code that both the editor and the game genuinely consume belongs here.

Allowed dependencies start with `common/core`; add JUCE GUI dependencies only when real shared UI
code requires them.

Do not place editor-only JUCE components or game-only bgfx/SDL rendering here.

First real code should be a shared UI service, window, look-and-feel, or calibration/settings
surface used by both products.
