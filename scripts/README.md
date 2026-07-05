# Repository Scripts

Agent- and developer-facing helper scripts that are not tied to any one tool or agent vendor.

## rockhero-build.ps1

Agent-safe configure/build/test helper for Windows. It uses CLion's bundled CMake for configure
(so the build graph stays exactly as the IDE generated it) and runs Ninja through Visual Studio's
developer environment, which is why agent builds do not break CLion's include paths. Run it from
the repo root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\rockhero-build.ps1 -Targets all -RunTouchedTests
```

- Preset names are case-sensitive and lowercase (`debug`, `release`, `relwithdebinfo`, matching
  `CMakePresets.json`); the default is `debug` and the build directory defaults to
  `build/<preset>`.
- `-Targets` takes any Ninja target, including `all` and the `clang-tidy` custom target.
- `-RunTouchedTests` discovers and runs every built `*_tests.exe` under the build directory
  (running the executables directly is much faster than `ctest`, which pays process startup per
  registered case).
- Add `-Configure` only after CMake graph changes or stale Ninja errors. Output is quiet on
  success; add `-FullOutput` when diagnosing build details.

Failure hints: missing standard headers means Ninja ran without `VsDevCmd.bat`; stale source paths
mean configure is needed; Conan/GitHub failures during configure usually require escalated network
access.
