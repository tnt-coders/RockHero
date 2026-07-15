# Test Coverage Badge Plan

## Goal

Add a real test coverage badge to `README.md` that reflects project-owned C++ coverage from
GitHub Actions.

GitHub does not calculate test coverage on its own. The badge needs a CI job that builds with
coverage instrumentation, runs tests, generates a machine-readable report, uploads that report to a
coverage service, and links the service-provided badge from the README.

## Recommended Approach

Use a dedicated Linux coverage workflow with:

- GCC or Clang coverage instrumentation.
- `gcovr` for coverage report generation.
- Codecov for hosting coverage history, pull request annotations, and the README badge.

Keep this separate from the normal build workflow. The existing build should continue proving the
normal supported build matrix, while the coverage workflow optimizes for stable coverage reporting.

## Coverage Scope

Coverage should include project-owned code only:

- `apps/`
- `libs/`

Coverage should exclude:

- `external/`
- `build/`
- Conan caches and generated Conan files.
- Generated files.
- Test source files.
- Third-party code brought in through Tracktion, JUCE, Catch2, or Conan packages.

This keeps the badge meaningful. Including vendored or generated code would make the number noisy
and would not reflect Rock Hero's actual test quality.

## Suggested Workflow Shape

Create `.github/workflows/coverage.yml`.

The workflow should:

1. Check out submodules recursively.
2. Install Linux dependencies needed by JUCE/Tracktion.
3. Install Python and `gcovr`.
4. Configure a dedicated coverage build directory.
5. Build the project.
6. Run tests with `ctest`.
7. Generate Cobertura XML with `gcovr`.
8. Upload the report with `codecov/codecov-action`.

Example outline:

```yaml
name: Coverage

on:
  push:
    branches: [master]
  pull_request:
    branches: [master]

permissions:
  contents: read
  id-token: write

jobs:
  coverage:
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - uses: actions/setup-python@v5
        with:
          python-version: '3.x'

      - name: Install coverage tools
        run: python -m pip install gcovr

      - name: Configure coverage build
        run: |
          cmake -S . -B build/coverage -G Ninja \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_COMPILER=gcc \
            -DCMAKE_CXX_COMPILER=g++ \
            -DCMAKE_C_FLAGS="--coverage -O0 -g" \
            -DCMAKE_CXX_FLAGS="--coverage -O0 -g" \
            -DCMAKE_EXE_LINKER_FLAGS="--coverage"

      - name: Build
        run: cmake --build build/coverage

      - name: Test
        run: ctest --test-dir build/coverage --output-on-failure

      - name: Generate coverage report
        run: |
          gcovr -r . \
            --object-directory build/coverage \
            --filter 'libs/' \
            --filter 'apps/' \
            --exclude 'external/.*' \
            --exclude 'build/.*' \
            --exclude '.*tests.*' \
            --cobertura-pretty -o coverage.xml \
            --html-details coverage.html

      - name: Upload coverage
        uses: codecov/codecov-action@v5
        with:
          files: coverage.xml
          flags: cxx
          fail_ci_if_error: true
          use_oidc: true
```

The actual workflow should reuse the repository's existing JUCE dependency installation approach
where practical. The current main workflow delegates C++ build setup to reusable workflows under
`tnt-coders/ci-workflows`, so coverage may be best implemented either by extending that reusable
workflow set or by copying the required setup steps into this repository's coverage workflow.

## README Badge

After the workflow uploads coverage successfully, add a badge near the existing build badge:

```md
[![Coverage](https://codecov.io/gh/tnt-coders/RockHero/branch/master/graph/badge.svg)](https://codecov.io/gh/tnt-coders/RockHero)
```

If Codecov flags are used for separate components later, consider adding flag-specific badges such
as `core`, `audio`, or `ui`. Start with one project-level badge unless component-level coverage
becomes useful for review.

## Policy Decisions

- Do not fail pull requests on a strict percentage threshold at first.
- Prefer upload failures to fail the coverage job so badge/report issues are visible.
- Revisit thresholds after the project has stable gameplay, audio, and UI test coverage.
- Keep integration tests in the coverage run only if they are stable on Linux CI.
- If Tracktion/JUCE integration makes full-project Linux coverage too fragile, start with core and
  headless tests only, then expand coverage scope as CI stabilizes.

## Acceptance Criteria

- A dedicated coverage workflow runs on pushes and pull requests targeting `master`.
- The workflow builds with coverage instrumentation and runs the intended test set.
- `gcovr` generates `coverage.xml`.
- Codecov receives the report and shows project coverage.
- `README.md` contains a working coverage badge.
- Coverage excludes third-party, generated, build, and test code.

