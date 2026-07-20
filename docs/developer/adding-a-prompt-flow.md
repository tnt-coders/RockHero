\page guide_add_prompt Adding a Prompt or Confirmation Flow

*Applies to: Editor-only.*

Use this checklist when an operation needs to ask the user something before (or instead of)
proceeding — a confirmation, a decision between destructive options, a blocking notice. The
editor has two prompt shapes; pick the right one first:

- **A simple decision prompt** — the operation raises a question, the answer resolves it
  (tone-import confirmation, game-audio-unavailable notice).
- **A deferred lifecycle action** — an action that must first settle unsaved changes (open,
  import, new, exit). These do *not* hand-roll prompts: they ride the stash-and-replay machine,
  `DeferredProjectActionState`.

Both shapes share one law: the prompt is **view state**, the answer is **a new intent/action
that re-enters the pipeline**. The controller never blocks waiting for an answer, and the
prompt struct carries only display facts — the resolve step re-derives everything else, because
the world may have changed while the dialog sat open.

# Shape 1 — a simple decision prompt

1. **Prompt struct + decision enum** in `editor_view_state.h` (exemplars: `ToneImportPrompt` +
   `ToneImportDecision`; `UnsavedChangesPrompt` + `UnsavedChangesDecision`). If the prompt
   should present once per distinct cause rather than per derivation, hand-write `operator==`
   over the identity fields only — `GameAudioUnavailablePrompt` compares its reason code alone,
   and that choice is what makes present-once tracking work.
2. **`std::optional<Prompt>` field on `EditorViewState`**, populated in `deriveViewState()` —
   a forgotten derivation means the prompt silently never appears.
3. **Pending state on the controller** (`editor_controller_impl.h`): stash whatever the resolve
   needs (the pending action value, a counter). Prefer a variant phase struct if the flow has
   more than one waiting state (see "State machines as variants" in \ref guide_patterns).
4. **The decision intent**: `onXxxDecision(...)` on `IEditorController` — the compiler then
   forces the `EditorController` forwarder, the `Impl` member, and the
   `RecordingEditorController` override. For action-shaped resolves, a `ResolveXxxPrompt`
   editor action re-entering `runAction` (see `ResolveToneImportPrompt`) keeps gating uniform.
5. **View render**: show through `showThemedDialogModally` (`themed_message_box.h`) — it owns
   the `SafePointer` liveness guard, delivers the result only while the owner lives, and gives
   Return/Esc their button meanings. Track presented-prompt identity so a re-derivation doesn't
   re-open the same dialog.
6. **Tests**: prompt appears under the triggering conditions; each decision value resolves
   correctly; the stale case (state changed while the prompt was open) is handled; no prompt
   re-presents on an unchanged re-derivation.

# Shape 2 — a deferrable lifecycle action

If the new operation is a project-lifecycle action that must respect unsaved changes, do not
build any of the above. Add the action to `ProjectAction` (and `ProjectWriteAction` if it
writes) in `editor_action.h` — see \ref guide_add_action — and the existing machine does the
rest: `DeferredProjectActionState` (`deferred_project_action_state.h`) stashes the action,
raises the unsaved-changes prompt, and replays via `takeReplay` exactly once on
save-then-replay / discard-and-replay. Its phase variant makes "two parked actions" and
"prompt with nothing parked" unrepresentable — extend the variant, never bypass it. The
unsaved-changes prompt wording switch in `editor_view.cpp` (exhaustive over `EditorActionId`)
will force the new action's wording at compile time.

The full open-flow choreography, including how the deferral interacts with busy operations and
the dirty gate, is in \ref guide_project_lifecycle.
