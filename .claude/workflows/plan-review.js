// Adversarial review of a plan document (mode 'draft') or of a just-completed plan phase
// (mode 'phase'), with three parallel read-only reviewers: template conformance,
// layering/constraints, and staleness verification against the current tree.
//
// This file is JavaScript, not C++. It was previously written against an API that does not
// exist — an `export default` entry point that the runtime never calls, `agent({name, prompt,
// tools})` instead of `agent(prompt, opts)`, `parallel()` handed promises instead of thunks, and
// `phases` as bare strings — so invoking it did nothing at all and returned undefined. It is now
// written against the real contract: the script BODY runs, with agent/parallel/phase/log/args as
// globals, structured output via the schema option, and phases as {title, detail} objects.
// (pre-commit's clang-format also used to claim .js and mangled these files; the hook is now
// restricted to c and c++ — do not widen it.)
//
// Invoke:
//   Workflow({ name: 'plan-review', args: { planPath: 'docs/plans/roadmap/40-chart-editing.md', mode: 'draft' } })
//   Workflow({ name: 'plan-review', args: { planPath: '...', mode: 'phase', phase: 5 } })
// Returns { verdict: 'approve' | 'revise' | 'reject', findings: [...] } sorted by severity.

export const meta = {
    name: 'plan-review',
    description: 'Adversarially review a drafted plan document, or a just-completed plan phase, with three read-only reviewers',
    whenToUse: 'Before signing off a new plan under docs/plans/roadmap, or right after implementing one phase of an existing plan.',
    phases: [
        { title: 'Review', detail: 'conformance, layering/constraints, staleness — in parallel' },
        { title: 'Merge', detail: 'rank findings and derive the verdict' },
    ],
}

const { planPath, mode, phase: phaseNumber } = args || {}
if (!planPath) {
    throw new Error('plan-review: args.planPath is required')
}
if (mode !== 'draft' && mode !== 'phase') {
    throw new Error("plan-review: args.mode must be 'draft' or 'phase'")
}
if (mode === 'phase' && typeof phaseNumber !== 'number') {
    throw new Error("plan-review: args.phase (number) is required when args.mode is 'phase'")
}

const FINDINGS = {
    type: 'object',
    properties: {
        findings: {
            type: 'array',
            items: {
                type: 'object',
                properties: {
                    severity: { type: 'string', enum: ['blocker', 'major', 'minor'] },
                    where: { type: 'string' },
                    problem: { type: 'string' },
                    evidence: { type: 'string' },
                    suggestion: { type: 'string' },
                },
                required: ['severity', 'where', 'problem', 'evidence', 'suggestion'],
            },
        },
    },
    required: ['findings'],
}

const target = mode === 'phase'
    ? `plan ${planPath}, phase ${phaseNumber} (just implemented on the current working tree)`
    : `drafted plan document ${planPath}`

const conformanceBody = mode === 'phase'
    ? `Read ${planPath} and locate phase ${phaseNumber}. For every exit criterion and verification command listed for that phase, check the current working tree for evidence the criterion is actually met (files exist, code and tests are present as described). Confirm the work stayed inside the phase's declared scope (files/modules touched) and flag anything the phase promised but did not deliver.`
    : `Read ${planPath} and verify it follows the 11-section docs/plans/roadmap template exactly, in order: (1) Status line with date and baseline "refactor @ <hash>"; (2) Goal; (3) Non-goals; (4) Constraints; (5) Current state inventory ending with the stamp "Verified against code on <date>, refactor @ <hash>"; (6) Dependencies; (7) Decisions already made; (8) Open questions for the user; (9) Phased implementation with scope, files touched, public-header impact, testing plan, exit criteria, and exact verification commands per phase; (10) Final acceptance phase; (11) Rollback/abort notes. Then resolve every cross-reference (repo-relative paths to other plans, design docs, and code files) and flag any path that does not exist on disk.`

const scope = mode === 'phase' ? `phase-${phaseNumber} work in the current tree` : "the plan's phases"

const REVIEWERS = [
    { id: 'conformance', body: conformanceBody },
    {
        id: 'layering-constraints',
        body: `Read the Constraints section of ${planPath} and docs/design/architectural-principles.md. Check ${scope} against every non-negotiable constraint stated there — especially: common never depends on editor or game code; editor and game never depend on each other; Tracktion headers stay isolated to rock-hero-common/audio implementation files; rock-hero-common/core stays headless (narrow juce_core only); public-header minimalism (ports-and-adapters); platform #ifdefs confined to one seam with a why-comment; all build/test/lint commands routed through .agents/rockhero-build.ps1. Flag every step or change that violates, or is silent on, an applicable constraint.`,
    },
    {
        id: 'staleness-verification',
        body: `Spot-check the factual claims in ${planPath} against the current tree using rg and focused file reads: every repo-relative path it names must exist; every type, function, CMake target, or settings key it references must exist as spelled; every "currently the code does X" claim must match the code. Prefer rg over full-file reads, and cite file:line evidence for each mismatch. Also flag any place the plan contradicts ITSELF — a superseded recommendation left reading as live guidance is as costly as a stale path.`,
    },
]

phase('Review')
log(`plan-review: ${REVIEWERS.length} reviewers on ${planPath} (mode=${mode}${mode === 'phase' ? `, phase=${phaseNumber}` : ''})`)

const results = await parallel(REVIEWERS.map(reviewer => () => agent(
    `You are the "${reviewer.id}" reviewer in an adversarial review of the ${target} in the RockHero repo (C:\\__MAIN__\\Coding\\__git__\\RockHero).

You are READ-ONLY: do not edit any file, and do not run builds — the invoking session owns the build directory. Be skeptical; your job is to find real defects, not to approve. Verify against files on disk, never from memory, and cite file:line for every claim.

${reviewer.body}

An empty findings array means this review dimension passed cleanly. Do not pad it: a minor finding you cannot evidence is worse than none.`,
    { label: `review:${reviewer.id}`, phase: 'Review', schema: FINDINGS, model: 'opus' })
    .then(result => ({ id: reviewer.id, findings: result ? result.findings : null }))))

phase('Merge')
const SEVERITY_RANK = { blocker: 0, major: 1, minor: 2 }
const findings = []
for (const result of results.filter(Boolean)) {
    if (result.findings === null) {
        // A dead reviewer is not a pass: say so loudly rather than reporting a clean dimension.
        findings.push({
            reviewer: result.id,
            severity: 'major',
            where: '(reviewer)',
            problem: `The ${result.id} reviewer produced no structured output.`,
            evidence: 'agent returned null (killed, or failed after retries)',
            suggestion: 'Re-run plan-review; treat this dimension as UNREVIEWED, not passed.',
        })
        continue
    }
    for (const f of result.findings) {
        findings.push({ ...f, reviewer: result.id })
    }
}

findings.sort((a, b) =>
    (SEVERITY_RANK[a.severity] ?? 3) - (SEVERITY_RANK[b.severity] ?? 3) ||
    a.reviewer.localeCompare(b.reviewer) ||
    String(a.where).localeCompare(String(b.where)))

const verdict = findings.some(f => f.severity === 'blocker')
    ? 'reject'
    : findings.some(f => f.severity === 'major') ? 'revise' : 'approve'

log(`plan-review: verdict=${verdict}, findings=${findings.length}`)
return { verdict, findings }
