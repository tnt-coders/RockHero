export const meta = {
    name : 'rockhero-deep-review',
    description : 'Full-project code review + doc sync with adversarial verification',
    phases : [
        {
            title : 'Review',
            detail : 'subsystem finders: correctness, efficiency, simplicity, conformance'
        },
        {title : 'Verify', detail : 'adversarial refutation of every code finding'},
        {title : 'Docs', detail : 'doc-vs-code staleness sweep'},
        {title : 'DocVerify', detail : 'verify each staleness claim'},
    ],
}

const PREAMBLE =
    `You are reviewing RockHero, an early-stage C++23 guitar rhythm game (real guitar input, 3D note highway, VST support) in C:\\__git__\\RockHero. Two products (editor, game) over a shared common layer:
- rock-hero-common/{core,audio,ui}: shared headless domain / audio ports + Tracktion impl / shared UI (the 3D highway renderer lives in common/ui).
- rock-hero-editor/{app,core,ui}, rock-hero-game/{app,core,audio,ui}.
Hard rules (from CLAUDE.md + docs/design/): common must never depend on editor/game code; products never depend on each other; Tracktion headers isolated to rock-hero-common/audio implementation files; rock-hero-common/core may use only narrow juce_core utilities and stays headless/testable; platform #ifdefs minimized to provably-necessary seams; naming: types CamelCase, functions camelCase, locals/params/namespaces lower_case, members m_lower_case. Design docs are the source of truth: docs/design/architecture.md, architectural-principles.md, coding-conventions.md, documentation-conventions.md. Developer guide: docs/developer/. Plans: docs/plans/{roadmap,in-progress,todo}, registries in docs/tracking/.
Search with rg (Grep tool) and read focused ranges; read whole files only when needed for correctness judgments. Cite exact file:line (verify by reading, not guessing). Your final output is machine-consumed data, not prose for a human.`

const FIND_QUALITY = `Report ONLY findings that materially matter:
- bug: real correctness defects (wrong math, broken edge cases, races, lifetime/UB, wrong API use) with a concrete failure scenario.
- efficiency: real waste in hot paths (per-frame allocations, redundant recompute, O(n^2) on unbounded n, texture/state churn) — not micro-nits.
- simplify: genuine over-complexity — duplicated logic that should share one implementation, dead code, vestigial parameters, a design that got accreted through iterations and now has a clearly cleaner shape.
- conformance: violations of the layering/design rules above (verify against the actual design doc text before claiming).
- test-gap: an important behavior with no test where a headless test is feasible.
NO stylistic nitpicks, NO speculative "might be nice", NO renames for taste. Max 12 findings, ordered by importance. Every finding needs: exact file + line, what is wrong, why it matters (failure scenario or cost), and a concrete suggested fix. Set confidence honestly. In coverage_note say what you did NOT get to.`

const FINDINGS = {
    type : 'object',
    properties : {
        findings : {
            type : 'array',
            items : {
                type : 'object',
                properties : {
                    title : {type : 'string'},
                    file : {type : 'string'},
                    line : {type : 'number'},
                    category : {
                        type : 'string',
                        enum : [ 'bug', 'efficiency', 'simplify', 'conformance', 'test-gap' ]
                    },
                    severity : {type : 'string', enum : [ 'high', 'medium', 'low' ]},
                    confidence : {type : 'string', enum : [ 'high', 'medium', 'low' ]},
                    description : {type : 'string'},
                    evidence : {type : 'string'},
                    suggested_fix : {type : 'string'},
                },
                required : [
                    'title',
                    'file',
                    'line',
                    'category',
                    'severity',
                    'confidence',
                    'description',
                    'evidence',
                    'suggested_fix'
                ],
            },
        },
        coverage_note : {type : 'string'},
    },
    required : [ 'findings', 'coverage_note' ],
}

const VERDICT = {
    type : 'object',
    properties : {
        isReal : {type : 'boolean'},
        reason : {type : 'string'},
        severity_adjust : {type : 'string', enum : [ 'keep', 'raise', 'lower' ]},
        fix_ok : {type : 'boolean'},
        fix_note : {type : 'string'},
    },
    required : [ 'isReal', 'reason', 'severity_adjust', 'fix_ok', 'fix_note' ],
}

