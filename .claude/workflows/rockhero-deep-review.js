// Full-project code review + doc sync with adversarial verification.
//
// This file is JavaScript, not C++. It was silently corrupted once by pre-commit's clang-format
// hook, whose upstream `types_or` claims javascript: statements were joined until the script was
// syntactically invalid (`const x = [] for (...)`) and it had been dead for some time before
// anyone noticed. .pre-commit-config.yaml now restricts that hook to c and c++; do not widen it.
//
// Invoke: Workflow({ name: 'rockhero-deep-review' })
//   or with a narrower target: Workflow({ name: 'rockhero-deep-review', args: { since: '1 week ago' } })
// args.since (optional) scopes the recent-changes finder; everything else always runs.

export const meta = {
    name: 'rockhero-deep-review',
    description: 'Full-project code review + doc sync with adversarial verification',
    phases: [
        { title: 'Review', detail: 'subsystem finders: correctness, efficiency, simplicity, conformance' },
        { title: 'Verify', detail: 'adversarial refutation of every code finding' },
        { title: 'Docs', detail: 'doc-vs-code staleness sweep' },
        { title: 'DocVerify', detail: 'verify each staleness claim' },
    ],
}

const SINCE = (args && args.since) || '2 weeks ago'

const PREAMBLE = `You are reviewing RockHero, an early-stage C++23 guitar rhythm game (real guitar input, 3D note highway, VST support) at C:\\__MAIN__\\Coding\\__git__\\RockHero. Two products (editor, game) over a shared common layer:
- rock-hero-common/{core,audio,ui}: shared headless domain / audio ports + Tracktion impl / shared UI (the 3D highway renderer and the 2D tab paint core both live in common/ui).
- rock-hero-editor/{app,core,ui}, rock-hero-game/{app,core,audio,ui}.

YOU ARE READ-ONLY. Do NOT edit any file. Do NOT run builds, ninja, cmake, or .agents/rockhero-build.ps1 — the invoking session owns the build directory and a second build corrupts it. Read, Grep, Glob and read-only git (log/show/diff) only.

Hard rules (CLAUDE.md + docs/design/): common never depends on editor/game code; products never depend on each other; Tracktion headers isolated to rock-hero-common/audio implementation files; rock-hero-common/core may use only narrow juce_core utilities and stays headless/testable; platform #ifdefs minimized to provably-necessary seams; naming: types CamelCase, functions camelCase, locals/params/namespaces lower_case, private members m_lower_case, globals g_lower_case. Design docs are the source of truth: docs/design/architecture.md, architectural-principles.md, coding-conventions.md, documentation-conventions.md. Developer guide: docs/developer/. Plans: docs/plans/{roadmap,in-progress,todo}, registries in docs/tracking/.

Project laws worth judging designs against: make illegal states unrepresentable; ONE source of truth (a "desyncable encoding" — the same fact maintained in two places that can drift — is a defect, not a style issue); plan/apply editing with exact undo; refuse-never-clamp for edits; derived-over-authored; no code that lies about intent.

Search with rg (Grep) and read focused ranges; read whole files when a correctness judgment needs it. Cite exact file:line, verified by reading — never guessed. Your final output is machine-consumed data, not prose for a human.`

const FIND_QUALITY = `Report ONLY findings that materially matter:
- bug: real correctness defects (wrong math, broken edge cases, races, lifetime/UB, wrong API use) with a concrete failure scenario.
- efficiency: real waste in hot paths (per-frame allocations, redundant recompute, O(n^2) on unbounded n, texture/state churn) — not micro-nits.
- simplify: genuine over-complexity — duplicated logic that should share one implementation, two concepts that should be one (or one that should be two), dead code, vestigial parameters, a design accreted through iterations that now has a clearly cleaner shape. Ask explicitly: if this were rebuilt from nothing today, would it look like this?
- conformance: violations of the layering/design rules above (verify against the actual design doc text before claiming).
- test-gap: an important behavior with no test where a headless test is feasible.
NO stylistic nitpicks, NO speculative "might be nice", NO renames for taste. Max 12 findings, ordered by importance. Every finding needs: exact file + line, what is wrong, why it matters (failure scenario or cost), and a concrete suggested fix. Set confidence honestly, and set needs_user_decision when the fix would change a settled product decision rather than repair an accident. In coverage_note say what you did NOT get to.`

