---
name: rockhero-build
description: Use for RockHero Windows configure/build/test work from Codex.
---

# RockHero Build

The helper lives in the agent-neutral `scripts/` directory; this skill is a pointer so Codex
discovery keeps working. Usage, flags, and failure hints: see `scripts/README.md`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\rockhero-build.ps1 -Targets all -RunTouchedTests
```
