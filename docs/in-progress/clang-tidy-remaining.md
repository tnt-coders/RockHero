# Clang-Tidy Cleanup — Remaining Work and Open Questions

Status: in-progress handoff note. The mechanical, convention-matching clang-tidy
findings from the full-pass cleanup have been fixed and committed. What remains is a
small set of findings that need a human decision because the current code is
intentional, framework-constrained, or conflicts with a check in a way that has no
single obvious resolution.

## How to reproduce the current state

Run clang-tidy read-only against the existing compile database (this does **not**
invoke cmake/ctest and does not disturb CLion's include paths):

```pwsh
& "C:\Program Files\LLVM\bin\run-clang-tidy" `
  -p C:/__MAIN__/Coding/__git__/RockHero/build/debug -j 16 -quiet `
  -extra-arg=-Wno-unknown-warning-option `
  -extra-arg=-Wno-unused-command-line-argument `
  "rock-hero-.*\.cpp$"
```

Or a single file:

```pwsh
& "C:\Program Files\LLVM\bin\clang-tidy.EXE" `
  -extra-arg=-Wno-unknown-warning-option `
  -extra-arg=-Wno-unused-command-line-argument `
  -p=C:/__MAIN__/Coding/__git__/RockHero/build/debug <file>
```

## Already done (for context)

All mechanical findings are fixed and committed, each file verified clang-tidy-clean
except for the flagged items below:

- AudioAsset / view-state / audio-state structs: added default member initializers to
  the struct fields (root fix for the missing-designated-field warnings) and converted
  call sites to designated initializers.
- Optional access in tests: the established `if (opt.has_value())` guard (or the
  `stateOrNull(...)` pointer helper) after `REQUIRE`.
- `const` correctness, no-op `std::move` removal, by-value -> `const&` parameters.
- C-array string constants -> `constexpr const char*`; meter tick tables -> `std::array`
  with range-for; `std::ranges::find` in `project.cpp`.

## Not a real backlog: generated files

`BinaryData1.cpp`..`BinaryData4.cpp` produce ~48 findings but are JUCE-generated
resource files. The real `clang-tidy` cmake target excludes them; they only appear
because the standalone `run-clang-tidy` invocation above globs every `*.cpp`. Ignore
them, or add an exclusion to the standalone command if you script it.

## Remaining findings — each needs a decision

Roughly 20 findings remain, grouped by the decision they require. For each, the
options are usually: (a) a behavior-preserving refactor, (b) a targeted
`// NOLINT(<check>)` with a justifying comment, or (c) a narrow allowance in
`.clang-tidy`.

### 1. Framework-constrained: JUCE/Tracktion ownership and casts

These are dictated by third-party API contracts. A refactor is not straightforward and
may not be possible without wrappers.

- `rock-hero-common/audio/src/engine.cpp:68` — `cppcoreguidelines-pro-type-reinterpret-cast`.
- `rock-hero-common/audio/src/engine.cpp:1071` — `cppcoreguidelines-owning-memory`:
  returns a Tracktion `Plugin::Ptr` (a ref-counted pointer) from a non-`gsl::owner`
  function.
- `rock-hero-common/audio/tests/test_audio_device_settings.cpp:245` — `cppcoreguidelines-owning-memory`:
  JUCE's `AudioIODeviceType::createDevice` override must `return new ...` because the
  base signature returns a raw `AudioIODevice*`.
- `rock-hero-common/audio/tests/test_live_rig_gain_plugin.cpp:26` — `cppcoreguidelines-owning-memory`:
  same pattern for a Tracktion `Plugin::Ptr` factory.

Open question: do we NOLINT these framework-boundary lines with a justification, or is
there an existing project wrapper convention they should route through?

### 2. Rule-of-five on classes with a user-declared destructor

`cppcoreguidelines-special-member-functions`. These are JUCE-adjacent helper/component
classes that declare a destructor but no copy/move members.

- `rock-hero-common/audio/src/engine.cpp:1079` — class `PluginWindow`.
- `rock-hero-common/audio/src/engine.cpp:1321` — class `MeterReader`.
- `rock-hero-editor/ui/src/input_calibration_window.cpp:96` — class `Content`.
- `rock-hero-editor/ui/src/audio_device_settings_window.cpp` — (same check; confirm line).

Open question: standardize on explicitly `= delete`-ing copy/move for these
non-copyable types, or NOLINT? A project convention here would also prevent recurrence.

### 3. Public data members in a test mock

`cppcoreguidelines-non-private-member-variables-in-classes`.

- `rock-hero-common/audio/tests/test_audio_device_settings.cpp:257` — `scan_call_count`.
- `rock-hero-common/audio/tests/test_audio_device_settings.cpp:260` — `control_panel_call_count`.

The check fires because `MockAudioDeviceType` is a `class` that mixes private impl
fields with public observation counters. Other project fakes pass because they are
all-public/struct-like.

Open question: make these counters private with accessors (touches the test call
sites), restructure the mock so its public surface is struct-like, or NOLINT?

### 4. Designated initializer for a third-party struct

`modernize-use-designated-initializers`.

- `rock-hero-common/audio/tests/test_live_rig_gain_plugin.cpp:92` —
  `tracktion::PluginInitialisationInfo{...}` built positionally.

Left alone because converting a Tracktion struct to designated form requires its exact
field names/order and risks a missing-field cascade on Tracktion fields without
defaults.

Open question: convert using Tracktion's field names, or NOLINT?

### 5. Static initialization that may throw

`bugprone-throwing-static-initialization`.

- `rock-hero-common/audio/src/live_rig_gain_plugin.cpp:11` — `g_gain_db_property`
  (a `juce::Identifier` with static storage duration).

Safe-but-not-trivial fix: replace the namespace-scope global with a function-local
static accessor (e.g. `gainDbProperty()`), updating call sites. That is a small,
behavior-preserving refactor if we want to actually resolve it rather than NOLINT.

### 6. Member initialized in the constructor body

`cppcoreguidelines-prefer-member-initializer`.

- `rock-hero-common/audio/src/live_rig_gain_plugin.cpp:38` — `m_last_target_linear_gain`.

This one genuinely cannot move to the member-init list: its value comes from
`targetLinearGain()`, which depends on `setTargetGainDb(...)` called earlier in the
constructor body. The field already has a header default initializer, which did not
silence the check. Resolution is either a constructor restructure or a NOLINT with the
"depends on prior body call" justification.

### 7. Exception escape from a destructor

`bugprone-exception-escape`.

- `rock-hero-common/audio/src/audio_device_settings.cpp` — `~Impl` calls
  `restorePreviousRouteBestEffort()`, which constructs `std::string`s (can throw
  `bad_alloc`).

Open question: wrap the destructor body in `try { ... } catch (...) {}` (best-effort
cleanup already, so swallowing is consistent), mark/declare `noexcept` intentions, or
NOLINT?

### 8. Deterministic hash formatting via snprintf

`cppcoreguidelines-pro-type-vararg`.

- `rock-hero-common/audio/src/audio_normalization.cpp:186` — `std::snprintf(..., "%.1f", ...)`
  inside `formatGainForHash`.

This is **deliberate**: the comment notes snprintf with `"%.1f"` is used for
cross-platform deterministic one-decimal formatting that feeds a persisted validation
hash. Changing the formatter (e.g. to `std::format`) risks altering existing hashes.

Open question: confirm `std::format("{:.1f}", x)` is byte-identical for the value range
we hash (then migrate), or NOLINT to preserve the proven snprintf path.

### 9. Identifier naming

`readability-identifier-naming`.

- `rock-hero-editor/ui/tests/test_editor.cpp:251` — private member `device_manager`
  flagged (expected `m_`-prefixed style for a private member).

Open question: rename to match the member convention, or is this a struct-like test
helper where the bare name is intended? (If the latter recurs, it may indicate a
fake-type convention worth documenting.)

### 10. Provably-safe production optional access

`bugprone-unchecked-optional-access`. Both are production code where the value is
established by construction but clang cannot track it across the call.

- `rock-hero-editor/core/src/editor_controller.cpp:1165` — `m_project` is move-assigned
  immediately above (line ~1154) before `m_project->...`.
- `rock-hero-editor/core/src/input_calibration_workflow.cpp:407` — guarded by the
  `calibrationMatches(...)` helper, which checks `has_value()` internally where
  clang cannot see it.

Open question: restructure to make the invariant visible to the checker (e.g. a local
reference bound inside an explicit `if (...has_value())`), or NOLINT with a
"checked via <helper>/assignment" justification.

## Suggested next steps

1. Decide a default policy for the framework-boundary findings (sections 1, 2, 4): most
   likely targeted NOLINT-with-justification, since they are dictated by JUCE/Tracktion
   APIs. If we prefer zero NOLINT, sections 1 and 2 need small wrapper/helper types.
2. Do the safe refactors where we agree they are worth it: section 5 (function-local
   static), section 7 (try/catch in the destructor), section 10 (visible guards).
3. Validate the hash-formatting question (section 8) before touching `formatGainForHash`.
4. Run a full local build + `ctest` to confirm the committed mechanical changes compile
   and pass before pushing (clang-tidy was run read-only; no build has been done here).
