# Busy Progress Bar Flat-Variant Decision

Status: completed decision note. Keep the on-demand + composition implementation. The flat/custom
variant remains a reasonable alternative, but it does not improve the current implementation enough
to justify replacing JUCE's built-in indeterminate progress animation.

## Scope

The editor busy overlay's progress bar uses an **on-demand + composition** design. This note
considered swapping the *inner* implementation to a **flat, fully-custom** bar while keeping the
on-demand lifetime exactly as-is.

Only `rock-hero-editor/ui/src/busy_overlay.h` and `busy_overlay.cpp` (and the busy-overlay test) are
in scope. The `core::BusyIndicator` / `BusyViewState` contract does not change.

## Settled Background (Do Not Revisit)

The bar's **lifetime** is owned by `BusyOverlay`: it builds a `std::unique_ptr<BusyProgressBar>` on
demand in `setBusyState` and `reset()`s it when no bar is needed (MessageOnly or cleared). This is
the right backbone and is not in question. It dissolved the idle-animation-timer issue at its root
(no bar exists during idle editing, so no timer exists) and removed all visibility-coordination
plumbing. See the `project_busy_progress_bar_decision` memory for the full history of rejected
alternatives (inheritance / base-from-member, permanent member, in-bar timer gating, variant).

Determinate progress is hand-painted as the exact fraction because `juce::ProgressBar` smooths its
displayed value toward the target and briefly shows wrong percentages (the original bug). That stays
true in both variants.

## Implemented State (on-demand + composition)

`BusyProgressBar` is a `juce::Component` that:

- hand-paints determinate progress as the exact fraction in `paint`, and
- owns a child `juce::ProgressBar m_indeterminate_bar` that is shown only while indeterminate, reusing
  JUCE's LookAndFeel animation and its internal timer.

Structure: `BusyOverlay` -> `unique_ptr<BusyProgressBar>` -> child `juce::ProgressBar`.

Known minor warts:

- A determinate-only operation still constructs the wrapper's hidden `juce::ProgressBar`, but that
  child remains hidden, so JUCE does not run its progress-bar timer for determinate progress.
- The component has a dual role: custom painter for determinate, container-of-one-child for
  indeterminate.
- Determinate visuals are ours; indeterminate visuals are the LookAndFeel's, so the two modes are
  drawn by different code paths.

## Rejected Variant (on-demand + fully-custom, flat)

Keep the on-demand `unique_ptr` lifetime in `BusyOverlay` unchanged. Change `BusyProgressBar` to a
single self-contained `juce::Component` (deriving `juce::Timer`) with **no child component**:

- one `paint` that draws the determinate fill (when `m_value >= 0`) or the indeterminate stripe
  animation (when `m_value < 0`);
- `setProgress` sets `m_value`, then `startTimer` when indeterminate and `stopTimer` when
  determinate;
- `timerCallback` just `repaint()`s to advance the stripes.

Because the bar exists only while shown, this timer logic would need **no** visibility gating: no
`visibilityChanged`, no `updateTimer(isVisible() && ...)`, and no overlay hide-notification. The
overlay's `reset()` would destroy the timer when the operation ends.

Structure: `BusyOverlay` -> `unique_ptr<BusyProgressBar>` (flat; no nested widget).

The stripe-drawing geometry already existed in an earlier revision and can be recovered from git
history rather than rewritten from scratch.

## Tradeoff

| Axis | Current (composition) | Proposed (fully-custom) |
|---|---|---|
| Lines owned in the bar | fewer (no animation code) | ~20 more (stripe drawing) |
| Indeterminate theming | follows LookAndFeel for free | hand-rolled; will not follow a theme |
| Structure | nested (extra component level) | flat |
| Painting | split across wrapper + child | one consistent paint path |
| Timer | JUCE's; runs only while the child is visible for indeterminate progress | trivial start/stop on mode; nothing unused |

Neither dominates. The swap would trade fewer lines and framework reuse for structural flatness,
one consistent paint path, and removal of the unused hidden child object.

## Decision

Keep the current composition design. `juce::ProgressBar` smooths determinate values, so determinate
progress still needs direct painting. For indeterminate progress, though, JUCE's implementation
already supplies themed animation and handles its timer from component visibility. Because the
child is hidden during determinate progress, the implemented design does not carry the active
timer cost that would have justified a flat/custom rewrite.

The remaining wart is only one unused hidden child object during determinate operations. That is
preferable to owning custom animation geometry and a local timer for a small editor overlay.

## Testing Impact

- `test_editor_view_busy_overlay.cpp` should need little change. The bar is still found by
  componentID `busy_progress_bar`, still a plain `juce::Component` to the test, still absent for
  MessageOnly and before any busy state.
- The determinate snapshot test ("paints determinate progress at its exact fraction") is unchanged:
  it asserts the fill covers the left half at 0.5, which both variants satisfy.
- Optionally add a small test that an indeterminate bar advances over time, but a headless
  timer-driven animation is awkward to assert; skip unless it earns its keep.

## Non-Goals

- Do not change the on-demand lifetime; that is settled and correct.
- Do not return to a permanent (always-constructed) `BusyProgressBar` member.
- Do not reintroduce a base class for the backing value, or inherit `juce::ProgressBar`.
- Do not change `core::BusyIndicator` or `BusyViewState`.
- Do not update durable `docs/design/` documents from this note.
