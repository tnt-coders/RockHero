\page user_highway_view_options Highway View Options

The editor's **View** menu chooses how the note highway looks and reads. The choices are yours, not
the song's: they never change a chart. The theme, the string colors and the **Highway Display**
switches are saved together and shared with the game, which uses your last choices the next time
it starts.

# Highway Theme

A theme sets the highway's colors and art. **View > Highway Theme** lists the themes Rock Hero
ships, then any of your own.

- **Default** is Rock Hero's standard look. Every other built-in theme starts from it.
- **Element - Earth, Fire, Water and Wind**, **Heaven**, **Hell**, **Blacklight** and
  **Night Drive** restyle the board around one mood each.
- **Retro Arcade** also shows the board as an old arcade monitor would, with scan lines and
  phosphor stripes, tuned so notes and numbers stay readable.
- **Colorblind - Protanopia, Deuteranopia, Tritanopia and Achromatopsia** each choose colors that
  stay apart for one kind of color vision deficiency.

Your own themes live in the `themes` folder of Rock Hero's user data folder. A theme that has an
error is left out of the list and the error is written to the log; if it was the one you had picked,
the highway uses **Default** instead.

# String Colors

**View > String Colors** sets the color of each string, which every note, lane and tail on that
string wears.

- **Theme's choice** uses the colors the current theme picks. Its name in brackets says which
  colors those are.
- Picking a named set below it keeps those string colors whatever theme you switch to.

# Highway Display

**View > Highway Display** holds four switches. A tick means the switch is on.

- **Invert Strings** puts the lowest-pitched string at the bottom of the highway, the way tablature
  is written. Off by default: the lowest string sits on top.
- **Note Targets** shows a hollow outline of each note where it will land on the hit line, which
  grows to full size as the note arrives. On by default.
- **Note Rotation** stands single notes upright as they approach and lays them flat as they land.
  With it off, notes travel flat the whole way. Chords never rotate either way. On by default.
- **Arpeggio Brackets** marks an arpeggio's hand shape with a panel where the shape starts and a
  bracket on each string it holds. With it off, the arpeggio still shows its rails. On by default.

# Tablature Strings

**View > Tablature Strings** sets how many string lanes the tablature and the 3D preview show.
**Match Chart** shows exactly the strings the chart uses. A larger count adds empty lanes, for
example to keep a 6-string chart lined up beside a 7-string one. A count smaller than the chart's
never hides notes: the chart's own strings always show.