const DOCFIND = {
    type : 'object',
    properties : {
        stale : {
            type : 'array',
            items : {
                type : 'object',
                properties : {
                    doc_file : {type : 'string'},
                    doc_line : {type : 'number'},
                    claim : {type : 'string'},
                    reality : {type : 'string'},
                    evidence : {type : 'string'},
                    suggested_edit : {type : 'string'},
                    severity : {type : 'string', enum : [ 'high', 'medium', 'low' ]},
                },
                required : [
                    'doc_file',
                    'doc_line',
                    'claim',
                    'reality',
                    'evidence',
                    'suggested_edit',
                    'severity'
                ],
            },
        },
        gaps : {
            type : 'array',
            items : {
                type : 'object',
                properties : {
                    doc_file : {type : 'string'},
                    what_is_missing : {type : 'string'},
                    why_it_matters : {type : 'string'},
                },
                required : [ 'doc_file', 'what_is_missing', 'why_it_matters' ],
            },
        },
        coverage_note : {type : 'string'},
    },
    required : [ 'stale', 'gaps', 'coverage_note' ],
}

const DOCVERDICT = {
    type : 'object',
    properties : {
        isReal : {type : 'boolean'},
        reason : {type : 'string'},
    },
    required : [ 'isReal', 'reason' ],
}

