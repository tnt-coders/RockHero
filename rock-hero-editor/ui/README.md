# `rock-hero-editor/ui`

Editor-specific JUCE presentation belongs here.

Allowed dependencies: `editor/core`, `editor/audio`, `common/audio::api`, and JUCE GUI modules when
needed for presentation.

Do not place save policy, undo/redo policy, project workflow ownership, app startup, or backend
audio implementation decisions here.

First real code includes editor windows, concrete editor components, menus, toolbars, waveform and
arrangement views, transport controls, and JUCE-backed editor settings helpers.
