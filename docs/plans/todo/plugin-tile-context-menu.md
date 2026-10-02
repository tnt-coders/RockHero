# Plugin tile context menu: everyday verbs first

Status: **DEFERRED. Requested 2026-10-01; nothing designed or built.** This records intent. Re-read
the code before designing from it.

## The user's request

Right-clicking a plugin in a tone's signal chain only changes its display type today. The user
thinks that menu would more naturally hold simpler, everyday verbs, such as **Replace plugin**.
Changing the display type must still be possible somehow.

## What exists

- Right-click on a tile opens `showDisplayTypeMenu`
  (`rock-hero-editor/ui/src/signal_chain/plugin_tile_view.cpp`). It offers "Use default (...)" and
  one item per display type, and it appears only while the display-type override is enabled.
- The controller's plugin verbs are insert, remove, move and open
  (`onSelectedPluginInsertRequested`, `onRemovePluginRequested`, `onMovePluginRequested`, `onOpenPluginRequested`). There is no replace
  verb.

## What the feature needs

1. **A replace verb.** It swaps one plugin for another at the same position, in a single undo
   entry. Whether that is its own engine operation or a remove plus an insert under one memento
   is a design question. Plan 50's transactional chain replace is the precedent to check first.
   What happens to the old plugin's automation lanes must also be decided: they cannot follow it
   to a different plugin.
2. **The menu.** The likely shape is Replace…, Open, Remove, then the display type as a submenu
   ("Display as ▸"). That keeps the type reachable without leading the menu. Each existing verb
   in the menu must go through the same controller intent as its current gesture, not a copy.
3. **Keymap.** Any new verb gets a row in the keymap matrix
   (`docs/plans/in-progress/keymap-matrix.md`), and the uniform-scope law applies.

## Open questions

- Should Replace open the plugin browser filtered to the old plugin's type, or unfiltered?
- Does Replace try to carry parameter values across for the same plugin family (a version
  upgrade), or always start from defaults?
