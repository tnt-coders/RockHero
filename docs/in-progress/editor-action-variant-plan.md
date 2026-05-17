# Editor Action Variant Refactor

## Goal

Collapse `EditorAction` and `ProjectCommand` into a single `std::variant`-based action type whose
cases are nested inside an outer `struct EditorAction`. The deferred-action slot holds the
original action directly, eliminating the parallel `ProjectCommand` type, its synthetic save
factory, and the parallel id enum. View state references `EditorAction::Id` directly instead of a
narrowed mirror enum.

This is primarily a structural refactor, with one intentional behavior fix: `RestoreProject`
routes through the same dirty gate as other project-replacement actions. No new features, no
changes to busy-policy or availability rules. Undo/redo is explicitly out of scope.

## Current State

`rock-hero-editor/core/src/editor_action.h` and `rock-hero-editor/core/src/project_command.h`
declare two value objects that share the same hand-rolled tagged-union pattern:

- An `Id` enum identifies the case.
- Every payload field for every case is stored as an independent member, always allocated regardless
  of which case the instance holds.
- One named factory function per case constructs an instance with its payload.
- Payload accessors are untyped against the id; calling `takeFile()` on a `PlayPause` action is a
  silent contract violation.

`EditorAction` carries 15 cases and 4 payload fields. `ProjectCommand` carries 7 cases and 2 payload
fields. The seven `ProjectCommand` cases are exactly the project-lifecycle subset of `EditorAction`:
the handler for `EditorAction::Id::OpenProject` constructs a `ProjectCommand::open(...)` with the
same payload, and the equivalent holds for `ImportProject`, `SaveProject`, `SaveProjectAs`,
`PublishProject`, `CloseProject`, and `ExitApplication`.

`ProjectCommand` is what the controller stashes in `m_pending_project_command` while an
unsaved-changes or Save As prompt is open. The view sees that pending command's identity via the
public `ProjectCommandId` enum in `editor_view_state.h`, referenced by the `command` field on
`UnsavedChangesPrompt` and `SaveAsPrompt`.

`ProjectCommand` also carries a `clears_deferred_command_on_failure` flag. The flag is set by a
synthetic `saveBeforeDeferredCommand()` factory that is only ever invoked from
`ResolveUnsavedChangesPrompt → Save`; on save failure, the flag causes the deferred command to be
dropped.

`EditorController::Impl` dispatches actions through `performAction(EditorAction)`, a switch on
`action.id()`. Availability and busy policy live in `canRunAction(EditorAction::Id)` and
`actionBusyPolicy(EditorAction::Id)`, both id-keyed. `deriveAndPush()` calls `canRunAction` with
explicit ids to populate menu enable flags.

## Latent Issue Resolved by This Refactor

The current dispatcher routes `OpenProject` through `requestProjectCommand`, which consults the
unsaved-changes dirty gate. `RestoreProject`, however, bypasses the gate and calls
`openProject(file, true)` directly. The behavior is safe today only because `RestoreProject` is
fired exactly once, from `Impl` startup, at a moment when no project is loaded and nothing is dirty.
If any future caller wires `RestoreProject` to a "reopen last session" menu item, a crash-recovery
flow, or a settings-driven reload, the dirty gate would be silently skipped and an in-progress
project would be overwritten without prompting.

The refactor closes this by routing `RestoreProject` through the same dirty-gate helper as every
other lifecycle action. Startup behavior is unchanged (the gate is a no-op when nothing is dirty);
future call sites become safe by construction.

## Design Rationale

`ProjectCommand` exists today because the controller needs to stash a unit of project-lifecycle
work while a prompt is open. It stashes a re-encoded copy of the original action. If the controller
stashes the original `EditorAction` instead, `ProjectCommand` evaporates: there is no second
discriminated union to maintain, no duplicate case-struct layer, and no synthetic
`saveBeforeDeferredCommand` factory.

