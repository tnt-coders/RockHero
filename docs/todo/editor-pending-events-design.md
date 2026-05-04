# Editor Pending-Events View State (Deferred)

Status: deferred. Not part of the v20 refactor plan. Revisit when a second
user-facing notification kind appears in the editor.

## Why This Is Deferred

The v20 plan currently models load failure as
`std::optional<std::string> last_load_error` on `EditorViewState`. That field
covers exactly one event kind and works fine while the editor only surfaces load
failures. The design below replaces it with an event-list channel that scales
naturally to additional notification kinds, but that scaling problem does not
exist yet — the editor has no second notification kind in flight.

Reach for this design when one of the following becomes real, not before:

- A second user-visible notification kind needs to be surfaced (save failed,
  device disconnected, plugin scan failure, calibration warning, "song saved"
  toast, import-complete confirmation, etc.).
- Modal/toast dismissal lifecycle becomes a UX concern that the
  optional-string-on-state model is producing bugs around.
- A test wants to assert "this notification was published" in a way that the
  controller's `last_load_error` cannot express.

Until then, prefer the simpler `std::optional<std::string>` field. Premature
generalisation here costs more in machinery than it saves in flexibility.

## The Idea

Replace `std::optional<std::string> last_load_error` with an event list, plus a
view-driven acknowledgement intent. The view tracks already-presented event ids
locally; the controller mints unique ids and removes acknowledged ones from the
list before the next state push.

This keeps the single `setState(const EditorViewState&)` channel intact,
preserves snapshot equality, gives tests an observable list to assert on, and
admits new event kinds by adding a variant alternative rather than growing the
view interface.

## Sketch

In `editor_view_state.h`:

```cpp
struct EditorEventId
{
    std::uint64_t value{0};
    constexpr EditorEventId() noexcept = default;
    explicit constexpr EditorEventId(std::uint64_t id_value) noexcept
        : value{id_value} {}
    [[nodiscard]] constexpr bool isValid() const noexcept { return value != 0; }
    friend auto operator<=>(const EditorEventId&, const EditorEventId&) = default;
};

struct LoadFailed
{
    std::string message;
    friend bool operator==(const LoadFailed&, const LoadFailed&) = default;
};

// Extend with new alternatives as new event kinds are introduced.
using EditorEventPayload = std::variant<LoadFailed>;

struct EditorEvent
{
    EditorEventId id;
    EditorEventPayload payload;
    friend bool operator==(const EditorEvent&, const EditorEvent&) = default;
};

struct EditorViewState
{
    // ...existing fields, with last_load_error removed...
    std::vector<EditorEvent> pending_events;
};
```

In `i_editor_controller.h`:

```cpp
// In addition to the existing intents.
virtual void onEditorEventAcknowledged(EditorEventId event_id) = 0;
```

## Controller Behavior Notes

When this design is adopted, the load workflow rules change as follows
(replacing the v20 stage-7 behavior bound to `last_load_error`):

- **Mint ids monotonically.** The controller owns a counter; each new event gets
  a fresh id, unique within the controller's lifetime, even if its payload is
  textually identical to a prior event.
- **On failed load.** Append a `LoadFailed` event with a freshly minted id and a
  controller-composed message. Do not mutate or clear earlier unacknowledged
  events.
- **On successful load.** Push the derived state but do not modify
  `pending_events`. A successful load does not implicitly acknowledge a prior
  failure — events live until the view acknowledges them.
- **On internal-consistency failure** (post-edit `Session::setAudioClip`
  returning `false`): append a `LoadFailed` event whose message identifies the
  internal-consistency case, distinct from user-facing load failures.
- **On `onEditorEventAcknowledged(id)`.** Remove the event with the matching id
  from `pending_events` (if present) and push a fresh derived state. Tolerate
  unknown ids as no-ops without pushing redundant state — the view may
  legitimately ack an event the controller has already removed.

## View Behavior Notes

- The view tracks already-presented event ids locally (an
  `std::unordered_set<EditorEventId>` keyed on `value` is sufficient) so it does
  not re-render the same notification on every unrelated state push.
- When the user dismisses a notification (closes a modal, dismisses a toast,
  clicks "OK"), the view emits `onEditorEventAcknowledged(id)` and drops the id
  from its local presented set on the next state push that omits it.

## Tests To Add When Adopted

- `EditorViewState` carries `pending_events` round-trip through the variant.
- `EditorEventId` distinguishes distinct values, equates equal ones, supports
  ordering for use as a map key.
- Controller equality covers `pending_events` differences by id and by message.
- Failed load appends a `LoadFailed` event with a previously-unseen id.
- Successful load does not clear unacknowledged events.
- `onEditorEventAcknowledged(id)` removes the matching event and pushes fresh
  state.
- `onEditorEventAcknowledged(unknown_id)` is a no-op and does not push redundant
  state.
- Minted ids are unique even when consecutive events carry textually identical
  messages.

## Migration Cost When Adopted

Small while the editor only has the load workflow:

- Header: add four types in `editor_view_state.h`, swap one field.
- Controller interface: add one method.
- Controller implementation: replace direct `last_load_error` writes with
  `pending_events.push_back(...)`, add the id counter, add the acknowledgement
  handler.
- Tests: update existing fixtures to construct `pending_events` instead of
  `last_load_error`; add the new test cases listed above.

The migration cost grows with the number of `last_load_error` consumers, so this
should be revisited before that field is referenced in many call sites.

## Discussion Thread

Original analysis lives in conversation history. Key points:

- Errors are event-shaped, not state-shaped. A single optional field forces
  either the controller to remember to clear it on every unrelated push, or the
  view to dedup a sticky modal.
- The v20 doc string for `last_load_error` already concedes this ("...if one
  should currently be shown") — the field cannot decide whether to display its
  own contents.
- The optional-string design does not scale to a second event kind without
  either parallel optional fields, parallel one-shot view methods, or a
  re-encoded string discriminator. Each option either grows the view interface
  linearly with notifications or breaks view-as-pure-function-of-state.
- The event-list design preserves all of: single `setState` channel, snapshot
  equality, view-as-pure-function-of-state, observable-list testing, and a
  natural extension point for new kinds.
