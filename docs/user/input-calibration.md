\page user_input_calibration Input Calibration

Input calibration sets the pre-effects guitar input gain used by Rock Hero. It calibrates your audio
interface, not your guitar: Rock Hero's reference is **+12 dBu reads 0 dBFS**, the level most
interfaces and amp simulators are built around, so a hot pickup drives the rig harder, exactly as it
would a real amp.

Live input stays off until the selected input is calibrated, so every player hears their guitar at
the same level. The editor opens input calibration by itself the first time it sees an input
without a calibration. **Cancel** closes it for that input until the input changes or the editor
restarts; the **Calibrate** button under the signal chain's input meter opens it again at any
time. (Inside the calibration window, its own **Calibrate** button measures by playing.)

# Calibrating

1. Select the correct input device and input channel in the audio settings.
2. Find your interface in **Known Audio Devices** below. Set it up exactly as the table says,
   type its gain on the **Gain** slider of the calibration window, and click **Apply**, which saves
   the gain and closes the window. This is the most accurate way.
3. If your interface is not listed but its manual states its input level, work its gain out with
   the formula below and type that. Otherwise, measure it by playing (see **Automatic
   Calibration** below).

To check a gain before you apply it, set **Pickup type** to your pickups and strum hard: the
input meter's blue mark shows where a hard strum on those pickups peaks when the gain is right.
The mark is hidden while a measurement runs, so the measurement hears your normal playing.

# Recommended Method

Use your interface's published figure when it has one: the **Known Audio Devices** table below
lists the interfaces whose input level is known, marking each figure as a manufacturer's,
community-measured or estimated one, and the formula below gives the gain for any other interface
whose manual states the dBu level its instrument input reaches at 0 dBFS.

Use the automatic measurement when the interface publishes no figure. It listens to you play and
estimates the interface from your hardest playing, assuming how hard a typical guitar with your
kind of pickups strums (see **Pickup Types** below). Guitars differ, so the result is within about
6 dB; type your interface's gain from the table below for better.

If you own a multimeter, you can measure your interface exactly: play a 1 kHz sine at 0 dBFS out of
the interface, read its voltage across tip and sleeve, then loop it into the instrument input and
read the level. Measured figures are welcome additions to the list.

# Known Audio Devices {#known-audio-devices}

These are the interfaces whose input level is known, with the input setting each figure holds
for and the gain Rock Hero derives from it: set your interface up as its row says and type that
gain. Each model links to the source of its figure.

\include{doc} known_interfaces.md

# The Gain Formula

Interface figures hold when the hardware input gain is at minimum or `0.0 dB`, the guitar is
connected to the instrument or Hi-Z input, and any pad, boost, compressor, vintage mode, Air mode,
or operating-system input boost is off.

NAM calibration metadata, like most interface manuals, expresses an input's calibration as the dBu
level of a 1 kHz sine wave that reaches `0 dBFS` peak. Rock Hero's reference is `+12 dBu`, so the
gain is:

```text
Rock Hero gain = device level at 0 dBFS (dBu) - 12 dB
```

The Quad Cortex, for example, reaches `0 dBFS` at `+14.3 dBu`, so its gain is
`14.3 - 12 = +2.3 dB`, the figure Neural DSP itself gives for its plugins. Use the exact model and
generation: interface families reuse names, but their instrument input headroom can change between
generations.

Sources for the reference:

- [NAM calibration tutorial](https://neural-amp-modeler.readthedocs.io/en/stable/tutorials/calibration.html)
- [Neural DSP input calibration guidance](https://unity.neuraldsp.com/t/optimal-input-level-for-highest-accuracy-when-using-ndsp-plugins/11048)

## Automatic Calibration

1. Select the correct input device and input channel in the audio settings.
2. In the calibration window, set **Pickup type** to the pickups you will play; hovering the
   list describes the chosen kind (see also **Pickup Types** below).
3. Plug the guitar straight into the interface's instrument (Hi-Z) input, set to 1 MOhm where it
   has a choice, with no pedals in between. Turn the guitar's volume and tone all the way up and
   select one pickup, then click **Calibrate**.
4. Play as hard as you play in a song, on all strings, until the countdown ends. The measured gain
   fills in; click **Apply** to save it.
5. If the input clips, the window says so: lower the interface's input gain and calibrate again.
   If nothing is heard, check the input and click **Stop**.

Rock Hero waits for you to start, however long that takes, then listens for ten seconds from your
first strum and sets the gain from the loudest of your playing, ignoring a single stray spike.

## Pickup Types {#pickup-types}

The measurement assumes how hard a typical hard strum peaks on each kind of pickup. Pick the kind
you will play; finer differences (vintage against hot output, Strat against Tele) are smaller than
the spread every guitar has anyway.

\include{doc} pickup_types.md

The assumed peak is the voltage at the guitar's jack on a hard strum, which no pickup maker
publishes: their output figures in mV come from their own test rigs and read several times lower.

Two cases the list cannot cover well:

- **Actives modified to run on 18 V** can reach about twice the voltage and may calibrate up to
  6 dB low.
- **Acoustic-electric guitars** reach the jack through their own preamp and volume control, so no
  pickup type fits; type your interface's gain instead.

The sources and the arithmetic are in `docs/tracking/2026-10-01-hard-strum-peak-research.md` and
`docs/tracking/2026-10-01-pickup-type-output-research.md`.

## Best Results

Use the same guitar volume, pickup selection, cable, interface input, and Windows recording level
that will be used while playing. Recalibrate after changing the input device, input channel,
hardware gain, or operating-system recording level.
