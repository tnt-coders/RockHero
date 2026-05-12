# `rock-hero-editor/audio`

Editor-specific audio behavior that is not part of the shared engine belongs here.

Allowed dependencies: `common/core`, `common/audio`, and editor-only audio dependencies added with
real code. Use audio ports here unless concrete shared-engine composition is genuinely needed.

Do not place the shared Tracktion-backed engine here, and do not place scoring or game analysis
logic here.

The include namespace currently contains only `.gitkeep` so Git preserves the empty public include
root. Remove `.gitkeep` in the same change that adds the first real public header.

First real code should be editor-only audio policy, services, or adapters outside the shared
playback engine.
