# Automated Testing Options

## Goal

Establish a testing strategy for Rock Hero that:

- Covers meaningful behavior at each architectural layer
- Integrates with the existing CMake/Conan/CTest pipeline
- Does not require abstracting JUCE out of `rock-hero-ui` headers
- Is honest about what cannot be practically automated

---

## Project Layers and Their Testability

Before comparing approaches it helps to understand how testable each layer already is.

| Layer | JUCE dependency | Testability today |
|---|---|---|
| `rock-hero-core` | None | Excellent — pure C++23, no initialization needed |
| `rock-hero-audio-engine` | Private (one `juce::File` in public API) | Good — synchronous methods are straightforward |
| `rock-hero-ui` | Public (inherits `juce::Component`) | Limited — logic is thin but coupled to JUCE lifecycle |
| Editor app | JUCE throughout | Integration only — it is a JUCE application |
| Game app | SDL3 / bgfx | Depends on what gets extracted into libraries |

---

## Test Infrastructure: Catch2 with a JUCE-Aware Main

Regardless of which testing patterns are adopted, the test executable needs a custom
`main` to initialize the JUCE `MessageManager` before any JUCE-derived type is touched.
Catch2 is already in this project's Conan dependency graph.

**`tests/main.cpp`**

```cpp
#define CATCH_CONFIG_RUNNER
#include <catch2/catch_all.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui_init;
    return Catch::Session().run(argc, argv);
}
```

**CMake sketch**

```cmake
add_executable(rock_hero_tests tests/main.cpp)
target_link_libraries(rock_hero_tests PRIVATE
    rock-hero-core
    rock-hero-audio-engine
    rock-hero-ui
    Catch2::Catch2)
catch_discover_tests(rock_hero_tests)  # automatic CTest registration
```

`ScopedJuceInitialiser_GUI` initialises the `MessageManager` singleton and registers
the `DeletedAtShutdown` cleanup hook. It does **not** pump the message loop — that
distinction matters and is explained in the async section below.