const FINDINGS = {
    type: 'object',
    properties: {
        findings: {
            type: 'array',
            items: {
                type: 'object',
                properties: {
                    title: { type: 'string' },
                    file: { type: 'string' },
                    line: { type: 'number' },
                    category: { type: 'string', enum: ['bug', 'efficiency', 'simplify', 'conformance', 'test-gap'] },
                    severity: { type: 'string', enum: ['high', 'medium', 'low'] },
                    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
                    description: { type: 'string' },
                    evidence: { type: 'string' },
                    suggested_fix: { type: 'string' },
                    needs_user_decision: { type: 'boolean' },
                },
                required: ['title', 'file', 'line', 'category', 'severity', 'confidence', 'description', 'evidence', 'suggested_fix', 'needs_user_decision'],
            },
        },
        coverage_note: { type: 'string' },
    },
    required: ['findings', 'coverage_note'],
}

const VERDICT = {
    type: 'object',
    properties: {
        isReal: { type: 'boolean' },
        reason: { type: 'string' },
        severity_adjust: { type: 'string', enum: ['keep', 'raise', 'lower'] },
        fix_ok: { type: 'boolean' },
        fix_note: { type: 'string' },
    },
    required: ['isReal', 'reason', 'severity_adjust', 'fix_ok', 'fix_note'],
}

const DOCFIND = {
    type: 'object',
    properties: {
        stale: {
            type: 'array',
            items: {
                type: 'object',
                properties: {
                    doc_file: { type: 'string' },
                    doc_line: { type: 'number' },
                    claim: { type: 'string' },
                    reality: { type: 'string' },
                    evidence: { type: 'string' },
                    suggested_edit: { type: 'string' },
                    severity: { type: 'string', enum: ['high', 'medium', 'low'] },
                },
                required: ['doc_file', 'doc_line', 'claim', 'reality', 'evidence', 'suggested_edit', 'severity'],
            },
        },
        gaps: {
            type: 'array',
            items: {
                type: 'object',
                properties: {
                    doc_file: { type: 'string' },
                    what_is_missing: { type: 'string' },
                    why_it_matters: { type: 'string' },
                },
                required: ['doc_file', 'what_is_missing', 'why_it_matters'],
            },
        },
        coverage_note: { type: 'string' },
    },
    required: ['stale', 'gaps', 'coverage_note'],
}

const DOCVERDICT = {
    type: 'object',
    properties: {
        isReal: { type: 'boolean' },
        reason: { type: 'string' },
    },
    required: ['isReal', 'reason'],
}

