# MIDI command bindings — MIDI input as a binding front-end over the command registry

Status: **Todo — drafted 2026-07-20**, when the MIDI requirement permanently settled the
custom-shortcuts-dialog decision (plan 46 Phase 3 record). Per the todo-bucket rule, re-verify
every claim here against the current code and re-read `docs/design/architectural-principles.md`
before executing; the JUCE citations were verified against the vendored source on the draft
date.

## Motivation

The user wants MIDI inputs (a foot controller, pads, a MIDI keyboard) to trigger editor
commands as if they were keybinds. For a guitarist with both hands on the instrument this is
the natural control surface: foot-pedal play/pause, and — once grammar verbs are registered as
commands — hands-free navigation like "jump back a measure" while practicing a passage.

## Why this is a RockHero-owned layer (verified 2026-07-20)

JUCE ships no MIDI→command facility, and the stock keymapping stack cannot grow one:

- `KeyMappingEditorComponent`'s only constructor takes a `KeyPressMappingSet&`
  (juce_KeyMappingEditorComponent.h:58) — no seam for a second binding domain.
- `KeyPress` is exactly `{int keyCode; ModifierKeys mods; juce_wchar textCharacter}`
  (juce_KeyPress.h:279-281) — no vocabulary for a note, CC, or channel; counterfeiting MIDI as
  fake key codes is code-that-lies, banned.
- `KeyPressMappingSet` is a `KeyListener` (juce_KeyPressMappingSet.h:95) fed via `keyPressed`
  (:219) through keyboard focus — a MIDI event can reach neither dispatch nor a press-a-key
  capture flow.

The layer *beneath* is trigger-agnostic: `ApplicationCommandManager::invokeDirectly`
(juce_ApplicationCommandManager.h:219) fires any registered command from any source, with
enablement still consulted through the target's `getCommandInfo`. So MIDI bindings are a
parallel front-end over the same `EditorCommandId` registry — exactly the REAPER actions
model (keys, MIDI, OSC all bind to action ids), which plan 53's standing convention adopts.
The keymap (`KeyPressMappingSet` + XML persistence) stays keyboard-only and untouched.

A property worth preserving in the design: MIDI events arrive from the device driver
regardless of window focus, so MIDI bindings inherently work while a plugin window is focused
— no analogue of the plugin-window keyboard mirror is needed.

## Design sketch

- **Value type** — `MidiCommandBinding`: a closed source descriptor (note-on with note number
  + channel, or CC number + channel with press semantics) mapped to an `EditorCommandId`.
  Model it like `PluginWindowShortcutChord`: a small comparable value with a defaulted `==`.
- **Store** — RockHero-owned, editor-side (`rock-hero-editor/ui/src/keybinds/` beside the
  keymap persistence), persisted through `IEditorSettings` under its own key. No defaults
  exist, so this is a plain serialized list, not a diff-vs-defaults blob; drop entries whose
  command id is unknown on restore, mirroring `EditorKeymapPersistence`.
- **Runtime** — a MIDI input listener that matches incoming events against the store and
  invokes the command. MIDI callbacks arrive on a system MIDI thread: hop to the message
  thread before invoking (verify the exact `invokeDirectly` threading contract at execution
  time). Disabled commands stay inert for free because invocation consults enablement.
- **Device model** — an explicit user-selected binding input device (persisted), not
  all-open-inputs: the editor may later route MIDI *to* plugins, and the binding path and the
  audio path must not fight over the same device silently. This is the main open question
  below.
- **Dialog** — `KeymapEditorView` rows grow MIDI chips beside the key chips, plus a
  "MIDI learn" capture: a themed dialog that arms, shows the next incoming event live (with
  current-owner preview, like the key capture), and applies through the same one-owner
  conflict confirm. Per-command reset clears MIDI bindings too (to empty — there are no MIDI
  defaults).
- **Grammar verbs** — to MIDI-bind navigation, grammar verbs must first register as commands
  whose `perform` delegates *into* the decoder cascade (keys stay reserved; plan 53's
  convention records this as the future path). That registration is a phase here, not a
  prerequisite for a first cut that covers only existing commands.

## Deliberately out of scope

Continuous CC → parameter mapping (that is automation/controller territory, a different
feature), multi-event chords, and velocity sensitivity. First cut: note-on and CC-crossing-
threshold act as momentary triggers only.

## Open questions (resolve before executing)

1. **Device selection UX and plumbing** — where the binding device is chosen (audio settings
   panel vs. the actions dialog), and how it coexists with any future MIDI-to-plugin
   routing. Depends on state of the audio-device settings code at execution time.
2. **Channel matching** — store and match the channel, or ignore it? Leaning store-and-match
   (a closed value is honest about what it matched).
3. **CC press semantics** — threshold value and whether release (value below threshold) is
   observable; first cut needs only rising-edge triggers.

## Phases sketch

0. Re-verify this plan against current code; settle the open questions.
1. Value type + store + `IEditorSettings` persistence + tests (headless).
2. Runtime listener + message-thread invocation + device selection setting + tests via a fake
   MIDI source.
3. Dialog surfaces: MIDI chips, learn capture, conflict/one-owner flow, reset behavior +
   tests.
4. (Optional, unlocks navigation bindings) Grammar-verb command registration delegating into
   the decoder.
5. Manual acceptance with a real MIDI device, including while a plugin window is focused.
