# Parallel signal paths in a tone, and automatable tone volume and pan

Status: **DEFERRED. Requested 2026-10-01; nothing designed or built.** This records intent and the
facts a design must respect. Re-read the code and the design docs before designing from it.

## The user's requests

1. A tone can run its signal through **parallel paths** at the same time. For example, one sound
   panned hard left and another hard right, or two paths blended some other way. The blend must be
   **fully automatable**.
2. A tone's automation lanes cover only the parameters of the plugins inside it. A tone-level
   **volume** and **pan** should be automatable as well, and any other clearly global setting. The
   user floated an EQ, but leaned towards it being a plugin in the tone itself.

The two requests share one stage, the mix at a tone's output, so they are planned together.

## Facts a design must respect

- **A chain is a list today.** A tone's signal chain is an ordered list of plugins, and the
  `.tone` file stores it that way (plan 50). Parallel paths turn it into a small graph: a split,
  the paths, and a mix. That changes the tone model, both package readers, the shared validator,
  the format reference (`docs/developer/changing-the-package-format.md`) and the signal-chain view.
- **Tones are already parallel branches.** `buildToneRack` assembles one Tracktion `RackType` with
  a parallel branch per tone. Each branch ends in a `ToneBranchGainPlugin` whose automation curve
  carries the tone-switch schedule (`rock-hero-common/audio/src/tracktion/multi_tone_rack.h`). Paths
  inside a tone would nest inside that branch. Whether Tracktion can do that through a nested rack
  or through more rack wiring needs checking against the vendored source before any design.
- **The switch crossfade must stay its own stage.** It is ruled that a tone switch is a branch-gain
  crossfade between branches that stay loaded, and nothing else (`167a61a2`). A tone volume lane is
  a separate gain that combines with that crossfade. It must never write to the branch-gain curve.
- **A per-tone output level already exists.** This is the "Output" slider (`6c7d6935`). That
  decision is final and is not reopened here. Automating it gives the tone volume lane; it does not
  add a second level control. `gain-block-and-signal-chain-panel.md` records the parked
  alternative, a Gain block inside the chain.
- **Automation is musical.** Lanes store musical positions in `song.json`, and the Tracktion curve
  is derived from them (`docs/plans/completed/tone-parameter-automation-plan.md`). Volume, pan and
  path blends must use the same lane model rather than a second one.
- **The game consumes the same rack**, so anything a chart automates must play in the game too.

## Open questions

- Are path blend, pan and level separate built-in parameters, or is a path's pan simply the stereo
  version of its level? The answer decides how many built-in lanes a tone gains.
- Does tone pan mean a balance control on a stereo signal, or panning a mono signal? The live
  guitar input is one channel, so a path is probably mono until a stereo plugin widens it.
- How does the signal-chain view draw parallel paths in the space the single row uses today?
- Should a stereo tone be audible as stereo in the game's player monitor? This depends on the
  game's output stage (`setMonitorGain`).
- EQ: the user leans towards a plugin in the chain, which adds nothing to this plan. Confirm that
  before treating it as settled.
