---
name: rockhero-build
description: Use for RockHero Windows configure/build/test work from Codex.
---

# RockHero Build

Use the helper from the repo root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex\skills\rockhero-build\scripts\rockhero-build.ps1 -Targets rock_hero_editor_ui_tests -RunTouchedTests
```

Preset names are case-sensitive; the default is `Debug`, matching `CMakePresets.json`.

Add `-Configure` only after CMake graph changes or stale Ninja errors. Output is quiet on success;
add `-FullOutput` when diagnosing build details. The helper discovers CLion's bundled CMake and
runs Ninja through Visual Studio's developer environment.

Failure hints: missing standard headers means Ninja ran without `VsDevCmd.bat`; stale source paths
mean configure is needed; Conan/GitHub failures during configure usually require escalated network
access.
