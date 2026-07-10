# Plan 29 — Online Leaderboards

**Status: Deferred + Decision-gated** — 2026-07-06 — baseline `refactor @ 3c7febe0`.
Gated on gate **G29-STABILITY** (named numeric detection/scoring stability criteria measured by
docs/roadmap/23-detection-verification-harness.md, defined in Phase 0a below) and on the
hosting/identity/licensing sign-off in Phase 0b. No phase after Phase 0 may start before both
gates close. This plan is written now so that upstream plans (10, 24, 27) bake in the interfaces
leaderboards need; execution is deliberately late in the roadmap.

## 2. Goal

After finishing a song, a player can opt in to submit their score to a small hosted service and
see ranked boards — per (chart identity, arrangement part, ruleset version, modifier class) —
shared with a friends-scale community. Boards appear on the results screen and in the song
library. Personal bests sync across reinstalls via the player's identity key.

**Honest integrity scope, stated up front:** the client computes its score from analog guitar
audio. No server can ever verify that a human played the notes — the "proof" is a microphone-side
signal only the client saw. The product scope is therefore **friends-scale boards with
score-record and replay plausibility heuristics** (consuming plan 24's per-note verdict log), not
hard anti-cheat. A determined cheater always wins; the design goal is keeping honest friends
honest and making casual tampering detectable. Every phase below is sized to that scope.

## 3. Non-goals

- Hard anti-cheat: no binary attestation, no kernel driver, no audio-upload "proof of play".
- Global public boards at scale; matchmaking; real-time multiplayer or spectating.
- Account infrastructure with passwords/email recovery flows (identity stays lightweight; see
  open question Q2).
- Content distribution: the server never stores or transmits chart content, package files, or
  audio. Converted commercial corpus content never leaves the player's machine — the server sees
  only the semantic chart-identity hash, display metadata strings, and the score record.
- Replay-video or DetectionEvent-stream sharing between players (the score record is uploaded;
  raw event streams stay local).
- Editor-side involvement of any kind — this is game-only per the layering rules.

## 4. Constraints

Applicable subset of the roadmap constraint block, restated:

- (a) **Layering**: client-side leaderboard code lives in `rock-hero-game` (game may depend on
  common; game never includes editor headers). Nothing here is needed by the editor, so no
  common-first extraction is expected; if that changes, the shared piece moves to
  `rock-hero-common` first, as its own phase with tests.
- (b) **Public-header minimalism / ports-and-adapters**: the network client is a port
  (`ILeaderboardClient`) in game/core with a JUCE-backed adapter TU, per
  docs/design/architectural-principles.md ("Ports and Adapters", "Framework-Adapter Units");
  game/core scoring and submission logic stays pure and testable with a fake adapter.
- (c) **NAMING FIREWALL**: the commercial real-guitar game that inspired this project is never
  named in any file; use "RS" or neutral phrasing. Charter (MIT) and the RS leaderboard companion
  app (github.com/tnt-coders/rock-buddy-app) may be cited as here.
- (h) **Builds**: all C++ build/test/lint through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`); intermediate phases run only determinately warranted checks; the final
  acceptance phase runs the sanctioned bundle as separate invocations.

Plan-specific constraints:

- **AGPLv3 network obligations**: the repo is AGPLv3 (root `LICENSE`). A hosted backend is
  software accessed over a network — exactly the case where AGPL section 13's network-source
  obligation bites (docs/design/architecture.md § Licensing already notes the network clause
  "only applies to software accessed over a network"; today that section discusses only the
  desktop app). If backend code shares any project source, its Corresponding Source must be
  offered to network users. This is flagged as a roadmap-level licensing decision (open question
  Q3) and requires a design-doc update with user confirmation when the backend lands.
- **Opt-in networking**: the game performs no network I/O unless the player explicitly enables
  leaderboards. Nothing phones home by default.
- **Score submission format = plan 24's score record, unchanged.** This plan may add a transport
  envelope around it but never forks or re-encodes the record itself.

## 5. Current state inventory

Verified with `rg` and file reads against the working tree:

- `LICENSE` (repo root) is the GNU Affero General Public License v3. The licensing rationale and
  dependency-compatibility table live in docs/design/architecture.md § Licensing, which states
  the network clause applies to software accessed over a network — a hosted leaderboard backend
  is that case, and the section will need an update (with user confirmation) when one exists.
- **No first-party networking code exists.** `rg` for
  `juce::URL|WebInputStream|httplib|curl|StreamingSocket` matches only vendored JUCE sources
  under `external/tracktion_engine/modules/juce/` (plus an unrelated comment about a
  "quarter-tone curl" at
  `rock-hero-common/core/include/rock_hero/common/core/chart/chart.h:111` and JUCE module wiring
  in `cmake/RockHeroExternalModules.cmake`). `conanfile.txt` declares no HTTP/TLS dependency
  (cmake-package-builder, catch2, libebur128, quill, ogg, vorbis).
- `rock-hero-game/` is a build-system skeleton: `rock-hero-game/app/main.cpp` is an 80-line JUCE
  `DocumentWindow` shell; `rock-hero-game/{core,audio,ui}/src/` each contain only
  `placeholder.cpp`.
- **No gameplay scoring code exists anywhere.** `rg -i score` over first-party sources matches
  only the GP importer's musical-score types in `rock-hero-editor/core/tests/`. The score record
  format, ruleset versioning, and no-fail labeling are owned by
  docs/roadmap/24-scoring-star-power-failure.md and do not exist yet.
- **No chart-identity hash exists.** The package reader hard-rejects `formatVersion != 1` at
  `rock-hero-common/core/src/package/rock_song_package_read.cpp:976-983` with no migration
  machinery; the semantic identity hash boards key on is owned by
  docs/roadmap/10-format-versioning-and-chart-identity.md.
- **No profile or identity concept exists** in code or settings; plan 27 introduces the profile
  id every persisted record carries.
- No doc under `docs/todo/` covers leaderboards or networking (verified by listing and grepping
  the folder); this plan absorbs nothing.

Verified against code on 2026-07-06, refactor @ 3c7febe0.

## 6. Dependencies

- docs/roadmap/10-format-versioning-and-chart-identity.md — the semantic chart-identity hash phase:
  boards key on (hash, hash-algorithm id). A byte-level hash is unusable because load
  normalization legitimately rewrites package bytes; this plan requires the semantic hash.
- docs/roadmap/22-note-detection.md — the accuracy-metric definition phase: G29-STABILITY is
  expressed in the metrics that plan defines (per-technique precision/recall, latency
  distribution).
- docs/roadmap/23-detection-verification-harness.md — the harness measures G29-STABILITY; its
  serialized DetectionEvent replay and autoplay bot generate deterministic test submissions for
  Phase 2's backend harness.
- docs/roadmap/24-scoring-star-power-failure.md — the score-record-format phase (per-note verdict
  log, ruleset version, detection-engine version, chart hash, calibration offsets, modifiers);
  this plan consumes the record unchanged and inherits the no-fail labeling policy.
- docs/roadmap/27-in-song-flow-results-profiles.md — the local score store keyed
  (chart hash, arrangement, profile, ruleset version) is designed for unchanged upload; the
  profile id and IGameSettings storage this plan's client settings live in.
- docs/roadmap/26-game-startup-menus-library.md — the menu input layer and the results/library UI
  surfaces where board views render.
- docs/roadmap/00-roadmap.md — carries gate G29-STABILITY and the AGPL/licensing decision; this
  plan is blocked until the roadmap marks the gate closed.
- External decisions: hosting choice, identity/auth choice, backend licensing stance, backend
  implementation stack (open questions Q1–Q4).

## 7. Decisions already made

- The project license is AGPLv3, chosen deliberately because JUCE requires it (root `LICENSE`;
  docs/design/architecture.md § Licensing). Any hosted component must be planned around the
  network-source obligation, not surprised by it.
- Boards key on the semantic chart-identity hash with a versioned hash-algorithm id — never a
  byte-level package hash (docs/roadmap/10-format-versioning-and-chart-identity.md).
- The score record format is defined by docs/roadmap/24-scoring-star-power-failure.md and the local
  score store in docs/roadmap/27-in-song-flow-results-profiles.md is keyed so records upload
  unchanged; leaderboards add transport framing only.
- No-fail is ON by default and runs are labeled (docs/roadmap/24-scoring-star-power-failure.md);
  boards therefore segregate or flag modifier classes rather than pretending all runs are equal.
- Corpus strategy: the 39-package .rock corpus and 101-file GP corpus are converted commercial
  content, local-only, never committed or shipped
  (docs/roadmap/23-detection-verification-harness.md); this plan extends the same rule to the
  network — chart content and audio never leave the client.
- Friends-scale integrity scope (heuristics, not hard anti-cheat) is recorded here as the
  normative scope and mirrored in docs/roadmap/00-roadmap.md.

## 8. Open questions for the user

Each is mirrored into docs/roadmap/00-roadmap.md § Decisions needed. None block writing this plan;
all block Phase 1.

- **Q1 — Hosting.** Options: (A) budget VPS (~$4–7/mo, datacenter availability, full control,
  trivial AGPL source hosting, you patch it); (B) serverless/managed free tier ($0 at friends
  scale, minimal ops, free tiers can change or vanish, some lock-in); (C) home self-host ($0
  rent, residential availability — the exact problem the RS leaderboard companion app
  (github.com/tnt-coders/rock-buddy-app) hit: boards vanish when the host machine or home
  connection is down). **Recommendation: A**, with B as the fallback if recurring cost must be
  zero; C is rejected on the availability record. Cost table refreshed in Phase 0b.
- **Q2 — Identity/auth.** Options: (A) client-generated keypair (public key = identity,
  self-asserted display name, key stored per profile; zero signup friction, no email infra; a
  lost key = lost history unless exported); (B) third-party OAuth (e.g. Discord — natural for a
  friends group, real identities, adds provider dependency and UI flow); (C) email magic links
  (requires mail-sending infra and spam handling). **Recommendation: A at v1**, with B as an
  optional later link-up; C rejected as pure overhead at this scale.
- **Q3 — Backend licensing stance.** Options: (A) backend lives in this repo under AGPLv3 and the
  API serves a Corresponding Source link (simplest compliance, allows reusing common/core record
  code for server-side re-scoring); (B) separate repo, separately licensed, communicating only
  over the wire protocol (AGPL does not cross a network API boundary, but then the backend may
  share no project source, forcing a reimplemented validator). **Recommendation: A.** Either way
  docs/design/architecture.md § Licensing gets a confirmed update when the backend lands.
- **Q4 — Backend stack.** Options: (A) small C++ service reusing the project's score-record and
  re-scoring code (single source of truth for validation; heavier to deploy); (B) small
  managed-runtime service (TypeScript/Go) with a reimplemented validator pinned to golden
  fixtures generated by plan 23's replay harness (easier hosting, dual-implementation drift
  risk). **Recommendation: A if Q3=A, otherwise B with golden-record cross-validation in CI.**
- **Q5 — Board visibility model.** Options: (A) invite-code groups (friends-scale, no discovery,
  no moderation surface for strangers); (B) one global public board (bigger, needs real
  moderation and stronger abuse handling). **Recommendation: A at v1.**
- **Q6 — Ruleset-bump policy.** When plan 24's scoring ruleset major version changes, boards:
  (A) freeze the old board read-only and start a new one per ruleset major; (B) wipe.
  **Recommendation: A** — history is cheap and players hate deletion.

## 9. Phased implementation

### Phase 0 — Decision gate: stability evidence + hosting/licensing evaluation

Scope: no code. Two sub-gates, then STOP.

**0a — G29-STABILITY (numeric criteria).** Verified from
docs/roadmap/23-detection-verification-harness.md CI reports. The numbers below are this plan's
proposed defaults; reconcile them against the metric definitions plan 22 actually ships and
record the reconciled values in docs/roadmap/00-roadmap.md as the gate:

- **Replay determinism (exact):** replaying identical serialized DetectionEvent logs through the
  current scoring ruleset produces byte-identical score records. Non-negotiable — without this,
  server-side re-scoring (Phase 2) is impossible.
- **Cross-version score stability:** on the annotated fixture corpus, per-song total-score delta
  between the two most recent detection-engine versions is ≤ 0.5% at p95 and ≤ 1.0% max.
- **Metric stability:** per-technique precision/recall (per plan 22's definitions) for every
  technique classified detected-and-scored moves ≤ 1 percentage point between those versions;
  onset-latency distribution p95 shifts ≤ 5 ms.
- **Sustained:** all criteria hold across two consecutive detection-engine releases spanning at
  least four weeks of local play. Rationale: boards comparing scores produced by unstable
  detection are noise, and every engine release would invalidate rankings.

**0b — Hosting, identity, licensing evaluation.** Refresh the cost table (figures below are
plan-date indicative — re-verify at gate close), then resolve Q1–Q6 with the user:

| Option | Examples | $/month | Availability | Ops burden | Notes |
|---|---|---|---|---|---|
| Budget VPS | Hetzner CX-class, DigitalOcean/Vultr basic | ~4–7 USD | Datacenter-grade | OS patching, backups, TLS | Fixed cost; full control; AGPL source link trivially hosted alongside |
| Serverless / free tier | Cloudflare Workers + D1, Supabase free tier, Fly.io | ~0 at friends scale | High (managed) | Minimal | Free tiers mutate/expire; some lock-in; Supabase bundles auth + Postgres |
| Home self-host | Spare machine + dynamic DNS | 0 rent | Residential-grade | Port-forwarding, uptime babysitting | Prior art: the RS leaderboard companion app (github.com/tnt-coders/rock-buddy-app) — its home-hosting availability problem is the thing to avoid |

Also in 0b: the AGPL analysis for the chosen Q3 stance, and the drafted (not applied)
docs/design/architecture.md § Licensing amendment for user confirmation.

Exit criteria: gate evidence documented; Q1–Q6 answered; roadmap gate marked closed.
**STOP — present findings and get sign-off before any later phase.**

### Phase 1 — Protocol spec and pure client port (assumes Phase 0 sign-off)

Scope: wire protocol document + headless client model. No network I/O yet.

- Protocol spec (a section of this plan, updated in place): submission = plan 24's score record
  **verbatim**, wrapped in an envelope `{protocolVersion, chartIdentityHash, hashAlgorithmId
  (both from plan 10), arrangementPart, profileIdentity (per Q2), displayName, displayMetadata
  (title/artist strings for board rendering — cosmetic, untrusted), signature}`. Board key =
  `(chartIdentityHash, arrangementPart, rulesetMajor, modifierClass)`. JSON over HTTPS, versioned
  endpoint paths (`/v1/...`), unknown-field-tolerant readers on both sides (mirrors plan 10's
  reader-tolerance policy).
- `rock-hero-game/core`: `ILeaderboardClient` port, envelope/board model types, canonical
  signature-payload serialization (sorted keys, fixed numeric formatting — same discipline plan
  10 uses for the identity hash). Fake adapter for tests.
- Files: `rock-hero-game/core/include/rock_hero/game/core/leaderboard/` (port + types, the only
  new public headers), `rock-hero-game/core/src/leaderboard/`, tests in
  `rock-hero-game/core/tests/`.
- Testing: pure unit tests — envelope round-trip, board-key derivation, canonical-payload
  stability golden files, fake-adapter port exercises (docs/design/architectural-principles.md
  § Pure Unit Tests).
- Exit criteria: protocol section merged; port + tests green; no adapter yet.
- Verification (CMake targets added, so configure first):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 2 — Backend v1 (assumes Q1/Q3/Q4 outcomes)

Scope: the smallest service that is honest about what it can verify.

- Endpoints: submit score, board page query, personal bests, health, and (if Q3=A) the
  Corresponding Source link required by AGPL section 13.
- **Server-side re-scoring:** recompute the total score from the submitted per-note verdict log
  under the declared ruleset version; mismatch → reject. This catches naive score edits and is
  the strongest check physically available. Store the full record blob plus derived columns.
- Identity verification per Q2 (signature check for keypair identity); invite-group scoping per
  Q5; ruleset-major board segmentation per Q6.
- The server never receives chart content, package files, or audio — schema has nowhere to put
  them, by construction.
- Testing: a submission harness driven by score records generated from plan 23's autoplay bot
  and DetectionEvent replay (deterministic, chart-content-free); if Q4=B, golden cross-validation
  fixtures proving the reimplemented validator matches the C++ scorer bit-for-bit.
- Exit criteria: deployed to the Q1 host; harness green against the live endpoint; deployment
  runbook written (repo `docs/`, host-agnostic where possible).
- Verification: if Q4=A the service builds via the standard helper
  (`.\.agents\rockhero-build.ps1 -Targets all` then `-RunTouchedTests`); if Q4=B its CI lives
  with the backend and is registered in docs/roadmap/00-roadmap.md — either way the game tree
  still passes the Phase 1 commands above.

### Phase 3 — Game client integration (assumes Phase 2 deployed)

Scope: wiring the port to the real world and to the player.

- HTTP adapter as a framework-adapter TU in `rock-hero-game/core/src/` using narrow `juce_core`
  networking (`juce::URL` / `WebInputStream`), per docs/design/architectural-principles.md
  ("Framework-Adapter Units", "Core JUCE Utility Use"). **Checkpoint: verify with
  juce-tracktion-expert before implementing** — WebInputStream TLS support, timeout behavior,
  and threading rules on Windows; all requests on a background thread, never the message thread.
- Offline-first submission queue: persisted per profile, retries with backoff, survives restarts;
  drains from plan 27's local score store (records upload unchanged). Submission is an explicit
  opt-in on the results screen (docs/roadmap/27-in-song-flow-results-profiles.md flow), with a
  remembered "always submit" preference.
- Board views on the results screen and library song list
  (docs/roadmap/26-game-startup-menus-library.md surfaces), navigable via the menu input layer.
- Settings via plan 27's IGameSettings: enabled flag (default OFF), server URL, identity key
  storage, display name.
- Testing: port-level tests against the fake adapter (queue semantics, retry, opt-in gating);
  adapter test against a local stub HTTP server (docs/design/architectural-principles.md
  § Adapter Tests). No CI test talks to the real backend.
- Exit criteria: end-to-end submit-and-view against the deployed backend, demonstrated with
  autoplay-bot records; zero network I/O when the feature is disabled (assert via adapter spy).
- Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
```

### Phase 4 — Integrity heuristics and operations (assumes Phase 3 shipped)

Scope: keep honest friends honest; flag, don't pretend to prove.

- Record-plausibility heuristics on the server, all flagging (never silent auto-ban at this
  scale): degenerate timing-delta distributions (all-zero deltas = replay bot), implausible
  confidence distributions, non-monotonic note positions, note-count/section sanity against the
  record's own declared counts, detection-engine and ruleset version allow-lists, per-identity
  rate limits.
- Modifier segregation surfaces: no-fail and speed-modified runs displayed per plan 24's
  labeling; boards default-filter to unmodified runs with a toggle.
- Moderation tools: hide/remove an entry, freeze an identity, delete-my-data on request (privacy
  stance: the server stores display name, identity key, and score records — nothing else; a
  deletion path is required and cheap at friends scale).
- Explicitly rejected here, permanently: binary attestation, kernel anti-cheat, audio uploads as
  proof. The client scores analog audio; these would add invasiveness without adding proof.
- Testing: heuristic unit tests over synthesized bad records (from plan 23's generators);
  moderation-action integration tests on the backend harness.
- Exit criteria: heuristics documented in the protocol section with their false-positive stance;
  moderation runbook written.
- Verification: same three commands as Phase 3 for any game-tree changes; backend CI otherwise.

## 10. Final acceptance phase

Per constraint (h), after the last code phase, as separate invocations from the repo root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Plus, unique to this plan: the backend harness (Phase 2) green against the deployed service, and
a manual end-to-end check that a fresh install with leaderboards disabled performs zero network
requests. If Q4=B, the backend's own CI must also be green; its location is registered in
docs/roadmap/00-roadmap.md.

## 11. Rollback/abort notes

- **Phase 0 may conclude "do not build."** That is a valid outcome (cost, licensing burden, or
  stability never converging); record it in docs/roadmap/00-roadmap.md and stop. Nothing upstream
  depends on this plan.
- **Client ships safely without a backend**: the feature is behind a default-OFF setting, and
  Phases 1–3 keep the port/fake split, so aborting after Phase 1 leaves inert, tested code with
  no user-visible surface — acceptable and cheap to delete.
- **Hosting migration**: keep the storage schema portable (SQLite/Postgres-compatible SQL, record
  blobs as opaque JSON) so a dead free tier (Q1=B risk) migrates to a VPS by dump/restore. Export
  the DB before any teardown; a frozen board can be served as a static archive if the service is
  retired.
- **Ruleset/detection instability regression after launch**: if a detection-engine release later
  violates G29-STABILITY, freeze affected boards (Q6 mechanism) rather than mixing incomparable
  scores; reopen the gate before resuming submissions.
- **Licensing risk**: if the Q3 stance proves wrong in practice, the immediate mitigation is
  taking the hosted service offline (removing the network trigger) while relicensing questions
  are resolved — the game itself remains unaffected.
