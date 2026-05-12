# `rock-hero-common/core`

Shared, framework-free domain and data behavior used by both the editor and the game belongs here.

Allowed dependencies: standard C++ and approved pure data/format dependencies.

Do not place JUCE, Tracktion, SDL, bgfx, app settings, product workflow, audio devices, or UI loop
code here.

First real code should be shared domain primitives, package/value types, validation, timing math, or
other logic that both products can test without frameworks.