const CODE_FINDERS = [
    {
        key: 'hw-core-math',
        prompt: `Deep-review the headless 3D-highway math in rock-hero-common/core: include/rock_hero/common/core/highway/*.h, src/highway/*.cpp, and tests/test_highway_*.cpp. This feeds both the game view and the editor 3D preview and went through many iterations. Check: projection/view-state building, sustain prefix-max + highwayDisplayHoldEnds (span-extended holds for sustainless strums, onset-epsilon grouping), visible-range bracketing, tail sampling/taper, bend evaluation, slide easing, vibrato/tremolo wobbles, camera math, hand-window (FHP) math, metrics. Hunt edge cases: empty inputs, zero-duration notes, unsorted or duplicate times, epsilon boundary behavior, mirrored/inverted display, string-count extremes (4..8). Judge whether tests pin the important invariants.`,
    },
    {
        key: 'hw-renderer-notes',
        prompt: `Deep-review the note-drawing path of rock-hero-common/ui/src/highway/highway_renderer.cpp (the draw() note loop and its helpers). Focus: visible-note selection vs display_hold_ends; chord grouping via onset epsilon; repeat-box logic; span-hold take-over pass; head anchor math; passed fade + attack fade; slide state, bend lift + inversion, vibrato depth scaling; tail ribbon construction incl. open-string band stations, tip fade, adaptive sampling of modulated tails; brackets/fingering/mute markers/harmonics composited per head. Hunt: stale assumptions left by iteration layers, index/group mismatches, off-by-one in batching, fade/anchor interactions, degenerate chart data.`,
    },
    {
        key: 'hw-renderer-board',
        prompt: `Deep-review the board/furniture path of rock-hero-common/ui/src/highway/highway_renderer.cpp: board face + inlay skin, fret lines + hit-flash, string grid + lane Y math + floor lift, measure/beat lines, glow posts, open-note bars, chord/arpeggio boxes, the capo pass (dead-zone dim + rimmed clamp bar), anticipation ring + rolling flip, hand-window (FHP) lights: motion dim, morph dip, phantom fret line. Also camera reset/update and rebuildBoardFace lifecycle on setViewState. Hunt: draw-order/translucency mistakes, per-frame recompute of geometry that changes only with setViewState, window-motion edge cases, iteration leftovers that no longer serve the final look.`,
    },
    {
        key: 'hw-renderer-arch',
        prompt: `Architecture/simplification review of the whole highway renderer stack in rock-hero-common/ui/src/highway/ plus its public header. Fresh-eyes questions: is the very large draw() decomposable into cleaner units without changing behavior; duplicated math that belongs in the tested common/core highway feature; per-frame allocations and bgfx state churn that matter at 60fps; dead constants/paths left by visual iterations; Impl member coherence; whether CPU-side vertex building is batched sensibly. Compare against docs/design/architectural-principles.md (Humble Object scope: headless-testable math in core, thin drawers in ui). Recommend extractions only where the payoff is real.`,
    },
    {
        key: 'tab-paint',
        prompt: `Deep-review the shared 2D notation rasterizer rock-hero-common/ui/src/tab/tab_paint_core.cpp and tab_lane_layout.*, plus tests/test_tab_paint_core.cpp. This is the one juce_graphics-bearing public header in common/ui and both products must produce identical notation pixels from it. Check: head shape/layer stack, mute icons and plates, the legato mark, sustain ribbon + tremolo band, slide diagonals and linked continuation heads (including unpitched/scrape junctions), bend polyline + chips, arpeggio brackets, FHP markers, the capo chip, deferred label-chip drawing. Hunt: per-paint allocations on a scrolling lane, geometry constants duplicated with the highway atlas or the layout manifest, clip-region correctness for partial repaints, drawing-order regressions, and any editor-only chrome that has leaked into the shared core.`,
    },
    {
        key: 'game-shell',
        prompt: `Review rock-hero-game/ui and rock-hero-game/app: sdl3_application.cpp, juce_message_pump.h, rock_hero_game.cpp (GameShell), frame loop + pacing, bgfx init/backend selection, diagnostics overlay, resources root composition, menu input wiring. Check against the GameShell watch item (composition should move to app/ per architectural-principles "UI Modules"). Hunt: event-loop mistakes (SDL event pump vs JUCE message pump interplay), shutdown ordering vs bgfx handle destruction, frame-pacing math, minimized/occluded behavior.`,
    },
    {
        key: 'game-core',
        prompt: `Review rock-hero-game/core fully: scoring/ (note_verdict, score_math, scoring_ruleset, timing_window), detection/ event types, library/ (scan engine, index store, package describer, album art), menu/song_select, session/gameplay_session, settings/, frame_clock/, input/menu_bindings, resources/, plus tests. Hunt: hit-window math errors (speed-factor scaling both directions, inclusive bounds), library scan edge cases (dedup, missing dirs, corrupt index round-trip), settings persistence errors, headless-purity violations (UI/audio dependencies creeping into core).`,
    },
    {
        key: 'common-core-chart',
        prompt: `Review rock-hero-common/core chart code: chart.h types and accessors (fretFor, releasedFret, fretHandHarmonic, savedChartNote, nodeIsOnNeck), chart_rules (the technique-matrix rule authority validateChartNotes plus validateChartRules), grid_arithmetic (grid-native positions, the minimum-sustain-distance margin, g_minimum_kept_sustain_beats, chartEffectiveSustains, predecessorHoldReaches), chart_document serialization, and their tests. Hunt: rule-order masking inside validateChartNotes, boundary arithmetic (fret 0, fret == capo, node == stop, node == cap), accessor sets that overlap enough for a caller to pick the wrong one, round-trip asymmetries between reader and writer, and whether the tests pin the RULES or merely the current code.`,
    },
    {
        key: 'common-core-timeline',
        prompt: `Review rock-hero-common/core timeline/ and song/ and shared/: the tempo map (musical<->seconds resolution, the forward-cursor vs plain resolver split), Fraction arithmetic and overflow, song/arrangement types, logger and path handling. Plus tests. Hunt: tempo-map resolution inconsistencies between the two resolver paths, Fraction overflow or precision loss on realistic song-scale values, signature-change boundary behavior, and epsilon policy coherence across the codebase (is there ONE simultaneity policy, or several ad-hoc ones?).`,
    },
    {
        key: 'common-core-package',
        prompt: `Review rock-hero-common/core package/session/serialization: package/ (.rock song package read/write, format versioning, chart identity), session/ (workspace persistence), JSON/ZIP handling through juce_core, result/error propagation. Plus tests. Hunt: write-safety (partial writes, missing atomic rename), error swallowing, round-trip asymmetries, path/Unicode handling on Windows. Note that the project has a strict no-legacy rule — formats change in place and version numbers are never bumped — so flag migration/back-compat code as dead weight rather than as missing.`,
    },
    {
        key: 'common-audio',
        prompt: `Review rock-hero-common/audio (large — prioritize by risk): engine/ ports + engine.cpp assembly + per-port TUs, src/tracktion/ adapter units (incl. plugin dirty tracking / gesture-gated plugin-state transactions), device settings, live input monitoring, playback clock. Verify the Tracktion isolation rule holds: NO tracktion or juce-audio headers leak into public headers of the audio library or into consumers. Check threading contracts at the audio boundary (message thread vs audio thread annotations), suspend/resume around state capture, and whether the test suite's coverage is honest. Hunt: lifecycle bugs around edit/transport teardown, callbacks retained past owner death, blocking calls on the audio thread.`,
    },
    {
        key: 'editor-core-controller',
        prompt: `Review the editor controller layer in rock-hero-editor/core: src/controller/editor_controller.cpp, editor_controller_impl.h, i_editor_controller.h, editor_view_state.h, and the handler split. This is the biggest accretion hotspot in the repo. Assess: is the handler decomposition coherent or drifting; are command/undo transaction patterns consistent; is view-state publication efficient; interface bloat on i_editor_controller and whether the recording test double tracks it sanely; dead workflow paths from superseded iterations. Hunt real bugs in undo/redo sequencing, selection/caret/marker state, the multi-digit fret entry window, and dirty tracking.`,
    },
    {
        key: 'editor-core-chart',
        prompt: `Review chart editing in rock-hero-editor/core/src/chart/: the plan/apply planners and their shared finalize gate (sort, sustain-overlap normalization, legato repair, whole-matrix validation of the saved form, refuse), legato_normalize, pick_slide_defaults, chart_selection, chart_hit_testing, and tests. Judge the gate architecture from scratch: is copying the whole note stream per plan the right shape; is the step order inside finalize correct and each step idempotent; can the normalizers fight each other; is the legato repair a fixpoint; are the per-verb eligible-subset skips consistent with what validation would refuse.`,
    },
    {
        key: 'editor-core-import-tone',
        prompt: `Review rock-hero-editor/core import + tone subsystems: gp_song_importer and gp_chart_builder (Guitar Pro import — track/part mapping, tuning/capo/cent offset, capo-relative to absolute fret shifting, bend/slide/technique translation, sustain normalization rules, fret-hand-position generation), tone handlers, tone automation model, plus tests. Hunt: import fidelity bugs (dropped techniques, wrong tick math, tuning errors), any place a capo-shifted and an unshifted fret are compared, clamp/floor ordering mistakes, and resource lifetime on import failure.`,
    },
    {
        key: 'editor-ui-tab',
        prompt: `Review the editor 2D tab surface in rock-hero-editor/ui: tab_view.*, the shared layout manifest, pinned bands scroll model, pointer/keyboard interaction grammar (Alt=author, Ctrl=precision, Shift=extend), the marker/caret/cursor state model, and tests. Hunt: paint-path allocations per frame, hit-test drift vs the layout manifest, interaction-grammar inconsistencies between lanes and tab, stale iteration leftovers. Verify the humble-object split: is logic that should be headless (editor/core, or the shared TabViewState) leaking into the JUCE component?`,
    },
    {
        key: 'editor-ui-shell',
        prompt: `Review the editor shell in rock-hero-editor/ui: editor_view.*, main_window composition, track_viewport.*, timeline views, tone automation lanes, command/keybinding wiring, the 3D preview surface embedding (native child window, focus bounce, device suspend/resume, vblank ticks), plus tests. Hunt: accretion (is editor_view a god object; what belongs in editor/core), listener leaks or dangling component references, repaint storms where a child rect would do, layout math duplicated with the manifest, preview lifecycle races on window close/reopen.`,
    },
    {
        key: 'conformance',
        prompt: `Project-wide conformance audit against docs/design/architectural-principles.md and CLAUDE.md. Mechanically verify with rg: (1) no include of rock_hero/editor/... or rock_hero/game/... inside rock-hero-common/; (2) no editor<->game cross-includes; (3) tracktion/ or juce_audio headers only in rock-hero-common/audio src, never public headers or other libs; (4) juce usage inside rock-hero-common/core limited to juce_core; (5) every platform guard (#ifdef _WIN32, JUCE_WINDOWS, ...) listed and judged against "Minimize Platform-Specific Code" (confined to one seam with a why-comment?); (6) naming conventions on the newest files; (7) CMake target/alias shape (product::scope) consistency; (8) test placement conventions. Report each violation with file:line.`,
    },
    {
        key: 'ci-blind-spots',
        prompt: `Audit for the CI blind spots CLAUDE.md enumerates, since local MSVC/debug cannot reproduce them and each costs a full pipeline round trip. Scan the whole tree (prioritize files changed in the last month via git log --since="1 month ago" --name-only): floating-point == or != including a DEFAULTED operator== on any float-bearing struct; aggregate/designated initializers omitting non-DMI fields; locals or parameters shadowing an INHERITED base member; anonymous-namespace or static helpers with no remaining caller; std::optional dereferences with no has_value guard on the path (including after a Catch2 REQUIRE, which the checker cannot see through); reads of a variable after std::move, especially two arguments of the SAME call; and framework calls whose meaning differs per OS (PNG decode pixel format, popup-menu modifier tests). Report every hit with file:line — this is the list CI fails on, so be exhaustive rather than selective.`,
    },
    {
        key: 'recent-changes',
        prompt: `Adversarial re-review of the most recent work. Run git log --oneline --since="${SINCE}" and git show on the substantive commits to see what changed, then read the CURRENT code (not just the diffs) and assume the changes contain a bug. Prioritize: newly introduced shared helpers and their call sites; any rule expressed in more than one place; boundary arithmetic added late; anything whose tests were edited in the same commit as the behavior (a test changed to match new output can hide a regression); and interactions between changes made on different days that were never exercised together. Name concrete failure scenarios.`,
    },
    {
        key: 'goal-alignment',
        prompt: `Strategic sanity check: read docs/design/architecture.md fully, docs/plans/roadmap/00-roadmap.md fully, and skim the actual product state (the two app/ main files and their shell composition; the game menu/session flow; the editor main window). Question: is the work converging on the stated two-track goal (playable game + charting editor for real guitar)? Produce findings ONLY for real drift signals: subsystems built beyond current need, roadmap gates marked one way while code says another, foundational gaps every future plan will trip over, or effort pooling in polish while a critical-path item stays open. Evidence-based; this is a judgment pass, not a vibe check.`,
    },
]