This is the community standard, codified by the
[Pamplejuce](https://github.com/sudara/pamplejuce) template. All approaches below
assume it is in place.

---

## Approach 1: Test `rock-hero-core` Directly

**What it is.** The `rock-hero-core` library (`Song`, `Chart`, `Arrangement`,
`NoteEvent`) has no JUCE dependency at all. Tests for it require no JUCE
initialization and run as fast as any plain C++ unit test.

**What it covers.**

- Song construction and validation
- Arrangement filtering and ordering
- Note event correctness, sorting, edge cases
- Serialization round-trips (once that code exists)
- Any domain logic added to core types

**What it does not cover.** Anything involving audio, UI, or external I/O.

**Setup cost.** Near zero — just add test files and link `rock-hero-core`.

**Verdict.** This is the highest-value, lowest-cost investment. These tests are
completely stable, have no threading concerns, and cover the domain model that
everything else depends on.

---

## Approach 2: The Humble Object Pattern

**What it is.** A technique from *xUnit Test Patterns* (Meszaros, 2007) and
*Clean Architecture* (Martin, 2017): strip a hard-to-test object down to a passive
shell that contains no logic, and extract all decision-making into a plain C++ object
with no framework dependency. The framework object (the JUCE component) becomes a
thin adapter. The extracted object is tested directly.

**Applied to `TransportControls`.** The component already has the right instincts —
callbacks, explicit state setters — but the toggle logic (`onPlayPauseClicked`) still
lives inside the component. Extracting it:

```cpp
// Plain C++ — no JUCE header needed
struct TransportState {
    bool file_loaded = false;
    bool playing     = false;

    bool canPlay()  const { return file_loaded && !playing; }
    bool canPause() const { return playing; }
    bool canStop()  const { return file_loaded; }
};

// Also plain C++
enum class PlayPauseResult { Play, Pause, Ignored };

PlayPauseResult handlePlayPause(const TransportState& state);
```

The JUCE component holds a `TransportState`, delegates decisions to it, and wires
the results to `on_play` / `on_pause`. The component becomes untested but so thin it
barely needs to be.

**Applied to `WaveformDisplay`.** The seek position calculation is currently inline:

```cpp
// Currently inside mouseDown()
double position = (event.x / static_cast<double>(getWidth())) * m_total_length_seconds;
```

Extracting it:

```cpp
// Plain C++ — fully testable
double seekPositionFromClick(int click_x, int component_width, double total_duration);
```

**What it covers.** All extractable logic: state transitions, calculations, mode
conditions, data transformations. These tests are as fast and simple as testing a
state machine.

**What it does not cover.** Visual rendering correctness, layout, and event
dispatch — the thin component shell that remains is still not tested by this approach.

**Setup cost.** Moderate up-front refactoring per component, very low ongoing cost.
No mock infrastructure needed — these objects are tested directly.

**Important caveat.** The Humble Object pattern is the right tool for logic, but it
does not solve the rendering and event dispatch problem. Whether a mouse click at
a given coordinate maps correctly through JUCE's event system, or whether the
waveform renders at the right pixel position, is not answered by a `TransportState`
struct. That is a different problem requiring a different tool.

---

## Approach 3: Direct Component Testing

**What it is.** Instantiating real `juce::Component` subclasses inside Catch2 tests,
calling methods, simulating synchronous events, and asserting on the resulting state.

**What works.** With `ScopedJuceInitialiser_GUI` in place:

- Synchronous state queries: `setFileLoaded(true)` → `isFileLoaded()` → check result
- Button callbacks: `button.triggerClick()` fires `onClick` synchronously
- Keyboard dispatch: `keyPressed()` can be called directly
- Layout: call `setSize()` + `resized()`, then query child component bounds
- The `onPlayPauseClicked()` path in `TransportControls`

**What does not work.** `ScopedJuceInitialiser_GUI` initializes the
`MessageManager` but does not pump it. This means:

- `juce::Timer` callbacks never fire
- `callAsync` notifications are never delivered
- `juce::Value::Listener` and `ValueTree::Listener` notifications are not dispatched
- `repaint()` schedules a deferred repaint that never executes

To test async behavior you must run the Catch2 session on a background thread and
pump the message loop on the main thread — a fragile pattern that is documented in
the JUCE forums but not recommended as a general testing strategy.

**Additional pitfalls.**

- On headless CI (Linux without a display), certain component operations fail unless
  a virtual display (Xvfb) is configured.
- Components with zero bounds produce nonsense from coordinate math; set explicit
  bounds before calling any layout or hit-test logic.
- `DeletedAtShutdown` warnings appear if `ScopedJuceInitialiser_GUI` is
  constructed inside fixtures rather than once per process.

**What it covers.** Synchronous state and synchronous event callbacks. A useful
complement to Humble Object tests for verifying that the JUCE wiring is correct.

**What it does not cover.** Async notifications, visual rendering, or headless CI
without additional infrastructure.

**Setup cost.** Low — no additional infrastructure beyond the JUCE-aware main.

---

## Approach 4: Thin JUCE Wrapper / Mock Layer

**What it is.** Define abstract interfaces over JUCE types (`IComponent`,
`IGraphics`, `ITimer`) that your components implement. Inject mock implementations
in tests to verify interaction.

**The core problem.** This approach tests whether your code calls the right mock
methods, not whether your code actually works. Your mock `IGraphics` will not behave
like `juce::Graphics`. Your mock `IComponent` will not replicate JUCE's event
dispatch semantics. Tests that pass against mocks can conceal bugs that only surface
against the real framework.

**The surface area problem.** Making JUCE meaningfully mockable requires wrapping:

- `juce::Component` — paint, resize, mouse/keyboard events, parent/child relationships
- `juce::Graphics` — every draw primitive
- `juce::Timer` — callback scheduling
- Every widget type in use (`juce::DrawableButton`, `juce::Drawable`, etc.)

Each new JUCE feature used in the project extends the wrapper surface.

**What it covers.** That the component calls the expected mock methods. This is a
shallow form of correctness — it verifies structure, not behavior.

**What it does not cover.** Actual rendering, actual event dispatch, or any JUCE
behavior that differs between the real implementation and the mock.

**Setup cost.** High — interfaces, mock implementations, and wiring per component,
maintained indefinitely.

**Comparison with Humble Object.** The Humble Object pattern extracts real logic and
tests it directly. The wrapper pattern adds a mock intermediary between the test and
the logic. For the same logic test, Humble Object is simpler, faster, and tests
real behavior. The wrapper approach offers no meaningful advantage over Humble Object
for any test category.

**Verdict.** The wrapper approach is the weakest option. It carries the highest
infrastructure cost and delivers the least real coverage. It is not recommended.

---

## Approach 5: Snapshot / Approval Testing for Visual Output

**What it is.** Render a component to a `juce::Image` using
`paintEntireComponent()`, serialize the pixels, and compare against a stored
reference image. The first run establishes the approved baseline; subsequent runs
flag pixel-level regressions. Libraries such as ApprovalTests.cpp integrate with
Catch2.

**What it covers.** Visual regression detection — if a code change unintentionally
shifts pixels, the test fails. Also applicable to audio: render a passage to a
buffer, compare sample-for-sample against a golden file to catch DSP regressions.

**What it does not cover.** Whether the baseline is correct in the first place.
Snapshot tests detect changes, they do not verify intent.

**Maintenance cost.** Intentional visual changes require re-approving the baseline.
This is manageable for stable components; it becomes friction on rapidly iterated UI.

**Verdict.** A useful complement for stable, visually critical components. Not
appropriate as a primary testing strategy. Most appropriate for the waveform
renderer once its appearance is settled.

---

## Approach 6: End-to-End / Integration Testing

**What it is.** Test the running application from the outside. Focusrite's open
source [juce-end-to-end](https://github.com/FocusriteGroup/juce-end-to-end) library
embeds a `TestCentre` component (behind a build flag) that accepts commands over IPC
and drives the real application. Tests are written in JavaScript/TypeScript.

**What it covers.** Full user flows through the real application with the real
message loop. Solves the async problem entirely because the app runs normally.

**What it does not cover.** Fast feedback — these tests are slower than unit tests
by nature. Fine-grained logic.

**Setup cost.** High — Node.js dependency, cross-language test code, build flag
management.

**Verdict.** Worth knowing about for smoke testing user flows once the application
is mature enough to have stable flows to smoke-test. Premature at the current stage.

---

## Honest Limits: What Cannot Be Practically Automated

Some behavior is genuinely hard to automate and should be acknowledged rather than
papered over with fragile tests:

| Behavior | Why it is hard | Practical approach |
|---|---|---|
| Waveform visual correctness | Requires human judgment on aesthetics | Snapshot tests for regression; manual review for intent |
| Timer-driven repaint | Message loop not pumped in test process | Test the logic the timer triggers, not the timer itself |
| JUCE event dispatch ordering | Requires real message loop | Direct component tests for simple cases; accept limits |
| Audio device I/O | Requires real hardware or virtual device | Test DSP math in isolation; integration test separately |
| 60 Hz playback cursor accuracy | Real-time, hardware-dependent | Test position calculation as a free function |

The goal is not 100% coverage — it is meaningful coverage of the behavior where
automated tests provide reliable signal.

---

## Summary and Recommended Strategy

| Approach | Setup cost | Speed | Tests logic | Tests rendering | Tests events | Recommended |
|---|---|---|---|---|---|---|
| `rock-hero-core` direct | Very low | Fastest | Yes | No | No | Yes — start here |
| Humble Object extraction | Medium (refactor) | Fastest | Yes | No | No | Yes — primary pattern |
| Direct component tests | Low | Fast | Partial | No | Sync only | Yes — selectively |
| Snapshot / approval | Medium | Moderate | No | Regression | No | Yes — for stable UI |
| JUCE wrapper / mocks | High | Fast | Weakly | No | No | No |
| End-to-end (Focusrite) | High | Slow | No | Partial | Yes | Future — not yet |

**The practical starting point:**

1. Add Catch2 test target with JUCE-aware `main` (one CMake target, one C++ file)
2. Write tests for `rock-hero-core` domain types immediately — no JUCE needed
3. Apply Humble Object incrementally: extract logic from `TransportControls` first,
   then waveform seek calculations, then peak data bucketing in `WaveformSource`
4. Add direct component tests selectively for synchronous state and callback wiring
5. Defer snapshot tests until visual output is stable
6. Do not implement a JUCE wrapper layer
