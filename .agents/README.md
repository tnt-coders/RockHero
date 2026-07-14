# Agent Helpers

Helper scripts for coding agents (any vendor). Human contributors do not need anything in
this folder: IDE builds go through the CMake presets in `CMakePresets.json`.

## rockhero-build.ps1

Agent-safe configure/build/test helper for Windows. It uses CLion's bundled CMake for configure
(so the build graph stays exactly as the IDE generated it) and runs Ninja through Visual Studio's
developer environment, which is why agent builds do not break CLion's include paths. Run it from
the repo root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all -RunTouchedTests
```

- Preset names are case-sensitive and lowercase (`debug`, `release`, `relwithdebinfo`, matching
  `CMakePresets.json`); the default is `debug` and the build directory defaults to
  `build/<preset>`.
- `-Targets` takes any Ninja target, including `all` and the `clang-tidy` custom target.
- `-RunTouchedTests` discovers every built `*_tests.exe` under the build directory and runs the
  ones whose executable changed since it last passed under this helper (each pass stamps the exe
  timestamp into `<build dir>/.agents-test-stamps.json`). An unchanged binary means none of the
  code it tests relinked, so those suites are skipped; running the executables directly is also
  much faster than `ctest`, which pays process startup per registered case.
- Add `-Configure` only after CMake graph changes or stale Ninja errors. Output is quiet on
  success; add `-FullOutput` when diagnosing build details.

Failure hints: missing standard headers means Ninja ran without `VsDevCmd.bat`; stale source paths
mean configure is needed; Conan/GitHub failures during configure usually require escalated network
access.
