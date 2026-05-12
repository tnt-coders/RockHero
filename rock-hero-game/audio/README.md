# `rock-hero-game/audio`

Game-specific audio analysis and audio-adjacent gameplay plumbing belongs here.

Allowed dependencies: `common/core`, `common/audio`, `game/core`, and DSP dependencies added with
real code. Use audio ports here unless concrete shared-engine composition is genuinely needed.

Do not place scoring rules, editor audio policy, shared engine implementation, rendering, or app
startup here.

The include namespace currently contains only `.gitkeep` so Git preserves the empty public include
root. Remove `.gitkeep` in the same change that adds the first real public header.

First real code should be pitch detection, onset detection, input-buffer adaptation, framing, FFT
helpers, calibration capture plumbing, or game-specific plugin-chain policy.
