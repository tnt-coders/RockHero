# `rock-hero-game/app`

Game executable startup and process lifecycle belong here.

The existing game executable remains under `apps/rock-hero` during the migration. When that entry
point moves, this folder should own `main.cpp`, app resources, executable metadata, and only
process-level composition.

Reusable gameplay, audio analysis, and rendering behavior belongs in `game/core`, `game/audio`, or
`game/ui`, not in `app/`.
