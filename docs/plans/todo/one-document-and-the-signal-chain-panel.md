# One open document, and the signal-chain panel's output stage

Status: **DEFERRED. Designed 2026-10-02 with the user. Two Fable UI experts and two Fable
simplicity experts compared four layouts, a judge ruled between them, and later rounds settled the
menus and the document model. Nothing is built.** Re-verify every `file:line` below against the
code before executing; they were read on 2026-10-02.

This plan supersedes the parked direction in `gain-block-and-signal-chain-panel.md`. That plan
moved the per-tone level into a Gain block because a level beside the global output meter read as
global. Here the level and pan move under the tone's own title instead, so they read as the tone's.
The Volume and Pan automation lanes stay in `tone-parallel-paths-and-mix-lanes.md`. This plan
builds the static values those lanes anchor to.

## Rulings

- **One open document.** The editor holds exactly one document: the open project or, when none is
  open, the tone (plan 50's live-rig floor). The mode is derived from what is open, never stored.
  File acts on that document. Ctrl+S always saves it, and the window title names it in both cases
  (`Untitled* - Rock Hero Editor`).
- **Bare names; the menu supplies the type.** File > Import… / Export… are the song commands;
  Tone > Import… / Export… are the tone-copy commands. The keybind editor already groups commands
  under category headers (`keymap_editor_view.cpp:444-451`). File's Open… / Save / Save As… /
  Close are bare names today.
- **New offers both kinds.** A user opening the editor for the first time most likely wants a
  project, so New must offer a project as well as a tone. Because each item names its kind, neither
  needs greying in either mode: each is one command with one meaning, behind the
  unsaved-changes prompt.
- **The output stage is one value per tone:** `ToneOutput { Gain level; Pan pan; }`, with one
  setter, one undo edit and one preview/commit pair (the existing gain pair, generalized).
  - **Pan** is a balance with unity at centre (`juce::dsp::PannerRule::balanced`), so adding a Pan
    lane never changes the level.
  - **The value format** is `C` / `L35` / `R100`, from one formatter and parser pair beside
    `gain.h` in `rock-hero-common/audio/shared/`. The control and the lane both use it.
  - **The controls show the stored value**, which is the lane's anchor, and never the playhead.
- **Level and pan live in the signal-chain title band; the side columns are meters only.**
  - Pan is a **knob**.
  - The "Input" and "Output" titles stay centred and matched, and the two meters stay symmetric.
  - The columns stay 72 px wide.
- **The fix sits where the problem is, and the chain is never blocked by the input.** A charter
  with no guitar must still edit tones. Blocking the chain would also have to block the tone's
  automation, which edits the same rig.

## The menus

Every item is listed in both modes. An item that does not apply is greyed, never hidden or renamed.

| File | Shortcut | Tone open (no project) | Project open |
|---|---|---|---|
| New ▸ Project… | Ctrl+N | blank project | blank project (save prompt) |
| New ▸ Tone | Ctrl+Shift+N (OPEN) | untitled tone (save prompt) | closes the project into an untitled tone |
| Open… | Ctrl+O | `*.rhp;*.tone`; the file decides | same |
| Import… | Ctrl+I | `*.rock;*.gp` → new project | same |
| Save / Save As… | Ctrl+S / Ctrl+Shift+S | the tone | the project |
| Export… | Ctrl+E | greyed | `.rock` |
| Close | Ctrl+W | greyed (the tone is the floor) | back to an untitled tone |
| Exit | Ctrl+Q | | |

| Tone | Shortcut | Tone open | Project open |
|---|---|---|---|
| Import… | Ctrl+Shift+I | greyed | replaces the active tone's rig (automation confirm) |
| Export… | Ctrl+Shift+E | greyed | writes a copy of the active tone |

| Audio | |
|---|---|
| Audio Device Settings… | the same intent as the menu-bar device button (`editor_view.cpp:453`) |
| Calibrate Input… | the always-available path for recalibrating |

**New ▸ Project…** depends on the blank-project entry point, plan 40 Phase 11, which is gated on
plan 41. Today a project is born only from Import. Until Phase 11 ships, the item does not exist
(no placeholder) and Ctrl+N stays reserved for it.

## The panel

**Title band (34 px), the same layout in both modes:**

`[title, elided] … [Level bar ~120 + "-6.0 dB" box] 16 px [Pan knob 28 + "C" box]` (both
boxes editable)

- **Level:** `LinearHorizontal`, ±24 dB, double-click → 0 dB. The editable box is today's dB box,
  moved, so type-to-set survives.
- **Pan:** `RotaryHorizontalVerticalDrag`, −100..100, double-click → C, the text from the shared
  formatter.
- **Both:**
  - no captions;
  - scroll wheel off, since an idle wheel over the band must not write undo entries;
  - JUCE's default velocity modifiers, which already include Ctrl.
- **Drawing:** one small LookAndFeel replaces `OutputGainSliderLookAndFeel`
  (`signal_chain_view.cpp:100-129`). It fills the level from 0 dB and the knob arc from 12
  o'clock. Stock V4 cannot do either: it insets a rotary by 10 px (a 28 px knob would draw as an
  8 px disc) and fills from the start, so 0 dB and C would both paint half-full.