const DOC_FINDERS = [
    {
        key: 'docs-architecture',
        prompt: `Doc-sync audit of docs/design/architecture.md against the current code. For every concrete claim (module lists, file paths, threading model, timing chain, render stack, fallback strategy, line-number citations): verify with rg/reads. Report stale claims with evidence and a minimal edit that preserves the doc's voice. Also list genuinely missing coverage — major shipped subsystems the doc omits entirely.`,
    },
    {
        key: 'docs-principles-conventions',
        prompt: `Doc-sync audit of docs/design/architectural-principles.md, coding-conventions.md, and documentation-conventions.md against current code. For principles: do the named patterns and sections still match how code is actually organized — and where code diverges, is the code or the doc wrong? For the convention docs: spot-check the ten newest headers/sources against the stated rules (const correctness, parameter passing, Doxygen block format, backslash commands, blank-line rules) and report systematic drift, not one-off typos.`,
    },
    {
        key: 'docs-developer-guide',
        prompt: `Doc-sync audit of docs/developer/ (index, area tours, the pattern catalog with code exemplars, the procedural checklists and their "silent steps" lists, file-formats.md). Every file, class, function, and path the guide names must exist as described — verify each with rg. Report stale references with suggested edits, and list guide sections that new work has made incomplete (a checklist missing a new touchpoint, a pattern whose cited exemplar has moved or changed shape).`,
    },
    {
        key: 'docs-plans',
        prompt: `Audit docs/plans/ lifecycle hygiene: (1) roadmap/00-roadmap.md — do statuses, gates and decision records match reality (cross-check a sample of plan files' own status lines and the code)? (2) in-progress/ — is everything there actually active; anything complete that should move out; anything that contradicts the code it describes, or ITSELF (a superseded recommendation left reading as live)? (3) plans referenced as moved — moved correctly? (4) todo/ — only flag entries whose factual premises are now provably false via cheap rg checks; deferred plans are ALLOWED to lag per the maintenance rules, so do not deep-audit them. Report per-file with evidence.`,
    },
    {
        key: 'docs-tracking',
        prompt: `Re-verify every entry in docs/tracking/watch-items.md and docs/tracking/backlog.md against the current code (the registries' own rule: a stale registry is worse than none). For each entry: do the cited files/functions/lines still exist and behave as claimed; has any trigger already fired; are any items now resolved and due for retirement? Report per-entry: still-accurate / stale-details (with corrected details) / trigger-fired / resolved.`,
    },
]

