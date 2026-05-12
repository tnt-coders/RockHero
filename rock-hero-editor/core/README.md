# `rock-hero-editor/core`

Editor-specific headless workflow and policy belongs here.

Allowed dependencies: `common/core` and `common/audio::api`.

Do not place JUCE widgets, concrete audio engines, app startup, or Tracktion implementation details
here.

Current code includes the headless editor controller, editor intent contracts, and derived editor
view state. Future code should continue to put editor session workflow, save/open/close policy,
command handling, undo/redo policy, or state machines here when they can be tested without
concrete UI.
