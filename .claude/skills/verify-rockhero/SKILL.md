---
name: verify-rockhero
description: Run the sanctioned end-of-plan verification bundle for RockHero — full build, touched tests, clang-tidy, and pre-commit as four separate invocations. Use at a plan's final acceptance phase or before committing a coherent batch of code changes.
argument-hint: "[preset]"
---

# verify-rockhero

Run the one sanctioned verification bundle (CLAUDE.md "Agent-Run Builds"; constraint (h) in every
docs/plans/roadmap/*.md Constraints section). This bundle is for **end-of-plan acceptance** or a coherent
finished batch of edits — intermediate phases run only the single check their change determinately
warrants, never this whole bundle reflexively.

## Preset

The optional argument is the CMake preset: `debug` (default), `release`, or `relwithdebinfo`.
Preset names are case-sensitive and lowercase. Substitute it for `<preset>` below; when the
argument is omitted, use `debug` (and you may drop `-Preset` entirely — `debug` is the helper's
default).

## Steps — four SEPARATE invocations, in this order, from the repo root

Never combine them into one invocation, and never use raw `cmake`, `ninja`, or `ctest` — the
helper at `.agents/rockhero-build.ps1` is the only sanctioned entry point (usage in
`.agents/README.md`).

1. Full build:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Preset <preset> -Targets all
   ```

2. All built test executables:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Preset <preset> -RunTouchedTests
   ```

3. Static analysis:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Preset <preset> -Targets clang-tidy
   ```

4. Formatting and lint hooks:

   ```powershell
   pre-commit run --all-files
   ```

## Rules

- **`-Configure`**: add it to step 1 only when CMake graph inputs changed (CMakeLists.txt, preset
  files, conanfile.txt, target source lists) or a previous run failed with stale-Ninja/stale
  source-path errors. Never pass it by default.
- **On any failure: STOP.** Do not run the remaining steps. Re-run only the failing helper
  invocation with `-FullOutput` appended and show the full output (the helper is quiet on
  success). For a `pre-commit` failure, show its complete output as-is. Fix, then restart the
  bundle from the failed step.
- Step 4 may rewrite files (clang-format/cmake-format). If it reports files were modified, list
  the modified files, then run `pre-commit run --all-files` once more — it must pass clean.

## Report

Finish with a per-step result summary, for example:

```text
1. -Targets all          PASS
2. -RunTouchedTests      PASS (N test executables)
3. -Targets clang-tidy   PASS
4. pre-commit --all-files PASS
```

On failure, the summary shows which step failed and the steps that were skipped as a result.
