# Plan 31 — Integrated Game/Editor Workflow ("Launch Editor" + device handoff)

Status: Deferred (someday) | 2026-07-12 | baseline `refactor @ 75cc26dd`

Revisit only when ALL of: (1) the game is audible on the per-app audio foundation
(docs/plans/roadmap/14-shared-live-input-monitoring.md plus the game's native device + calibration
setup); (2) a distribution/installer story exists for the two apps; (3) the integrated workflow is
prioritized as a product goal. This is polish layered on that foundation — it changes nothing
structural and is intentionally unscheduled. Re-verify the current-state inventory before execution
per the roadmap baseline rule.

## Goal

Make the game and editor feel like one integrated environment. From inside the running game — once
the player's audio is configured — a **"Launch Editor"** menu item spawns an editor session, so
moving from playing to authoring (and back) feels seamless rather than like switching between two
unrelated installs.

## Non-goals

- Simultaneous audio in both apps on one interface. This is a device **handoff**, not concurrent
  streaming (see Constraints/design). A shared audio server was evaluated and rejected (the "Rock
  Hero Audio Control Panel" analysis); **single active client** is the ASIO stance.
- A shared audio-config store. The audio model is **per-app independent** — the editor optionally
  mirrors the game read-only (docs/plans/roadmap/14 and the per-app audio design). This plan does not
  reintroduce shared settings.
- The installer/distribution mechanism itself. This plan consumes whatever install story exists; it
  does not create one.

## Constraints

Applicable subset of the roadmap's non-negotiable block (see docs/plans/roadmap/00-roadmap.md):

- (a) **Layering**: the device-ownership arbiter is `common/audio`; the "Launch Editor" menu is
  `game/ui`; editor discovery is game-side. `common` never depends on `editor`/`game`.
- (c) **Naming firewall**: never name the commercial real-guitar game; RS/neutral.
- (h) **Builds**: through `.agents/rockhero-build.ps1`.
- (i) **Real guitar input**: the handoff must never assume a keypress to arm audio.

## Dependencies

- **docs/plans/roadmap/14-shared-live-input-monitoring.md** — the shared calibrate-first monitoring gate
  the handoff hands the device to.
- **docs/plans/roadmap/13-audio-device-settings-and-calibration.md** — device open/restore; the
  single-active-client ASIO stance this plan escalates from.
- **docs/plans/roadmap/26-game-startup-menus-library.md** — the game menu the "Launch Editor" item lives
  in.
- A **distribution/installer story** — does not exist today (both apps build locally).

## The two real design points (already reasoned through)

### 1. Audio device handoff — a HANDOFF, not simultaneous audio

ASIO is single-client and the driver is instantiated **in-process** (the device handle can only
live inside whichever process is streaming), so the game and editor cannot both hold the same
interface at once. The established project stance (decided during the per-app audio-config design;
recorded in docs/plans/roadmap/00-roadmap.md "Decisions", RM-4) is **single active client** — one app
holds the device, the other gets a typed "device busy" error — with an **in-process device lease**
as the escalation path when concurrent use is genuinely justified.

"Launch Editor" does not need simultaneous audio — you edit *or* you play — so it is exactly that
escalation: an explicit device **handoff**.

- The active app holds the device; on switching to the other it releases and the other acquires. A
  one-time ASIO close/open glitch at a *deliberate* switch is acceptable (unlike rapid alt-tab,
  which is why an always-on focus lease was rejected for the general case).
- Mechanism: an `IDeviceOwnershipArbiter` port (acquire / release / who-owns), backed by an
  in-process lease. "Launch Editor" is the concrete workflow that justifies building it. Flow: game
  releases the device as it hands focus to the editor → editor acquires → reverse on return.

**Seam preserved now (zero extra work):** each app's device-acquisition path is kept
*arbiter-shaped* — device open goes through a seam a lease can later wrap — so the per-app audio
foundation is not re-plumbed when the arbiter lands. The per-app design (each app owns its own
device open) already satisfies this.

**Unknown to verify when building:** JUCE's exact runtime mechanism for releasing an ASIO device
without tearing down the whole engine, and re-acquiring it later — confirm with the
juce-tracktion-expert against `external/tracktion_engine/` before committing the handoff design.

### 2. Editor discovery across separate installs

The apps are separate installations, so "Launch Editor" must handle "editor not present":

- Either **bundle** the editor into the game's install (so the menu item always works), or
- keep them separate and have the game **locate** the editor (registry / known install path / a
  config value), spawn it if found, and **fail gracefully** otherwise — "Editor isn't installed —
  get it here."

Design the menu action as locate → spawn-if-found → friendly pointer if not. Distribution-era
decision; do not design it before an installer story exists.

## Phased implementation (sketch — not execution-ready)

When the triggers hold, the expected shape is:

1. **Device arbiter.** `common::audio::IDeviceOwnershipArbiter` port + in-process lease
   implementation; both apps acquire on focus/launch and release on the switch. Verify the JUCE
   runtime ASIO release/re-acquire mechanism first.
2. **Game "Launch Editor" action** (native SDL menu, in plan 26's menu system) with editor
   discovery (locate → spawn → graceful "not installed" pointer), gated on the editor being
   locatable.
3. **Wire both apps' device-open paths through the arbiter** (the seam already kept arbiter-shaped
   in the per-app foundation).

## Open questions (deferred — decide when built)

- **31-Q1** Editor delivery: bundle the editor with the game's install vs. separate installs +
  runtime discovery. A distribution decision; defer until an installer story exists.
- **31-Q2** Handoff trigger: focus-driven (automatic on switching apps) vs. explicit ("take
  control" action). Decide informed by how jarring the ASIO re-init proves in practice.

## Current state

Not started; intentionally deferred. The per-app audio foundation keeps device acquisition
arbiter-shaped so this slots in without re-plumbing. See docs/plans/roadmap/14 for the monitoring gate
and the per-app audio model this builds on.
