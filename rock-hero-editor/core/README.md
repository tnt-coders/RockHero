# `rock-hero-editor/core`

Editor-specific headless workflow and policy belongs here.

Allowed dependencies: `common/core`, `common/audio::api`, and private editor project and song
import format dependencies.

Do not place JUCE widgets, concrete audio engines, app startup, or Tracktion implementation details
here.

Current code includes the headless editor controller, native `.rhp` project context, song
importers, editor intent contracts, and derived editor view state. Future code should continue to
put editor session workflow, save/open/close/publish policy, command handling, undo/redo policy, or
state machines here when they can be tested without concrete UI.
