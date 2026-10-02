\page user_input_calibration Input Calibration

Input calibration sets the pre-effects guitar input gain used by Rock Hero. It calibrates your audio
device, not your guitar: Rock Hero's reference is **\calibrationReference reads 0 dBFS**, the level
most interfaces and amp simulators are built around, so a hot pickup drives the rig harder, exactly
as it would a real amp.

Live input stays off until the selected input is calibrated, so every player hears their guitar at
the same level. The editor opens input calibration by itself the first time it sees an input
without a calibration. **Cancel** closes it for that input until the input changes or the editor
restarts; the **Calibrate** button under the signal chain's input meter opens it again at any
time.

# Calibrating

There are two ways to a gain. Typing your audio device's gain is the most accurate; measuring it by
playing works for any device but is an estimate.

1. Select the correct input device and input channel in the audio settings.
2. Find your device in **Known Audio Devices** below, set it up exactly as its row says, type its
   gain on the **Gain** slider of the calibration window, and click **Apply**, which saves the gain
   and closes the window.
3. If your device is not listed but its manual states its input level, work its gain out with
   **The Gain Formula** below and type that. Otherwise, measure it (see **Measuring by Playing**).

# Known Audio Devices {#known-audio-devices}

These are the devices whose input level is known, with the input setting each figure holds for and
the gain Rock Hero derives from it. Each model links to the source of its figure, and the last
column says whether that figure is the manufacturer's, community-measured or estimated.

\include{doc} known_interfaces.md

A figure holds only with the hardware input gain at minimum or `0.0 dB`, the guitar connected to
the instrument or Hi-Z input, and any pad, boost, compressor, vintage mode, Air mode or
operating-system input boost off.

# The Gain Formula

NAM calibration metadata, like most interface manuals, states an input's calibration as the dBu
level of a 1 kHz sine wave that reaches `0 dBFS` peak. Rock Hero's reference is
\calibrationReference, so the gain is that level less the reference:

**\calibrationGainFormula**

\calibrationGainExample Use the exact model and generation: device families reuse names, but their
instrument input headroom can change between generations.

If you own a multimeter, you can measure your device exactly: play a 1 kHz sine at 0 dBFS out of the
device, read its voltage across tip and sleeve, then loop it into the instrument input and read the
level. Measured figures are welcome additions to the list.

Sources for the reference:

- [NAM calibration tutorial](https://neural-amp-modeler.readthedocs.io/en/stable/tutorials/calibration.html)
- [Neural DSP input calibration guidance](https://unity.neuraldsp.com/t/optimal-input-level-for-highest-accuracy-when-using-ndsp-plugins/11048)

# Measuring by Playing

The measurement listens to you play and estimates your device from your hardest strums, assuming
how hard a typical guitar with your kind of pickups peaks (see **Pickup Types** below). Guitars
differ, so the result is within about 6 dB; type your device's gain from the table when it is
listed.

1. Select the correct input device and input channel in the audio settings.
2. In the calibration window, set **Pickup type** to the pickups you will play; hovering the list
   describes the chosen kind.
3. \guitarSetupInstruction Where the input offers a choice of impedance, set it to 1 MOhm. Then
   click **Measure**.
4. \hardStrumInstruction Keep strumming until the countdown ends. The measured gain fills in;
   click **Apply** to save it.
5. If the input clips, the window says so at once: lower the device's input gain and measure
   again. If nothing is heard, check the input and click **Stop**.

Rock Hero waits for your first strum, however long that takes, then listens for
\calibrationListenSeconds seconds and sets the gain from the peaks of your hardest strums, ignoring
the loudest \calibrationIgnoredPercent of moments.

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
  pickup type fits; type your device's gain instead.

The sources and the arithmetic are in `docs/tracking/2026-10-01-hard-strum-peak-research.md` and
`docs/tracking/2026-10-01-pickup-type-output-research.md`.

# Checking a Gain with the Mark

The meter in the calibration window previews your input at the gain on the slider, with a
**Peak target** mark where your hardest strums should peak. To check a gain before you apply it,
typed or measured, set **Pickup type** to your pickups and set the guitar up as for a measurement:
\guitarSetupInstruction Then play the way the mark assumes:

- **How hard:** \hardStrumInstruction Not harder than you ever play, and not single notes, which
  peak well below chords.
- **What to watch:** the meter shows the peak of each moment, so it jumps on every strum. Watch
  the top of those jumps, not where the meter sits on average: when the gain is right, your
  hardest strums peak at the mark, and the rest of your playing peaks below it.
- **Reading it:** if your hardest strums stop clearly short of the mark, the gain is too low; if
  they regularly pass it, too high. An occasional strum a little past it is fine: the measurement
  itself ignores the loudest \calibrationIgnoredPercent of moments.

The mark is hidden while a measurement runs, so you play the way you normally do rather than
aiming at it.

# Best Results

Use the same guitar volume, pickup selection, cable, device input, and Windows recording level that
will be used while playing. Recalibrate after changing the input device, input channel, hardware
gain, or operating-system recording level.
