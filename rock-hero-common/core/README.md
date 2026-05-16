# `rock-hero-common/core`

Shared, headless domain and data behavior used by both the editor and the game belongs here.

This module currently owns shared song/session state, timeline values, and native song package
persistence.

Allowed dependencies: standard C++ and narrow `juce_core` utility facilities kept headless and
automated-testable.

Do not place Tracktion, SDL, bgfx, app settings, product workflow, audio devices, UI widgets, or UI
loop ownership here.

Native editor project workflow and song-source import policy belong in `rock-hero-editor/core`.
Future code should remain shared domain primitives, package/value types, validation, timing math, or
other logic that both products can test without app runtime dependencies.
