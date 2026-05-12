# `rock-hero-editor/audio`

Editor-specific audio behavior that is not part of the shared engine belongs here.

Allowed dependencies: `common/core`, `common/audio::api`, and editor-only audio dependencies added
with real code.

Do not place the shared Tracktion-backed engine here, and do not place scoring or game analysis
logic here.

First real code should be editor-only audio policy, services, or adapters outside the shared
playback engine.
