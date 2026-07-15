export const meta = {
    name : "plan-review",
    description :
        "Adversarially review a drafted docs/plans/roadmap document (mode 'draft') or a just-completed " +
            "plan phase (mode 'phase') with three parallel read-only reviewers: template " +
            "conformance, layering/constraints, and staleness verification against the current tree.",
    phases : [ "fan-out-reviewers", "merge-findings" ],
};

// Args: { planPath: string, mode: "draft" | "phase", phase?: number (required for "phase") }.
// Returns { verdict: "approve" | "revise" | "reject", findings: [...] } sorted by severity.
// Uses only the sanctioned workflow primitives: parallel, agent, log. Deterministic — no
// Date.now, no Math.random.

const READ_ONLY_TOOLS = [ "Read", "Grep", "Glob", "Bash" ];
const SEVERITY_RANK = {
    blocker : 0,
    major : 1,
    minor : 2
};

const FINDINGS_SCHEMA =
    "Return ONLY a JSON object of this exact shape, with no prose outside it:\n" +
    '{ "findings": [ { "severity": "blocker" | "major" | "minor",\n' +
    '    "where": "<plan section, repo-relative path, or file:line>",\n' +
    '    "problem": "<one-sentence statement of the defect>",\n' +
    '    "evidence": "<file:line citation or rg output proving it>",\n' +
    '    "suggestion": "<concrete fix>" } ] }\n' +
    "An empty findings array means this review dimension passed cleanly.";

function reviewerPrompts(planPath, mode, phase)
{
    const target =
        mode === "phase"
            ? `plan ${planPath}, phase ${phase} (just implemented on the current working tree)`
            : `drafted plan document ${planPath}`;
    const conformance =
        mode === "phase"
            ? `Read ${planPath} and locate phase ${
                  phase}. For every exit criterion and verification ` +
                  "command listed for that phase, check the current working tree for evidence the " +
                  "criterion is actually met (files exist, code and tests are present as described). " +
                  "Confirm the work stayed inside the phase's declared scope (files/modules touched) " +
                  "and flag anything the phase promised but did not deliver."
            : `Read ${
                  planPath} and verify it follows the 11-section docs/plans/roadmap template exactly, ` +
                  'in order: (1) Status line with date and baseline "refactor @ <hash>"; (2) Goal; ' +
                  "(3) Non-goals; (4) Constraints; (5) Current state inventory ending with the stamp " +
                  '"Verified against code on <date>, refactor @ <hash>"; (6) Dependencies; (7) Decisions ' +
                  "already made; (8) Open questions for the user; (9) Phased implementation with scope, " +
                  "files touched, public-header impact, testing plan, exit criteria, and exact " +
                  "verification commands per phase; (10) Final acceptance phase; (11) Rollback/abort " +
                  "notes. Then resolve every cross-reference (repo-relative paths to other plans, " +
                  "design docs, and code files) and flag any path that does not exist on disk.";
    const scope = mode === "phase" ? `phase-${phase} work in the current tree` : "plan's phases";
    return [
        {id : "conformance", body : conformance},
        {
            id : "layering-constraints",
            body :
                `Read the Constraints section of ${planPath} and ` +
                    `docs/design/architectural-principles.md. Check the ${scope} against every ` +
                    "non-negotiable constraint stated there — especially: common never depends on " +
                    "editor or game code; editor and game never depend on each other; Tracktion " +
                    "headers stay isolated to rock-hero-common/audio implementation files; " +
                    "public-header minimalism (ports-and-adapters); all build/test/lint commands " +
                    "routed through .agents/rockhero-build.ps1; and the naming firewall (the " +
                    'commercial real-guitar game is never named — only "RS" or neutral phrasing). ' +
                    "Flag every step or change that violates, or is silent on, an applicable " +
                    "constraint.",
        },
        {
            id : "staleness-verification",
            body :
                `Spot-check the factual claims in ${planPath} against the current tree using ` +
                    "rg and focused file reads: every repo-relative path it names must exist; every " +
                    "type, function, CMake target, or settings key it references must exist as " +
                    'spelled; every "currently the code does X" claim must match the code. Prefer ' +
                    '"rg -n" over full-file reads, and cite file:line evidence for each mismatch.',
        },
    ].map(({id, body}) => ({
              id,
              prompt :
                  `You are the "${id}" reviewer in an adversarial review of the ${target} in the ` +
                      "RockHero repo. Be skeptical — your job is to find real defects, not to approve. " +
                      "Verify against files on disk, never from memory.\n\n" + body + "\n\n" +
                      FINDINGS_SCHEMA,
          }));
}

function parseFindings(raw, reviewer)
{
    let parsed = raw;
    if (typeof raw === "string")
    {
        try
        {
            parsed = JSON.parse(raw.slice(raw.indexOf("{"), raw.lastIndexOf("}") + 1));
        }
        catch
        {
            parsed = null;
        }
    }
    if (!parsed || !Array.isArray(parsed.findings))
    {
        return [ {
            reviewer,
            severity : "major",
            where : "(reviewer output)",
            problem : `The ${reviewer} reviewer did not return the required findings JSON.`,
            evidence : String(raw).slice(0, 400),
            suggestion : "Re-run plan-review; treat this dimension as unreviewed, not passed."
        } ];
    }
    return parsed.findings.map(
        (f) => ({
            reviewer,
            severity : SEVERITY_RANK[f.severity] !== undefined ? f.severity : "minor",
            where : f.where ?? "",
            problem : f.problem ?? "",
            evidence : f.evidence ?? "",
            suggestion : f.suggestion ?? "",
        }));
}

export default async function planReview({args, agent, parallel, log})
{
    const {planPath, mode, phase} = args ?? {};
    if (!planPath)
        throw new Error("plan-review: args.planPath is required");
    if (mode !== "draft" && mode !== "phase")
        throw new Error('plan-review: args.mode must be "draft" or "phase"');
    if (mode === "phase" && typeof phase !== "number")
        throw new Error('plan-review: args.phase (number) is required when mode is "phase"');

    const reviewers = reviewerPrompts(planPath, mode, phase);
    log(`plan-review: fanning out ${reviewers.length} reviewers on ${planPath} (mode=${mode}` +
        (mode === "phase" ? `, phase=${phase})` : ")"));
    const results = await parallel(
        reviewers.map((r) => agent({name : r.id, prompt : r.prompt, tools : READ_ONLY_TOOLS})));

    const findings =
        results.flatMap((raw, i) => parseFindings(raw, reviewers[i].id))
            .sort(
                (a, b) => (SEVERITY_RANK[a.severity] ?? 3) - (SEVERITY_RANK[b.severity] ?? 3) ||
                          a.reviewer.localeCompare(b.reviewer) ||
                          String(a.where).localeCompare(String(b.where)));
    const verdict = findings.some((f) => f.severity === "blocker") ? "reject"
                    : findings.some((f) => f.severity === "major") ? "revise"
                                                                   : "approve";
    log(`plan-review: verdict=${verdict}, findings=${findings.length}`);
    return {verdict, findings};
}