const findingKey = f => `${f.file}:${f.line}:${f.title}`.toLowerCase()

phase('Review')
log(`Launching ${CODE_FINDERS.length} code finders (recent scope: ${SINCE}) + ${DOC_FINDERS.length} doc auditors`)

const codeResults = await pipeline(
    CODE_FINDERS,
    d => agent(`${PREAMBLE}\n\n${d.prompt}\n\n${FIND_QUALITY}`,
        { label: `find:${d.key}`, phase: 'Review', schema: FINDINGS, model: 'opus' }),
    async (result, d) => {
        if (!result) return null
        const capped = result.findings.slice(0, 12)
        if (result.findings.length > 12) log(`find:${d.key} returned ${result.findings.length} findings; verifying top 12`)
        const verified = await parallel(capped.map(f => () => agent(`${PREAMBLE}

A code reviewer reported this finding. Your job is to REFUTE it if you honestly can — read the actual CURRENT code at and around the cited location, and any code it interacts with, before judging. The cited file:line may have shifted, so search for the described construct rather than trusting the number.

Finding (from reviewer "${d.key}"):
${JSON.stringify(f, null, 2)}

Rules: isReal=true ONLY if you confirmed the problem still exists in the CURRENT code yourself. Refute it if the premise is wrong, the behavior is intentional per a comment/doc/test you actually read, the issue is already fixed, or the impact is immaterial. For simplify/efficiency, isReal=true only if the improvement is material AND clearly worth the churn. Default to refuted when uncertain. Judge the suggested fix separately (fix_ok=false plus fix_note if it is wrong or there is a better one).`,
            { label: `verify:${d.key}:${f.title.slice(0, 40)}`, phase: 'Verify', schema: VERDICT, effort: 'medium' })
            .then(v => ({ ...f, source: d.key, confirmed: v ? v.isReal : false, verdict: v || null }))))
        return { key: d.key, coverage: result.coverage_note, findings: verified.filter(Boolean) }
    })

