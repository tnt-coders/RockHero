---
name: rockhero-build
description: Use when working in the RockHero repository on Windows and needing to configure, build, run tests, or diagnose CMake/Ninja/MSVC environment issues from Codex. Captures the known-good CLion-bundled CMake plus Visual Studio Developer Command Prompt workflow for build/debug.
---

# RockHero Build

## Purpose

Use this skill for RockHero build and verification work from Codex on Windows. The reliable local
route is CLion's bundled CMake for configure and Ninja launched through Visual Studio's developer
environment for compilation.

## Known-Good Workflow

Run commands from the repository root.

Configure with CLion's bundled CMake:

```powershell
& 'C:\Program Files\JetBrains\CLion 2025.3.2\bin\cmake\win\x64\bin\cmake.exe' --preset debug
```

Build selected targets through `VsDevCmd.bat`:

```powershell
cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ninja -C build/debug rock_hero_common_audio_tests rock_hero_editor_core_tests rock_hero_editor_ui_tests'
```

Run focused test executables directly after building them:

```powershell
& 'build/debug/rock-hero-common/audio/tests/rock_hero_common_audio_tests.exe'
& 'build/debug/rock-hero-editor/core/tests/rock_hero_editor_core_tests.exe'
& 'build/debug/rock-hero-editor/ui/tests/rock_hero_editor_ui_tests.exe'
```

## Helper Script

Prefer the bundled helper when quoting or tool discovery would slow the task down:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex\skills\rockhero-build\scripts\rockhero-build.ps1 -Configure -Targets rock_hero_common_audio_tests,rock_hero_editor_core_tests,rock_hero_editor_ui_tests -RunTouchedTests
```

The script discovers the known CLion CMake and Visual Studio developer-command paths, configures
when requested, builds the requested Ninja targets inside `VsDevCmd.bat`, and can run the common
touched test binaries.

## Failure Signals

- `Cannot open include file: 'algorithm'`, `'memory'`, or `'cstddef'` means Ninja was launched
  without the Visual Studio developer environment. Re-run through `VsDevCmd.bat`.
- Ninja referencing deleted or renamed source files means the CMake graph is stale. Re-run the
  CLion CMake configure command.
- Conan metadata or GitHub access failures during configure are usually sandbox/network failures.
  Re-run the same configure with escalated network access when configure is necessary.
- Plain `cmake`, `ctest`, or `ninja` in bare Codex PowerShell may be missing environment setup.
  Prefer the workflow above before falling back to compile-command one-offs.
