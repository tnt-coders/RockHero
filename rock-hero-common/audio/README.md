# `rock-hero-common/audio`

Shared audio contracts and the default shared audio implementation belong here.

This module is intentionally split into:

- `rock_hero::common::audio::api` for audio ports and project-owned contracts.
- `rock_hero::common::audio::impl` for the current concrete Tracktion/JUCE-backed implementation.
- `rock_hero::common::audio` as an app-facing umbrella over both.

Normal library code and most tests should link `rock_hero::common::audio::api`. App executables and
concrete adapter tests may link `rock_hero::common::audio`. Normal library code should not link
`rock_hero::common::audio::impl` directly.

Tracktion headers stay private to implementation files and private implementation headers. Public
headers expose project-owned types and carefully chosen forward declarations.
