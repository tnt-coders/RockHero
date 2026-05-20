# Exploring C++20 Coroutines for the Editor Busy-Operation Pattern

Investigation of whether C++20 coroutines would meaningfully simplify the editor's busy
operation lifecycle, currently implemented via `runBusyOperation` + `safeCallback` helpers
in `rock-hero-editor/core/src/editor_controller.cpp`. **Status: investigation only,
no commitment to migrate.**

## Current shape (post-refactor baseline)

Each long-running editor operation today is a triple:

1. **Entry point method** that calls `runBusyOperation(op, state, worker, completion)`.
   The helper begins the busy state, submits a worker to the task runner, and wraps the
   completion in a liveness + token check.
2. **Worker lambda** that runs on the task runner, mutates a shared task state.
3. **Completion method** (e.g. `completeOpenProject`) that runs on the message thread,
   typically with the captured task state, and either finishes the busy operation or
   chains into a further stage (live rig load, deferred action, etc.).

For multi-stage operations (project open → live rig load → final commit), the chain is
explicit: completion methods schedule further callbacks. The code lives across three or
more functions per operation.

The shape works, but it has costs:

- The lifecycle of an operation is **spread across multiple functions**, with shared
  state passed via `shared_ptr<TaskState>` so each stage can read what previous stages
  produced.
- Sequencing is implicit — to understand "what happens after open succeeds," a reader
  has to follow the chain from `completeOpenProject` → `runLiveRigLoadStage` →
  `finishOpenProjectAfterLiveRigLoad`.
- Adding a new stage requires creating a new method, threading the task state, and
  capturing tokens / liveness correctly at each junction.
- Test coverage for "the full open sequence" requires either driving each finalizer
  manually or running the message loop and the task runner.

## What coroutines would look like

A C++20 coroutine lets a function `co_await` an asynchronous operation and resume on
completion, with locals (instead of `shared_ptr<TaskState>` fields) preserving state
across the await. With a suitable runtime, the open sequence could shrink to roughly:

```cpp
Task<void> EditorController::Impl::openProject(
    std::filesystem::path file, bool clear_last_open_project_on_failure)
{
    auto busy_guard = co_await beginBusyScoped(BusyOperation::OpeningProject);

    Project project;
    auto open_result = co_await m_task_runner->run([&] {
        return m_open_function(project, file);
    });

    if (!open_result.has_value())
    {
        if (clear_last_open_project_on_failure && m_settings != nullptr)
        {
            m_settings->setLastOpenProject(std::nullopt);
        }
        finishBusyOperation();
        reportError(std::string{"Could not open: "} + open_result.error().message);
        co_return;
    }

    common::core::Song song = std::move(*open_result);
    ProjectEditorState editor_state = project.editorState();

    if (!loadSessionSong(std::move(song), editor_state.selected_arrangement))
    {
        if (clear_last_open_project_on_failure && m_settings != nullptr)
        {
            m_settings->setLastOpenProject(std::nullopt);
        }
        finishBusyOperation();
        reportError(std::string{"Could not load audio from: "} + file.string());
        co_return;
    }

    auto rig_result = co_await runLiveRigLoadStage(
        songDirectoryForProject(project));

    if (!rig_result.has_value())
    {
        // ... teardown
        finishBusyOperation();
        reportError(...);
        co_return;
    }

    m_project = std::move(project);
    m_project_file = std::move(file);
    m_transport.seek(session().timeline().clamp(editor_state.cursor_position));
    m_save_requires_destination = false;
    m_has_unsaved_changes = false;
    clearDeferredAction();
    finishBusyOperation();
}
```

The entire open sequence reads top-to-bottom in one function, with normal local
variables across `co_await` points. No `shared_ptr<OpenTaskState>`. No splitting across
`completeOpenProject` / `finishOpenProjectAfterLiveRigLoad`.

## What would have to be built

A C++20 coroutine integration is **not free** — you need infrastructure:

1. **A `Task<T>` type** that satisfies the coroutine promise contract:
   - `promise_type` with `initial_suspend`, `final_suspend`, `return_value`/`return_void`,
     `unhandled_exception`.
   - An awaitable interface (`await_ready`, `await_suspend`, `await_resume`) for the
     consumer side.
   - Cancellation hooks (more on this below).

2. **Awaitable adapters for the existing async primitives**:
   - `ITaskRunner::run(...)` returning a `Task<T>` that resumes on the message thread.
   - `runAfterBusyOverlayPaintedOrNow(...)` returning a `Task<void>`.
   - `juce::Timer::callAfterDelay(...)` returning a `Task<void>`.
   - Possibly `ILiveRig::loadRig(...)` returning a `Task<...>`.

3. **A scheduler / runner that owns coroutine lifetime** — usually a list of in-flight
   `Task<>` handles owned by `Impl`. When `Impl` dies, the scheduler aborts (via symmetric
   transfer or explicit destruction) all in-flight coroutines.

