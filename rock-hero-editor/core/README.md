# `rock-hero-editor/core`

Editor-specific headless workflow and policy belongs here.

Allowed dependencies: `common/core` and `common/audio::api`.

Do not place JUCE widgets, concrete audio engines, app startup, or Tracktion implementation details
here.

First real code should be editor session workflow, save/open/close policy, command handling,
undo/redo policy, or state machines that can be tested without concrete UI.
