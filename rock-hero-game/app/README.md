# `rock-hero-game/app`

Game executable startup and process lifecycle belong here.

This folder owns `main.cpp`, app resources, executable metadata, and only process-level
composition. The CMake target is `rock_hero_game_exe`; the shipped executable output remains
`rock-hero`.

Reusable gameplay, audio analysis, and rendering behavior belongs in `game/core`, `game/audio`, or
`game/ui`, not in `app/`.