`ProjectCommandId` exists today because the view needs to identify which action a prompt is about,
and the routing enum was kept private. With the routing enum promoted to public (`EditorAction::Id`
becomes part of `editor_view_state.h`'s consumer surface), the parallel narrow enum is no longer
needed. The view references `EditorAction::Id` directly; its prompt-rendering switch handles
deferrable values explicitly and falls through to a defensive default for values that cannot occur
by invariant.

The three responsibilities currently carried by `ProjectCommand` are redistributed:

1. **Stash slot.** `m_pending_project_command` becomes `m_deferred_action`, a bare
   `std::optional<EditorAction::Action>`. Resuming a deferred action means feeding the stored
   variant value back through `runAction`. No re-encoding.
2. **Synthetic save flag.** `clears_deferred_command_on_failure` was carrying one bit of context
   ("this save was synthesized by prompt resolution, so drop the deferred work on failure"). With
   the original action stashed, the controller's save handler reads
   `m_deferred_action.has_value()` directly at the moment the save completes. The flag disappears.
3. **Public id surface.** `ProjectCommandId` is deleted, replaced by direct references to
   `EditorAction::Id`. The view's prompt switch handles the five deferrable values and uses a
   default branch for the rest. The `command` field on `UnsavedChangesPrompt` and `SaveAsPrompt`
   is renamed to `prompted_action`.

The "only project-lifecycle actions get stashed" invariant is enforced by call-site discipline:
the slot is only assigned from inside the dirty-gate helper, which is only invoked from a small set
of `performActionImpl` overloads. No type-system encoding (marker base, concept, slot wrapper,
variant subset) is introduced for this invariant — the constraint is local, trivially reviewable,
and a shared abstraction would be a false unification for a future deferral mechanism with
different membership.

The translation from "the deferred action's identity" to view state is just `idOf(action)`. There
is no separate `promptedActionFor`, `deferredActionIdOf`, or `PromptedAction` enum. One function,
one enum.

## Scope

In scope:

- replace `EditorAction` with an outer `struct EditorAction` containing a nested `Id` alias, case
  structs, and a nested `using Action = std::variant<...>` alias
- delete `ProjectCommand` entirely (header, implementation, factories, payload fields)
- delete `ProjectCommandId` from `editor_view_state.h`
- expose `EditorAction::Id` to consumers of view state: introduce a small public header that
  declares the enum (or its base) in a location the view can include
- rename the `command` field on `UnsavedChangesPrompt` and `SaveAsPrompt` to `prompted_action`,
  retyped as `EditorAction::Id`
- introduce `idOf(const EditorAction::Action&) -> EditorAction::Id`
- store the deferred action as `std::optional<EditorAction::Action>`
- route `RestoreProject` through the standard dirty-gate helper instead of the direct
  `openProject(file, true)` bypass
- adapt controller dispatch, id-keyed policy tables, and view-state derivation
- update tests to use brace-init construction and variant pattern matching

Out of scope:

- undo/redo
- separating document edits from UI intents
- a generic "deferrable" abstraction shared across multiple deferral mechanisms
- compile-time enforcement of the slot's membership rule
- changes to busy-policy or availability rules
- changes to `UnsavedChangesDecision` or supporting types beyond prompt-state and prompt-rendering
  updates needed by this refactor
- new actions, new commands, or any behavior change
- changes to the controller's public API surface (named methods like `openProject(path)` remain)

## Target Design

### EditorAction

`editor_action.h` declares a single outer struct holding the case structs and the variant alias:

```cpp
namespace rock_hero::editor::core
{

struct EditorAction
{
    using Id = EditorActionId;  // declared in the public header below

    struct OpenProject
    {
        explicit OpenProject(std::filesystem::path file_value);
        std::filesystem::path file;
    };
    struct RestoreProject
    {
        explicit RestoreProject(std::filesystem::path file_value);
        std::filesystem::path file;
    };
    struct ImportSong
    {
        explicit ImportSong(std::filesystem::path file_value);
        std::filesystem::path file;
    };
    struct SaveProject                 {};
    struct SaveProjectAs
    {
        explicit SaveProjectAs(std::filesystem::path file_value);
        std::filesystem::path file;
    };
    struct PublishProject
    {
        explicit PublishProject(std::filesystem::path file_value);
        std::filesystem::path file;
    };
    struct CloseProject                {};
    struct ExitApplication             {};
    struct ResolveUnsavedChangesPrompt
    {
        explicit constexpr ResolveUnsavedChangesPrompt(
            UnsavedChangesDecision decision_value) noexcept;
        UnsavedChangesDecision decision;
    };
    struct CancelSaveAsPrompt          {};
    struct PlayPause                   {};
    struct Stop                        {};
    struct SeekWaveform
    {
        explicit constexpr SeekWaveform(double normalized_x_value) noexcept;
        double normalized_x;
    };
    struct AddPlugin
    {
        explicit AddPlugin(std::filesystem::path file_value);
        std::filesystem::path file;
    };
    struct RemovePlugin
    {
        explicit RemovePlugin(std::string instance_id_value);
        std::string instance_id;
    };

    using Action = std::variant<
        OpenProject, RestoreProject, ImportSong,
        SaveProject, SaveProjectAs, PublishProject,
        CloseProject, ExitApplication,
        ResolveUnsavedChangesPrompt, CancelSaveAsPrompt,
        PlayPause, Stop, SeekWaveform,
        AddPlugin, RemovePlugin>;
};

[[nodiscard]] EditorAction::Id idOf(const EditorAction::Action& action);

} // namespace rock_hero::editor::core
```

The `EditorActionId` enum lives in a public header so view-state consumers can name its values:

```cpp
// rock-hero-editor/core/include/rock_hero/editor/core/editor_action_id.h
namespace rock_hero::editor::core
{

enum class EditorActionId : std::uint8_t
{
    OpenProject,
    RestoreProject,
    ImportSong,
    SaveProject,
    SaveProjectAs,
    PublishProject,
    CloseProject,
    ExitApplication,
    ResolveUnsavedChangesPrompt,
    CancelSaveAsPrompt,
    PlayPause,
    Stop,
    SeekWaveform,
    AddPlugin,
    RemovePlugin,
};

} // namespace rock_hero::editor::core
```

`editor_view_state.h` includes `editor_action_id.h` so its prompt structs can store
`EditorActionId` values.

Call sites brace-init the case directly; the variant's converting constructor wraps it:

```cpp
runAction(EditorAction::OpenProject{std::move(file)});
runAction(EditorAction::ResolveUnsavedChangesPrompt{decision});
runAction(EditorAction::SeekWaveform{normalized_x});
runAction(EditorAction::PlayPause{});
```

### View state

`UnsavedChangesPrompt` and `SaveAsPrompt` rename their `command` field to `prompted_action` and
retype it as `EditorActionId`:

```cpp
struct UnsavedChangesPrompt
{
    explicit constexpr UnsavedChangesPrompt(EditorActionId action) noexcept;
    EditorActionId prompted_action;
    friend bool operator==(
        const UnsavedChangesPrompt& lhs, const UnsavedChangesPrompt& rhs) = default;
};

struct SaveAsPrompt
{
    explicit constexpr SaveAsPrompt(EditorActionId action) noexcept;
    EditorActionId prompted_action;
    friend bool operator==(const SaveAsPrompt& lhs, const SaveAsPrompt& rhs) = default;
};
```

`ProjectCommandId` is removed from `editor_view_state.h`.

### Deferred-action slot

```cpp
std::optional<EditorAction::Action> m_deferred_action;
```

The slot accepts any `Action` by type. The invariant "only project-lifecycle actions get stashed
here" is enforced by call-site discipline: `m_deferred_action` is only assigned from inside the
dirty-gate helper, which is only invoked from `performActionImpl` overloads for the lifecycle
cases.

### Dispatch

`performAction(EditorAction::Action)` becomes:

```cpp
void Impl::performAction(EditorAction::Action action)
{
    std::visit(
        [this](auto&& a) { performActionImpl(std::forward<decltype(a)>(a)); },
        std::move(action));
}
```

One `performActionImpl` overload per alternative. Bodies are the same as today's switch cases, with
payload access through typed struct fields:

```cpp
void Impl::performActionImpl(EditorAction::OpenProject a)
{
    requestProjectAction(EditorAction::OpenProject{std::move(a.file)});
}

void Impl::performActionImpl(EditorAction::RestoreProject a)
{
    requestProjectAction(EditorAction::RestoreProject{std::move(a.file)});
}

void Impl::performActionImpl(EditorAction::SaveProject)
{
    if (m_save_requires_destination) return;
    runProjectAction(EditorAction::SaveProject{});
}

void Impl::performActionImpl(EditorAction::PlayPause) { /* ... */ }
```

The existing `requestProjectCommand` and `runProjectCommand` helpers become
`requestProjectAction(EditorAction::Action)` and `runProjectAction(EditorAction::Action)`. They
keep their current job: `requestProjectAction` consults the dirty gate and may stash + prompt
before dispatching; `runProjectAction` handles the accepted project-lifecycle action.
`performAction` remains the top-level intent dispatch, while project-action continuation stays in
the project helper path.

`RestoreProject`'s implementation is the same `openProject(file, true)` call as today, but now
reached through the standard gate path rather than as a direct bypass.

### Deferred-action replay

```cpp
case UnsavedChangesDecision::Save:
{
    if (m_save_requires_destination)
    {
        m_save_as_prompt_visible = true;
        deriveAndPush();
        return;
    }
    runProjectAction(EditorAction::SaveProject{});  // success replays, failure clears
    break;
}
case UnsavedChangesDecision::Discard:
{
    EditorAction::Action replay = std::move(*m_deferred_action);
    m_deferred_action.reset();
    m_has_unsaved_changes = false;
    m_save_requires_destination = false;
    if (idOf(replay) != EditorAction::Id::CloseProject)
    {
        if (closeProject()) runProjectAction(std::move(replay));
    }
    else
    {
        continueDeferredAction();
    }
    break;
}
```

### Availability and busy policy

`canRunAction(EditorAction::Id)` and `actionBusyPolicy(EditorAction::Id)` keep their current shape
and contents. Call sites in `deriveAndPush()` change only in the enum spelling:

```cpp
state.open_enabled = canRunAction(EditorAction::Id::OpenProject);
```

`runAction(EditorAction::Action)` calls `idOf(action)` once at entry to feed the gate:

```cpp
void Impl::runAction(EditorAction::Action action)
{
    const EditorAction::Id id = idOf(action);
    if (!prepareAction(id)) return;
    performAction(std::move(action));
}
```

### View-state derivation

When the deferred slot is occupied, `deriveAndPush` populates `UnsavedChangesPrompt` directly from
the action's id:

```cpp
if (m_deferred_action.has_value())
{
    state.unsaved_changes_prompt = UnsavedChangesPrompt{idOf(*m_deferred_action)};
}
```

The view's prompt-rendering code switches on `EditorActionId` and renders text per case, with a
default branch for values that cannot occur by invariant.

## Affected Files

- `rock-hero-editor/core/include/rock_hero/editor/core/editor_action_id.h` (new public header
  declaring the enum)
- `rock-hero-editor/core/include/rock_hero/editor/core/editor_view_state.h` (delete
  `ProjectCommandId`, include `editor_action_id.h`, rename prompt fields)
- `rock-hero-editor/core/src/editor_action.h` (rewrite)
- `rock-hero-editor/core/src/editor_action.cpp` (rewrite: factory functions go away; `idOf`
  implementation lives here)
- `rock-hero-editor/core/src/project_command.h` (delete)
- `rock-hero-editor/core/src/project_command.cpp` (delete)
- `rock-hero-editor/core/CMakeLists.txt` (drop the deleted sources, add the new public header)
- `rock-hero-editor/core/src/editor_controller.cpp` (dispatch, id-keyed tables, deferred-slot
  storage, deferred-resume path, `RestoreProject` routing through the dirty gate)
- `rock-hero-editor/core/tests/test_editor_controller.cpp` (update construction syntax; add a
  test for `RestoreProject`-via-dirty-gate)
- `rock-hero-editor/ui/` files that referenced `ProjectCommandId` or the `command` prompt field
  (rename references)

## Migration Notes

- `EditorAction` is no longer a class with factory functions; it is an outer struct containing
  nested case structs and a nested variant alias `EditorAction::Action`. Function parameter and
  member types that previously named `EditorAction` now name `EditorAction::Action`.
- `EditorAction::Id` is preserved as the spelling; it is now an alias for the publicly-declared
  `EditorActionId` enum.
- `EditorAction::takeFile()`, `takeInstanceId()`, `decision()`, and `normalizedX()` are removed.
  Replacements: `std::get<EditorAction::AlternativeType>(action).field` or pattern-match through
  `std::visit`.
- All `ProjectCommand` factories and accessors are removed. Code that called
  `ProjectCommand::open(file)` constructs `EditorAction::OpenProject{file}` directly. Code that
  consulted `command.clearsDeferredCommandOnFailure()` is replaced by the controller's local check
  of `m_deferred_action.has_value()` at save completion.
- `ProjectCommand::saveBeforeDeferredCommand()` is removed entirely. The synthetic save behavior is
  now expressed by "there is a deferred action waiting" rather than by a flag on the command.
- `ProjectCommandId` is deleted. References in view state and UI code switch to
  `EditorActionId`.
- The `command` field on `UnsavedChangesPrompt` and `SaveAsPrompt` is renamed to `prompted_action`
  and retyped as `EditorActionId`.
- `RestoreProject`'s dispatcher no longer calls `openProject(file, true)` directly. It now flows
  through the same dirty gate as `OpenProject` and reaches `openProject(file, true)` only after
  the gate clears.
- The default-constructed sentinel values (`m_id{Id::SaveProject}` etc.) disappear.
  `EditorAction::Action` is not default-constructible because payload-bearing action
  alternatives require their payload at construction. Payload-free alternatives remain
  brace-initialized with `{}`.
- `performAction` uses one overload per alternative for the top-level action dispatch. Smaller
  helper visits may use `if constexpr` ladders where they map ids or handle a narrow subset of
  alternatives.

## Tests

This is mostly a structural refactor. The `RestoreProject` routing change is the intentional
behavioral improvement and should have its own test.

- Existing `test_editor_controller.cpp` tests pass unchanged in intent. Where they construct an
  `EditorAction` or `ProjectCommand`, update the construction syntax; do not modify the
  assertions.
- Add a test that `RestoreProject` is intercepted by the dirty gate when a dirty project is
  already loaded (the previously latent case). The behavior should match `OpenProject` in the same
  scenario.
- Add a narrow `idOf` unit test if the per-library test target can host it cheaply. Optional.
- Run `ctest --preset debug` once the user is ready to verify.

If a test currently depends on the old default-constructed sentinel or on calling a payload
accessor for the wrong case, treat that as a bug uncovered by the refactor and surface it.

## Settled Decisions

- `EditorAction` is the single dispatch type. `ProjectCommand` is deleted, not refactored.
- The case structs are nested inside an outer `struct EditorAction`, not flattened in the
  namespace and not split across nested namespaces.
- The variant alias is `EditorAction::Action`. Function parameter and member types use this name;
  call sites construct cases directly (`EditorAction::OpenProject{...}`) and rely on the variant's
  converting constructor.
- The deferred slot is a bare `std::optional<EditorAction::Action>`. The invariant "only
  project-lifecycle actions are stashed" is enforced by call-site discipline. A compile-time
  encoding (marker base, concept, slot wrapper, variant subset) was considered and rejected.
- `RestoreProject` routes through the standard dirty gate, closing a latent gap in the current
  design.
- `clears_deferred_command_on_failure` is removed. Its behavior is expressed at the call site by
  checking `m_deferred_action.has_value()` when the save completes.
- `ProjectCommandId` and `PromptedAction` are not introduced. View state references
  `EditorActionId` (aliased as `EditorAction::Id`) directly; the routing enum is promoted to
  public. The view's prompt switch handles deferrable values explicitly with a defensive default
  for the rest.
- `EditorActionId` lives in a small public header (`editor_action_id.h`) so view state can include
  it without pulling in the action case structs.
- Top-level action dispatch uses one overload per alternative. Narrow helper visits may use
  `if constexpr` ladders where that keeps local subset handling straightforward.
