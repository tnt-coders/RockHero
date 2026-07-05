---
name: rockhero-build
description: Use for RockHero Windows configure/build/test work from Codex.
---

# RockHero Build

The helper lives in the agent-shared `.agents/` directory; this skill is a pointer so Codex
discovery keeps working. Usage, flags, and failure hints: see `.agents/README.md`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all -RunTouchedTests
```