- **Gone:** the header's six tone file buttons, their placement strips and the mode-dependent
  visibility (`signal_chain_view.cpp:237-267`, `:343-348`, `:511-531`). The registry's "the header
  is the mouse surface" exception (`editor_command_id.h:64-66`) goes with them.

**Side columns:**
- One 72 px constant. The title is centred, and the 28 px meter is centred and full height.
- Gone:
  - the vertical fader;
  - the bottom dB box and the Calibrate band;
  - five layout constants;
  - the "disabled" sub-caption (`signal_chain_view.cpp:408-422`), which today de-centres "Input".

**The input column, per status:**

| Status | Shows |
|---|---|
| Calibrated | the live meter |
| Missing calibration | the dimmed meter with a centred **Calibrate** button; it stays after Cancel, and re-fronts the window while the prompt is open |
| No input device | the dimmed meter with a centred **Settings** button (label OPEN) that opens audio device settings |
| Fault (store unreadable, backend refused) | the dimmed meter, the reason in the tooltip, no button (settings would not reliably fix it) |
| Settings open | under the editor-wide dim and notice |

- **The dim:** paint it in `AudioLevelMeter::paint` when the meter is disabled. The chain's
  `paintOverChildren` dim would cover the button. The dim is near-invisible on the meter body
  (about 1.07:1), so the button carries the signal.
- **The label must fit 72 px unsquashed.** "Calibrate" at V4's 15.6 px button font is compressed
  today. Give the panel's buttons about 13 px through the panel's LookAndFeel, and confirm with a
  render.
- **The view already receives `input_calibration_status`** (`signal_chain_view_state.h:62`) and
  reads it nowhere; the button choice switches on it.

## Phases

1. **One document.** No dependency on the other phases.
   1. Add one `documentKind()` authority and route the existing forks through it:
      - the Save As chooser fork (`editor_view.cpp:3024-3029`);
      - the twice-stated untitled → Save As routing (`:1915`, `:2840`);
      - the ORed dirty predicates (`project_handlers.cpp:774`).
   2. Fold `m_tone_designer.active`, `m_project` and `m_save_requires_destination` into one
      variant: `Transient | ToneDocument | ProjectDocument`. Today the invariant
      `active ⇒ !project` is kept by hand at four sites, and `m_project` goes empty during every
      write.
   3. Merge the commands. Open, Save, Save As and New each become one command, and the
      `ToneDesignerInactive` reason likely goes. Add the Tone and Audio menus, the window title,
      the combined Open chooser with one last-used folder, and the header-button deletion.

   Size: a substantive controller refactor of about 40 sites; step 1 alone gives the user-facing
   result.
2. **The tone output stage.**
   - `ToneOutput` replaces `output_gain` wherever the gain travels: the three rig structs, the
     tone document, the setter, the undo edit and the view state.
   - Split a `ToneOutputPlugin` (level + pan) out of `ToneBranchGainPlugin`, which keeps only the
     tone-switch crossfade. A Volume lane then can never reach the crossfade.
   - Make level and pan real `AutomatableParameter`s, persisted from their explicit value and not
     an attached `CachedValue` (a playing curve writes back into an attached value).
   - Not `VolumeAndPanPlugin`: it locks on the audio thread, and its volume tops out at +6 dB.
   - FORMAT: the tone document gains pan.
3. **The panel.** Needs phase 2 for the knob.
   - The title band, the bipolar LookAndFeel, the meter-only columns and the input-column states.
   - Update `docs/user/input-calibration.md:11-12`, which names the old button.

**A separate finding, worth doing on its own:** calibrate and device-settings availability lives
in the projection (`input_calibration_projection.cpp:70-71`), outside the action table.
- Give both verbs action-table rows, as the menu and button gates.
- The auto-offer must keep the projection gate. It runs inside the busy window
  (`project_handlers.cpp:342` before `finishBusyOperation` at `:350`), and the table refuses every
  action while busy, so gating the offer on the table would drop it after every load.

## Open

1. The New Tone shortcut: Ctrl+Shift+N, or none.
2. The no-device button label: "Settings" or "Device…".
3. The menu wording: "Audio", "Audio Device Settings…", "Calibrate Input…".
4. The empty-timeline message "No Project Loaded" (`track_viewport.cpp:218`) uses "load", which
   plan 50 bans from the UI. Suggested: "No project open".

## Sighting checks

- First launch, insert a plugin, press Ctrl+S: the tone's Save As chooser opens.
- With a clean project open, File > Open… a `.tone`: a charter predicts "this closes my project",
  not "this imports into it".
- A fresh viewer finds the Calibrate button within about 3 s with the input uncalibrated, and the
  chain stays fully editable.
- At 1280×720 with a 40-character tone name, nothing clips and the title elides with its `*`
  intact.
- The knob at 28 px: C is distinguishable from L10, and ten consecutive drags land without a miss.