const CODE_FINDERS =
    [
        {
            key : 'hw-core-math',
            prompt :
                `Deep-review the headless 3D-highway math in rock-hero-common/core: include/rock_hero/common/core/highway/*.h, src/highway/*.cpp, and tests/test_highway_*.cpp. This feeds both the game view and the editor 3D preview and went through many iterations. Check: projection/view-state building (highwayViewStateFor and friends), sustain prefix-max + highwayDisplayHoldEnds (span-extended holds for sustainless strums, onset-epsilon grouping), visible-range bracketing, tail sampling/taper, bend evaluation (recently changed linear->smoothstep), slide easing, vibrato/tremolo wobbles, camera math, hand-window (FHP) math, metrics. Hunt edge cases: empty inputs, zero-duration notes, unsorted or duplicate times, epsilon boundary behavior, mirrored/inverted display, string-count extremes (4..8). Judge whether tests actually pin the important invariants.`,
        },
        {
            key : 'hw-renderer-notes',
            prompt :
                `Deep-review the note-drawing path of rock-hero-common/ui/src/highway/highway_renderer.cpp (~3000 lines; the draw() note loop and its helpers). Focus: visible-note selection vs display_hold_ends; chord grouping via onset epsilon; repeat-box (box-only) logic; span-hold take-over pass (hold_cap_seconds, reverse scan); head anchor math (approaching rides onset, sounding pins at hit line, passed fades pinned at line via time_to_z(max(head_seconds, now)) — recently changed); passed fade + attack fade; slide state, bend lift + inversion, vibrato depth scaling (g_highway_vibrato_depth_semitones, recent); tail ribbon construction incl. open-string band stations, tip fade, adaptive sampling of modulated tails; brackets/fingering/mute markers/harmonics composited per head. Hunt: stale assumptions left by iteration layers, index/group mismatches, off-by-one in batching (flush_note_batches), fade/anchor interactions, degenerate chart data.`,
        },
        {
            key : 'hw-renderer-board',
            prompt :
                `Deep-review the board/furniture path of rock-hero-common/ui/src/highway/highway_renderer.cpp: board face + inlay skin, fret lines + sqrt-decay hit-flash, string grid + lane Y math + floor lift, measure/beat lines, glow posts, open-note hex bars, chord/arpeggio boxes (top-bar rule, scrunched lane-height marks), anticipation ring + rolling flip (g_flip_flat_lead_seconds), hand-window (FHP) lights: motion dim, morph dip, phantom fret line, settled-window behavior for ringing open tails (window sampled at tail_from), span margin rule. Also camera reset/update and rebuildBoardFace lifecycle on setViewState. Hunt: draw-order/translucency mistakes, per-frame recompute of static geometry, window-motion edge cases (back-to-back ramps, ramp exactly at hit line), iteration leftovers that no longer serve the final look.`,
        },
        {
            key : 'hw-renderer-arch',
            prompt :
                `Architecture/simplification review of the whole highway renderer stack in rock-hero-common/ui/src/highway/ (highway_renderer.cpp/h, highway_atlas.*, bgfx_handle.h, any text/glyph drawing) plus its public header in include/. Fresh-eyes questions: is the ~3000-line draw() decomposable into cleaner units without changing behavior; duplicated math that belongs in the tested common/core highway feature; per-frame allocations (vectors rebuilt每 frame, string building) and bgfx state churn (texture binds, transient buffer use) that matter at 60fps; dead constants/paths left by the many visual iterations; Impl member coherence; whether CPU-side vertex building is batched sensibly. Compare against docs/design/architectural-principles.md (Humble Object scope: does headless-testable math live in core, thin drawers in ui?). Recommend concrete extractions only where the payoff is real.`,
        },
        {
            key : 'hw-shaders-assets',
            prompt :
                `Review rock-hero-common/ui/shaders/ (all .sc/varying.def.sc), the shader compilation wiring (rg for rock_hero_add_compiled_shader in CMake), atlas channel-scheme usage (R tint / G highlight / B alpha) end-to-end from highway_atlas.cpp through the fragment shaders, texture sampler state, and the resource deploy path for both products (game app + editor preview shader/texture staging). Hunt: shader/vertex-layout mismatches, channel-scheme misuse, missing sRGB/premultiply assumptions, deploy staleness traps, fallback-path (procedural atlas) divergence from the reference-PNG path.`,
        },
        {
            key : 'editor-preview',
            prompt :
                `Deep-review the editor 3D preview integration in rock-hero-editor/ui (preview_surface.*, preview_window.*, anything composing the highway renderer into the editor): native child HWND embedding, WM_SETFOCUS bounce (render child must never hold keyboard focus), device suspend/resume lifecycle (bgfx cannot re-init in-process), vblank tick suspension on hide, resize handling, lost-child guard (m_reported_lost_child), view-state refresh flow from the editor controller (when does setViewState get called — every edit? every frame?), and interaction with docs/tracking/watch-items.md entries about the preview. Hunt: lifecycle races on window close/reopen, redundant view-state rebuilds, leaks of native resources, focus/input regressions.`,
        },
        {
            key : 'game-shell',
            prompt :
                `Review rock-hero-game/ui and rock-hero-game/app: sdl3_application.cpp, juce_message_pump.h, rock_hero_game.cpp (GameShell), frame loop + pacing, bgfx init/backend selection, diagnostics overlay, resources root composition, menu input wiring. Check against the GameShell watch item (composition should move to app/ per architectural-principles "UI Modules" — plan 21 Phase 6 will do it; note further accretion since). Hunt: event-loop mistakes (SDL event pump vs JUCE message pump interplay), shutdown ordering vs bgfx handle destruction, frame-pacing math, minimized/occluded behavior.`,
        },
        {
            key : 'game-core',
            prompt :
                `Review rock-hero-game/core fully (8.6k lines): scoring/ (note_verdict, score_math, scoring_ruleset, timing_window — pure hit-window math incl. speed-factor scaling), detection/ event types, library/ (scan engine, index store, package describer, album art), menu/song_select, session/gameplay_session, settings/, frame_clock/, input/menu_bindings, resources/. Plus tests. Hunt: hit-window math errors (speed-factor scaling both directions, inclusive bounds), library scan edge cases (dedup, missing dirs, corrupt index round-trip), settings persistence errors, headless-purity violations (any UI/audio dependency creeping into core).`,
        },
        {
            key : 'common-core-domain',
            prompt :
                `Review rock-hero-common/core domain code outside highway/: chart/ (chart.h types, chart_rules, grid_arithmetic — grid-native positions, bend points incl. 0.5 quarter curls, hand-shape spans), timeline/ (tempo map — musical<->seconds resolution, the forward-cursor vs plain resolver split that motivated the onset epsilon), song/, shared/ (logger, path handling incl. the UTF-16 log-path narrowing watch item). Plus their tests. Hunt: tempo-map resolution inconsistencies, grid arithmetic overflow/rounding, chart-rule edge cases, epsilon policy coherence across the codebase (g_highway_onset_match_epsilon vs any other epsilons — is there one policy or several ad-hoc ones?).`,
        },
        {
            key : 'common-core-package',
            prompt :
                `Review rock-hero-common/core package/session/serialization: package/ (.rock song package read/write, format versioning, chart identity), session/ (workspace persistence), JSON/ZIP handling through juce_core, result/error propagation patterns. Plus tests (test_rock_song_package etc.). Hunt: write-safety (partial writes, missing atomic rename), version-migration gaps, error swallowing, round-trip asymmetries, path/Unicode handling on Windows.`,
        },
        {
            key : 'common-audio',
            prompt :
                `Review rock-hero-common/audio (29k lines — prioritize by risk): engine/ ports + engine.cpp assembly + per-port TUs, src/tracktion/ adapter units (incl. plugin_dirty_tracking — gesture-gated plugin-state transactions), device settings, live input monitoring, playback clock. Verify the Tracktion isolation rule holds: NO tracktion/juce-audio headers leak into public headers of the audio library or into consumers. Check threading contracts at the audio boundary (message thread vs audio thread annotations), suspend/resume around state capture, and the test_engine.cpp suite's coverage honesty. Hunt: lifecycle bugs around edit/transport teardown, callbacks retained past owner death, blocking calls on the audio thread.`,
        },
        {
            key : 'editor-core-controller',
            prompt :
                `Review the editor controller layer in rock-hero-editor/core: src/controller/editor_controller.cpp (100+ commits of churn), editor_controller_impl.h, i_editor_controller.h, editor_view_state.h, and the handler split (project_handlers, tone_handlers, etc.). This is the biggest accretion hotspot in the repo. Assess: is the handler decomposition coherent or drifting; command/undo transaction patterns consistent; view-state publication (diffing? full rebuilds?) efficient; interface bloat on i_editor_controller (does the recording test double in tests/ track it sanely); dead workflow paths from superseded iterations. Hunt real bugs in undo/redo sequencing, selection/caret state, and dirty tracking. Note structural findings against docs/plans/todo/remaining-god-object-decomposition-plan.md if it names this area.`,
        },
        {
            key : 'editor-core-chart',
            prompt :
                `Review chart editing in rock-hero-editor/core: chart edit planning/normalization (planAdjustSustain, moveChartSelection, finalizePlan, normalizeSustainOverlaps, the numbered rule sections like 40-Q2-B and section-10 margin clamps), grid-native off-grid unification, span/hand-shape editing, tests (test_chart_editing.cpp and friends). Cross-check the watch item "Min-distance span exemption vs. 40-Q2-B same-string truncation" — is its claim still accurate? Hunt: normalization order dependencies, undo granularity bugs, span-sibling rules applied in one pass but not another, position arithmetic at grid boundaries.`,
        },
        {
            key : 'editor-core-import-tone',
            prompt :
                `Review rock-hero-editor/core import + tone subsystems: gp_song_importer (GuitarPro import — track/part mapping, tuning/capo/cent offset, bend/slide/technique translation), tone handlers, tone automation model. Plus tests (test_gp_song_importer, test_editor_controller_tone_automation). Hunt: import fidelity bugs (dropped techniques, wrong tick math, enharmonic/tuning errors), tone automation curve edge cases, resource lifetime on import failure.`,
        },
        {
            key : 'editor-ui-tab',
            prompt :
                `Review the editor 2D tab surface in rock-hero-editor/ui: tab_view.* (64 commits of churn), shared layout manifest, pinned bands scroll model, pointer/keyboard interaction grammar (Alt=mutate, Ctrl=precision, Shift=axis), marker/caret/cursor two-state model, and tests (test_tab_view.cpp). Hunt: paint-path allocations per frame, hit-test drift vs the layout manifest, interaction-grammar inconsistencies between lanes and tab, stale iteration leftovers. Verify the humble-object split: is logic that should be headless (in editor/core or common/core TabViewState) leaking into the JUCE component?`,
        },
        {
            key : 'editor-ui-shell',
            prompt :
                `Review the editor shell in rock-hero-editor/ui: editor_view.* (117 commits — top churn file in the repo), main_window composition, track_viewport.*, timeline views, tone_automation_lanes_view, command/keybinding wiring (ApplicationCommandManager, key-chord modifier names per platform convention — recent commit), plus tests (test_editor_view_timeline). Hunt: accretion (is editor_view becoming a god object; what belongs in editor/core), listener leaks/dangling component references, repaint storms (full-view repaints where a child rect suffices), layout math duplication with the manifest.`,
        },
        {
            key : 'conformance',
            prompt :
                `Project-wide conformance audit against docs/design/architectural-principles.md and CLAUDE.md rules. Mechanically verify with rg: (1) no #include of rock_hero/editor/... or rock_hero/game/... inside rock-hero-common/; (2) no editor<->game cross-includes; (3) tracktion/ or juce_audio headers appear only in rock-hero-common/audio src (not include/ public headers, not other libs); (4) juce usage inside rock-hero-common/core limited to juce_core utilities; (5) platform guards (#ifdef _WIN32, JUCE_WINDOWS, WIN32_LEAN...) — list every site and judge each against the "Minimize Platform-Specific Code" rule (confined to one seam with a why-comment?); (6) naming-convention spot check on newest files; (7) CMake target/alias shape (product::scope) consistency. Report each violation with file:line. Also check test placement conventions (test_*.cpp near code).`,
        },
        {
            key : 'session-changes',
            prompt :
                `Adversarial re-review of the four most recent code commits (this week's visual iteration): run git show for 7277e727 (pinned sounding note heads through sustain), cb85b995 (span-covered sustainless strums hold to span end + highwayDisplayHoldEnds + shared onset epsilon), 61b7fb09 (passed heads consumed at hit line via time_to_z(max(head_seconds, now))), 5dcedc8d (vibrato depth constant 0.35 + smoothstep bend easing). Assume they contain a bug and hunt for it: interaction of hold_cap_seconds with display_hold_ends in the visibility prefix-max (can a capped group keep notes visible too long or cut them early?); the take-over reverse scan when groups interleave with box-only repeats; smoothstep easing interacting with the tail's adaptive sampling and with bend points closer than the sample spacing; the consumed-at-line change interacting with slide/bend sampling frozen at hold end (head_y vs z mismatch?); vibrato depth interacting with the tail centerline (does the TAIL also scale by the new constant, or only the head — is the wobble amplitude now inconsistent between head and tail?). Read the current code, not just the diffs.`,
        },
        {
            key : 'goal-alignment',
            prompt :
                `Strategic sanity check: read docs/design/architecture.md fully, docs/plans/roadmap/00-roadmap.md fully, and skim the actual product state (what compiles into the editor exe vs game exe — read the two app/ main files and their shell composition; glance at the menu/session flow in game and the editor main window). Question: is the work actually converging on the stated two-track goal (playable game + charting editor for real guitar)? Produce findings ONLY for real drift signals: subsystems built beyond current need, roadmap gates marked one way while code says another, foundational gaps that every future plan will trip over, or effort visibly pooling in polish while a critical-path item stays open. Be specific and evidence-based; this is a judgment pass, not a vibe check.`,
        },
    ]

    const DOC_FINDERS = [
        {
            key : 'docs-architecture',
            prompt :
                `Doc-sync audit of docs/design/architecture.md against the current code. For every concrete claim (module lists, file paths, threading model, timing chain, render stack, fallback strategy, "Game View" section, line-number citations if any): verify it with rg/reads. Report stale claims with evidence and a suggested minimal edit that preserves the doc's voice. Also list genuinely missing coverage (major shipped subsystems the doc omits entirely — e.g. is the 3D highway/editor-preview render architecture reflected?).`,
        },
        {
            key : 'docs-principles-conventions',
            prompt :
                `Doc-sync audit of docs/design/architectural-principles.md, docs/design/coding-conventions.md, and docs/design/documentation-conventions.md against current code. For principles: do named patterns/sections (ports-and-adapters, Humble Object scope, Time Must Be a Dependency, Minimize Platform-Specific Code, Placement Procedure for New Files, Library Roles, UI Modules move-to-app triggers) still match how code is actually organized — and where code diverges, is the code or the doc wrong? For conventions docs: spot-check 10 newest headers/sources against the stated rules (const correctness, parameter passing, Doxygen block format, backslash commands, blank-line rules) and report systematic drift (not one-off typos).`,
        },
        {
            key : 'docs-developer-guide',
            prompt :
                `Doc-sync audit of docs/developer/ (index.md, area tours for the 2D timeline views + shared 3D highway + game side, pattern catalog with code exemplars, procedural checklists incl. their "silent steps" lists). Every file, class, function, and path the guide names must exist as described — verify each with rg. The 3D highway went through heavy visual iteration recently (span holds, consumed heads, FHP window motion, arpeggio boxes, vibrato/bend changes) — check the highway tour especially hard. Report stale references with suggested edits and list guide sections that new work has made incomplete (e.g. a checklist missing a new touchpoint).`,
        },
        {
            key : 'docs-plans',
            prompt :
                `Audit docs/plans/ lifecycle hygiene: (1) roadmap/00-roadmap.md — do statuses, gates, and decision records match reality (cross-check a sample of plan files' own status lines and the code)? (2) in-progress/ — is everything in there actually active work (check git log dates for the areas they cover); anything complete that should move out, anything stale that contradicts the code it describes? (3) completed plans referenced as moved (e.g. FHP window motion) — moved correctly? (4) todo/ — only flag entries whose factual premises are now provably false (cheap rg checks), do NOT deep-audit deferred plans (they are allowed to lag per the maintenance rules). Report per-file with evidence.`,
        },
        {
            key : 'docs-tracking',
            prompt :
                `Re-verify every entry in docs/tracking/watch-items.md and docs/tracking/backlog.md against the current code (the registries' own rule: a stale registry is worse than none). For each entry: do the cited files/functions/line refs still exist and behave as claimed; has any trigger already fired (check git log / code for the named conditions); are any items now resolved and due for the Retired section? Report per-entry: still-accurate / stale-details (with corrected details) / trigger-fired / resolved.`,
        },
    ]

