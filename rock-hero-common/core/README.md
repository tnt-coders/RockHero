# `rock-hero-common/core`

Shared, framework-free domain and data behavior used by both the editor and the game belongs here.

This module currently owns shared song/session state, timeline values, and runtime song package
persistence.

Allowed dependencies: standard C++ and approved data/format dependencies kept private to this
target.

Do not place JUCE, Tracktion, SDL, bgfx, app settings, product workflow, audio devices, or UI loop
code here.

Native editor project workflow and foreign-package import policy belong in `rock-hero-editor/core`.
Future code should remain shared domain primitives, package/value types, validation, timing math, or
other logic that both products can test without frameworks.
