# `rock-hero-game/audio`

Game-specific audio analysis and audio-adjacent gameplay plumbing belongs here.

Allowed dependencies: `common/core`, `common/audio::api`, `game/core`, and DSP dependencies added
with real code.

Do not place scoring rules, editor audio policy, shared engine implementation, rendering, or app
startup here.

First real code should be pitch detection, onset detection, input-buffer adaptation, framing, FFT
helpers, calibration capture plumbing, or game-specific plugin-chain policy.