phase('Docs')
const docResults = await pipeline(
    DOC_FINDERS,
    d => agent(`${PREAMBLE}\n\n${d.prompt}\n\nReport only claims you VERIFIED against code. A doc that lags a deferred plan is not a finding; a doc that contradicts shipped code, or contradicts itself, is.`,
        { label: `docs:${d.key}`, phase: 'Docs', schema: DOCFIND, model: 'opus' }),
    async (result, d) => {
        if (!result) return null
        const verified = await parallel(result.stale.slice(0, 15).map(s => () => agent(`${PREAMBLE}

A doc auditor claims the following documentation is stale. REFUTE it if you can: read the doc line AND the code it describes, and judge whether the claim is really wrong today.

Claim (from auditor "${d.key}"):
${JSON.stringify(s, null, 2)}

isReal=true only if the doc is genuinely wrong about the current code. Refute if the doc is describing intent rather than implementation, if the code matches after all, or if the "staleness" is a deferred plan allowed to lag.`,
            { label: `docverify:${d.key}:${s.doc_file.split(/[\\/]/).pop()}`, phase: 'DocVerify', schema: DOCVERDICT, effort: 'medium' })
            .then(v => ({ ...s, source: d.key, confirmed: v ? v.isReal : false, verdict: v || null }))))
        return { key: d.key, coverage: result.coverage_note, stale: verified.filter(Boolean), gaps: result.gaps }
    })

