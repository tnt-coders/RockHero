# User-Configurable Editor UI Scale

Expose a UI scaling preference so the editor reads at a comfortable size on displays the
OS does not automatically scale. JUCE's logical-pixel sizing only scales when the OS
reports a non-100% scale factor; many high-resolution displays (notably 2560×1440 desktop
monitors on Windows) run at the OS's default 100% scale and cause our hardcoded sizes to
render small in physical inches.

## Background

JUCE Components paint and lay out in logical pixels, and the platform peer scales the
result to physical pixels when the OS reports a scale factor > 1.0. Windows applies that
scale automatically for displays the OS classifies as "high DPI" — typically Retina-class
laptop panels and most 4K displays — but **not** for the common 1440p desktop case at
the default "Scale and layout" of 100%. Our `280×120` busy overlay surface, our `72 px`
plugin-row buttons, and similar fixed sizes therefore render at 280×120 / 72 physical
pixels on a 1440p display with ~109 DPI, which feels meaningfully smaller than the
equivalent on a 1080p panel with ~92 DPI.

`Desktop::setGlobalScaleFactor(...)` applies an additional uniform scale on top of
whatever the OS provides. Setting it from a user preference (or auto-detected default)
gives us a single dial that scales all logical pixels in the editor uniformly.

Every mature DAW exposes this preference (Reaper "UI Scale," Bitwig "Display Settings →
Zoom Level," Ableton "Zoom Display," Cubase "HiDPI Scale"). Users expect to be able to
tune editor density to their display.

## What to add

For each editor session:

1. **A persisted preference** — an integer scale percentage (e.g. 100, 125, 150, 175,
   200), with `100` as the default, stored in editor settings.
2. **A Preferences-style UI control** — a labeled dropdown or stepper on a settings
   panel, presenting the available values and applying the choice immediately.
3. **Application at startup** — call `juce::Desktop::getInstance().setGlobalScaleFactor`
   before the main editor window is shown, using the persisted value.
4. **Live re-application** — when the user changes the value mid-session, apply the new
   scale immediately. JUCE re-lays out components when the global scale changes, so
   this generally works, but UI-heavy components may need to re-derive cached metrics.

## File shape

The editor already has `editor_settings.h`
(`rock-hero-editor/core/include/rock_hero/editor/core/settings/editor_settings.h`). Adding a
`ui_scale_percent` field (or similar) fits the existing precedent. Persistence and
serialization should follow the patterns already used for whatever lives there.

## Plumbing checklist

- [ ] Extend `EditorSettings` with a `ui_scale_percent` (or `ui_scale` as a float; pick
  whichever serializes cleanest with the existing settings).
- [ ] Update the settings serializer (wherever editor settings round-trip) to handle the
  new field with sensible defaulting if absent.
- [ ] Add a Preferences UI control showing supported scale values. Standard set is
  `100 / 125 / 150 / 175 / 200`. Live-apply on change.
- [ ] Apply the persisted scale at editor startup via
  `juce::Desktop::getInstance().setGlobalScaleFactor(...)` before showing the main
  window.
- [ ] Verify behavior on representative configurations:
  - 1080p at OS 100% (baseline)
  - 1440p at OS 100% (the immediate motivating case)
  - 4K at OS 150% (already-scaled by OS; our scale stacks on top)
  - Retina at native (already-scaled by OS)
- [ ] Tests:
  - Round-trip the new field in editor settings.
  - Sanity-check that changing the value re-lays the editor without crashes; no need
    for pixel-perfect visual tests.

## Design questions to decide when picking this up

- **Auto-detection at first run.** Should the first-run default attempt to pick a sensible
  value from `Desktop::Displays::Display::dpi`? Cleanest UX, but display DPI reporting
  is famously inconsistent. Safer to start at `100` and let users discover the
  preference.
- **Scale stacking with OS DPI.** Our scale multiplies against the OS's. On 4K at OS
  150%, choosing our 125% gets ~1.88× total. Document this clearly in the preference UI
  (e.g. label as "Editor UI Scale" and note it stacks with the OS setting). Don't try to
  cancel the OS scaling.
- **Plugin editor windows.** Third-party plugin editor peers do not honor
  `setGlobalScaleFactor`. They will appear at their own native scale alongside our
  scaled chrome. This is consistent with how every other DAW behaves and is expected
  user behavior; do not attempt to scale plugin editors.
- **Step granularity vs. free slider.** Stepped values are easier to reason about for
  users and easier to test; a free slider is more flexible but rarely worth the
  imprecision. Start stepped.
- **Restart-required vs. live-apply.** Live-apply is the standard expectation. JUCE
  supports re-applying the global scale at runtime; any custom-rendered widgets that
  cache metrics should subscribe to a scale-change signal or re-derive on the next
  paint.

## Out of scope

- Per-window or per-panel scale (only one global slider).
- Theming / color palettes — separate concern.
- The game UI (`rock-hero-game/`). The game uses a different rendering stack (bgfx/SDL3
  per the architecture doc) and follows a fixed-reference-resolution + uniform-scale
  model, not JUCE's logical-pixel + global-scale-factor model.
- Plugin editor windows from third-party plugins (see design notes above).