phase('Review')
log(`Launching ${CODE_FINDERS.length} code finders + ${DOC_FINDERS.length} doc auditors`)

const findingKey = f => `${f.file}:${f.line}:${f.title}`.toLowerCase()

const verifyFinding = async (finding, sourceKey) => {
  const verdict = await agent(`${PREAMBLE}

A code reviewer reported this finding. Your job is to REFUTE it if you can — read the actual CURRENT code at and around the cited location (and any code it interacts with) before judging. IMPORTANT: a remediation pass has already run since this finding was written, so several issues in this batch are ALREADY FIXED; if the current code no longer has the problem, refute it. Also the cited file:line may have shifted — search for the described construct, don't trust the line number.
Finding (from reviewer "${sourceKey}"):
${JSON.stringify(finding, null, 2)}

Rules: isReal=true ONLY if you confirmed the problem still exists in the CURRENT code yourself. If the premise is wrong, the code changed, the issue is already fixed, the behavior is intentional per a comment/doc/test, or the impact is immaterial — refute it. For simplify/efficiency, isReal=true only if the improvement is material and clearly worth the churn. Judge the suggested fix too (fix_ok=false + fix_note if wrong or there is a better one). Default to refuted when uncertain.`,
    { label: `verify:${sourceKey}:${finding.title.slice(0, 40)}`, phase: 'Verify', schema: VERDICT, effort: 'medium' })
  return { ...finding, source: sourceKey, confirmed: verdict ? verdict.isReal : false, votes: verdict ? [verdict] : [] }
}

