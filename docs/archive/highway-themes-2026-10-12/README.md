# Highway theme archive, 2026-10-12

Every user theme sighted while the highway's theming and render looks were explored, kept here so
the commit that adds this folder holds both the renderer that drew them and the themes themselves.
The next commit removes the render looks and this folder; restore either from that commit's
parent when the intricate-themes plan is taken up.

Each directory is a `theme.json` in the user-theme format, inheriting the built-in `ice` theme.
Many state a `look` block (pixel, crt, dither, halftone, `theme_colors_only`) that only the
renderer of this commit understands. The private Classic theme is not here.

Families, by name:

- `sight-*`: single-color sightings of the hand-window and inlay roles.
- `theme-abyss`, `-ember`, `-ice-azure`, `-night-drive`, `-pastel-dusk`, `-phosphor`,
  `-smoked-glass`, `-steel`: whole-look color themes.
- `theme-signal`, `-ladder`, `-slate`, `-chalk`: the readability set (strings searched for
  perceptual and color-vision-deficiency distance; spans placed in the hue gaps the strings leave).
- `theme-retro-arcade*`: the arcade family — color variants (`-01` to `-35`, `-azure`, `-sky`,
  `-electric`, `-ice-pair`), lane-line strengths (`-lines*`), pixel looks (`-px*`), scanline
  sweeps (`-scan-*`), CRT sweeps (`-crt-*`), dither, and board-palette themes (`-namco-*`,
  `-galaga-crt`, `-pellet-crt`, `-g16-*`, the last snapping to a 16-color board palette).
- `theme-ice-dither2`, `theme-ice-halftone`, `theme-pastel-dusk-halftone`: the dither and
  halftone looks on existing color themes.