4. **Cancellation semantics** — what does it mean for a busy operation to be
   "superseded" mid-coroutine? Options:
   - The coroutine just returns naturally when its `co_await` detects the token mismatch
     (and the rest of the function unwinds via early returns).
   - The coroutine is destroyed at the next suspension point (via `coroutine_handle::destroy`).
   - The coroutine completes but `finishBusyOperation()` checks tokens and is a no-op.

   Each has trade-offs around RAII cleanup, exception safety, and visibility into "did
   it actually run?"

5. **Test infrastructure** — tests need to be able to drive the coroutine to completion
   deterministically. With the current `m_task_runner` being injectable, the same hook
   point works for coroutines, but tests would need to understand task-completion vs.
   coroutine-resumption.

## Pros

- **Top-to-bottom readability** of multi-stage operations is a real win. The cognitive
  cost of "follow the chain across N functions" disappears.
- **Locals across awaits** removes the need for `TaskState` shared_ptr plumbing. Local
  variables in the coroutine frame replace explicit per-operation structs.
- **Linear error handling** — a coroutine with early `co_return` after `reportError`
  looks like normal code, not callback-driven exit ladders.
- **Adding a new stage** is "insert another `co_await` line" instead of "add a new
  method, thread the state, capture tokens."
- **Modern C++ shape** — the codebase is already C++23 (per CLAUDE.md). Coroutines are
  fully supported.

## Cons / risks

- **Build-time and binary-size cost** — coroutine frames are heap-allocated by default;
  small but real overhead at every suspend point. Most plausible scale fine for an editor
  but worth measuring.
- **Lifetime / cancellation is subtle** — the rules for "when can I safely destroy an
  in-flight coroutine" are non-trivial. Mistakes show up as UAF, not as compiler errors.
  The current model's `m_alive` + token check is heavy-handed but bullet-proof; coroutines
  require equivalent rigor with less mechanical scaffolding.
- **Debugging is harder** — stack traces through coroutine state machines are denser
  than normal call stacks. Modern debuggers (VS, LLDB, GDB recent) handle this but
  step-through is less intuitive.
- **Library investment** — the project currently has zero coroutine infrastructure. The
  `Task<T>` + awaitable adapters layer is non-trivial to build correctly. Existing
  options:
  - **Hand-roll** — full control, learning opportunity, ~500-1000 LoC of careful code.
  - **cppcoro** — abandoned but used; not C++23-clean.
  - **stdexec / `std::execution` (P2300)** — landed in C++26, not in C++23 standard
    library yet; available via reference implementation.
  - **Concore / libunifex / Asio's awaitable** — varying degrees of fit; introduce
    external dependencies.
  None of these are drop-in. Each has trade-offs against the codebase's small dependency
  footprint policy.
- **Tracktion / JUCE integration** — the audio backend's threading model is fixed. The
  coroutine layer is purely an editor-side concern, but any await on JUCE-driven async
  (e.g. native dialogs) needs careful adapter design.
- **Test ergonomics** — controller tests today drive `m_task_runner` synchronously via
  `InlineEditorTaskRunner`. That same pattern works with coroutines but only if the
  coroutine adapter for the task runner schedules the resume synchronously. Otherwise
  tests need a message-loop pump.
- **Cognitive load for the team** — coroutines are a real C++ feature you have to
  learn. The current callback model is mundane and well-understood. If this is a
  one-person project right now, the cost lands entirely on you.

## Verdict (this investigation only)

**Worth considering, not worth starting yet.** Reasons:

- The current `runBusyOperation` + `safeCallback` model just landed and is materially
  cleaner than what came before. It should be lived with for a while before any new
  abstraction is layered.
- The actual pain point that coroutines best solve (multi-stage sequences spread across
  multiple files) is real but limited — there are 4 entry points, and only two
  (`openProject`, `importSongSource`) span more than one stage today.
- Building the `Task<T>` infrastructure is a non-trivial up-front investment with
  cancellation semantics that have to be right first time.
- The "modern C++" benefit is genuine but better realized when there's enough async
  surface to amortize the infrastructure cost.

**Revisit when:**

- Adding a new multi-stage operation that would naturally chain 3+ awaits (e.g. publish
  with multi-stage validation, batch import).
- The team grows beyond solo and others would benefit from sequential-looking flow.
- Cancellation requirements get richer (e.g. user-initiated cancel mid-operation, not
  just "supersede").
- An external `std::execution` (P2300) implementation lands cleanly in the codebase's
  dependency model.

## Concrete experiment to validate (if pursued)

Before committing, prototype with a single operation in a branch:

1. Build a minimal `Task<T>` type with single-thread message-loop scheduler.
2. Add a single awaitable for `ITaskRunner::run()`.
3. Rewrite `openProject` as a coroutine. Keep `importSongSource`, `addPlugin`, and
   writeProject in the current model side-by-side.
4. Measure: lines of code, ease of reading, test ergonomics, debuggability.
5. Decide whether to roll out further based on the comparison, not on theoretical
   appeal.

This is probably a 1-2 week investigation. Time-box accordingly.

## Out of scope

- The game-side code (`rock-hero-game/`) which has a different rendering and threading
  model.
- The audio backend (`rock-hero-common/audio/`) which is Tracktion-driven; awaitable
  adapters at that boundary would be a separate project.
- Pre-C++20 alternatives like generator macros, fibers, or stackful coroutines.