const codeResults = await pipeline(
  CODE_FINDERS,
  d => agent(`${PREAMBLE}

${d.prompt}

${FIND_QUALITY}`, { label: `find:${d.key}`, phase: 'Review', schema: FINDINGS }),
  async (result, d) => {
    if (!result) return null
    const capped = result.findings.slice(0, 12)
    if (result.findings.length > 12) log(`find:${d.key} returned ${result.findings.length} findings; verifying top 12`)
    const verified = await parallel(capped.map(f => () => verifyFinding(f, d.key)))
    return { key: d.key, coverage: result.coverage_note, findings: verified.filter(Boolean) }
  })

// Doc audit already completed separately (lean 5-auditor run wf_3943b178-2ed); this resume only
// finishes the credit-killed CODE verification and the session-changes adversarial pass.

const code = codeResults.filter(Boolean)
const seen = new Set()
const confirmedFindings = [] for (const r of code)
{
    for (const f of r.findings)
    {
        if (!f.confirmed)
            continue const k = findingKey(f)
            if (seen.has(k)) continue seen.add(k)
            confirmedFindings.push(f)
    }
}
const rejectedCount = code.reduce((n, r) => n + r.findings.filter(f => !f.confirmed).length, 0)
log(`Confirmed ${confirmedFindings.length} code findings (${rejectedCount} refuted)`)

return
{
    confirmed_code_findings: confirmedFindings,
        coverage_notes: {code: code.map(r => ({key : r.key, note : r.coverage}))},
        refuted_counts: {code: rejectedCount},
}