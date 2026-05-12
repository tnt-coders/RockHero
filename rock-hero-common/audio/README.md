# `rock-hero-common/audio`

Shared audio contracts and the default shared audio implementation belong here.

This module exposes one target:

- `rock_hero::common::audio` for audio ports, project-owned contracts, and the current concrete
  Tracktion/JUCE-backed adapter.

Normal library code should depend on the project-owned audio ports by convention. App composition
code and concrete adapter tests may construct the default `Engine` implementation.

Tracktion headers stay private to implementation files and private implementation headers. Public
headers expose project-owned types and carefully chosen forward declarations.
