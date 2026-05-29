# Trompeloeil Adoption Plan

Status: deferred. Revisit only when a real test needs strict interaction expectations that are
awkward or brittle with hand-written fakes.

## Purpose

Keep Catch2 as the test runner and assertion framework while defining when and how Trompeloeil may
be added for expectation-driven mock tests.

The centralized fake portion of the old plan is complete and archived in
`docs/completed/test-support-cleanup-plan.md`. This document is intentionally focused only on
future Trompeloeil adoption.

## Current Position

Rock Hero's test strategy is fake-first:

- pure and headless tests should assert returned values, state changes, and emitted snapshots;
- adapter and controller tests should use hand-written fakes for project-owned ports;
- reusable helpers live in module-owned `*_testing` targets;
- tests should not mock JUCE or Tracktion directly.

Trompeloeil is useful only when the interaction itself is the behavior being tested.

## Goals

- Add a strict mock framework only after a concrete test justifies it.
- Keep mock usage narrow and readable.
- Keep production APIs shaped by real design needs, not by mocking convenience.
- Keep `Mock*` names reserved for expectation-driven Trompeloeil types.
- Avoid a global mock target or a global fake warehouse.

## Non-Goals

- Do not replace Catch2.
- Do not rewrite existing state/output tests as mock tests.
- Do not introduce broad third-party wrapper layers only to support mocks.
- Do not mock JUCE widgets, Tracktion objects, or framework internals directly.
- Do not make every `*_testing` target depend on Trompeloeil by default.

## When To Use Trompeloeil

Use Trompeloeil when one of these is the observable behavior:

- a collaborator must not be called;
- a collaborator must be called exactly once or exactly N times;
- calls must occur in a specific order;
- a listener must be registered or unregistered with exact object identity;
- a failure path must trigger a specific escalation call;
- a dangerous backend mutation must be blocked.

Candidate future areas:

- listener registration and deregistration lifetimes where identity matters;
- safety gates that must prevent live audio or plugin mutations;
- retry, backoff, or failure-escalation workflows once those exist;
- async coordination where ordering is the contract rather than incidental implementation detail.

## When Not To Use Trompeloeil

Do not use Trompeloeil for behavior that is clearer as state/output checks:

- chart and song rules;
- timing math;
- transport position simulation;
- score calculation;
- editor view-state derivation;
- fake-backed success and failure flows where final state is the important result;
- UI layout and synchronous JUCE component wiring.

If a test can be written clearly with an existing `Recording*`, `Configurable*`, `Null*`, or
`Immediate*` helper, keep the fake.

## Integration Plan

1. Identify the first test whose behavior is genuinely an interaction contract.
2. Add Trompeloeil through the normal dependency path only as part of that test change.
3. Prefer target-private use first. Link Trompeloeil only to the test target that needs it.
4. Promote reusable `Mock*` helpers into the owning module's `tests/include/.../testing/` tree only
   after at least two test files or targets need the same mock.
5. If only one optional helper needs Trompeloeil, keep it out of the broader `*_testing` target or
   split a small opt-in mock helper target at that time.
6. Keep Catch2 assertions and test registration as the outer test framework.

## Naming Rules

- `Mock*` means a Trompeloeil expectation type.
- `Recording*` means a hand-written fake that captures calls or values for later assertions.
- `Configurable*` means a fake whose return behavior can be set by the test.
- `Null*` means a no-op implementation.
- `Immediate*` means synchronous execution of normally deferred behavior.

Do not use `Mock*` for hand-written fakes.

## Definition Of Done

The first Trompeloeil slice is complete only when:

- a concrete interaction-heavy test justifies the dependency;
- no production API was introduced only for mocking convenience;
- Trompeloeil is linked only where it is needed;
- test names and helper names make expectation-driven behavior obvious;
- existing fake-first tests remain fake-first.
