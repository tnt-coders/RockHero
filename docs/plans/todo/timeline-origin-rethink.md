# Timeline Origin Rethink

Status: deferred. The measure-one timeline origin was implemented and then reverted on 2026-07-05
after hands-on review; rethink the design before trying again.

## What was tried

The visible timeline started at the first tempo anchor (measure 1 beat 1) instead of absolute
zero: pre-anchor lead-in audio was unreachable, Stop reset to the anchor, and restored cursors
clamped to it.

## Why it was reverted

- The transport time readout still showed the anchor's absolute seconds (10.001 for the test
  song), so the "left edge is time zero" story was only half true.
- Selecting measure 1 at the hard left edge felt awkward: no room to overshoot, and edge targets
  fight the window border.

## Ideas for the rethink

- Show musical time (0 at measure 1) in the readout while keeping absolute transport seconds
  internal, so the display matches the origin story.
- Keep a small fixed margin of dead space left of measure 1 so the first beat is clickable
  without pixel-perfect aim, rather than hard-clamping the scroll at the anchor.
- Decide what lead-in audio means: hidden entirely, dimmed, or collapsible.