const code = codeResults.filter(Boolean)
const docs = docResults.filter(Boolean)

const seen = new Set()
const confirmedFindings = []
for (const area of code) {
    for (const f of area.findings) {
        if (!f.confirmed) continue
        const k = findingKey(f)
        if (seen.has(k)) continue
        seen.add(k)
        confirmedFindings.push(f)
    }
}
const refutedCount = code.reduce((n, area) => n + area.findings.filter(f => !f.confirmed).length, 0)

const confirmedStale = []
for (const area of docs) {
    for (const s of area.stale) {
        if (s.confirmed) confirmedStale.push(s)
    }
}
const refutedStaleCount = docs.reduce((n, area) => n + area.stale.filter(s => !s.confirmed).length, 0)

log(`Confirmed ${confirmedFindings.length} code findings (${refutedCount} refuted); ${confirmedStale.length} stale doc claims (${refutedStaleCount} refuted)`)

return {
    confirmed_code_findings: confirmedFindings,
    confirmed_stale_docs: confirmedStale,
    doc_gaps: docs.flatMap(area => area.gaps.map(g => ({ ...g, source: area.key }))),
    refuted_counts: { code: refutedCount, docs: refutedStaleCount },
    coverage_notes: {
        code: code.map(area => ({ key: area.key, note: area.coverage })),
        docs: docs.map(area => ({ key: area.key, note: area.coverage })),
    },
}
