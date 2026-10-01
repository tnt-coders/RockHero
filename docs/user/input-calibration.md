\page user_input_calibration Input Calibration

Input calibration sets the pre-effects guitar input gain used by Rock Hero. It calibrates your audio
interface, not your guitar: Rock Hero's reference is **+12 dBu reads 0 dBFS**, the level most
interfaces and amp simulators are built around, so a hot pickup drives the rig harder, exactly as it
would a real amp.

Live input stays off until the selected input is calibrated, so every player hears their guitar at
the same level. The editor opens input calibration by itself the first time it sees an input
without a calibration. **Later** closes it for that input until the input changes or the editor
restarts; the Calibrate button under the input meter opens it again at any time.

# Calibrating

1. Select the correct input device and input channel in the audio settings.
2. In the calibration window, choose your interface from the **Interface** list. The gain fills in,
   and the window says how far to trust the figure and how to set the interface up for it: set the
   interface exactly that way, then click **Apply**.
3. If your interface is not listed, either type its gain on the **Gain** slider and click
   **Apply** (see the formula below), or click **Measure by playing**.

# Recommended Method

Use your interface's published figure when it has one: the **Interface** list carries the
interfaces whose input level is known, each marked as a manufacturer's, community-measured or
estimated figure, and the formula below gives the gain for any other interface whose manual states
the dBu level its instrument input reaches at 0 dBFS.

Use the automatic measurement when the interface publishes no figure. It listens to you play and
estimates the interface from your hardest playing, assuming how hard a typical guitar with your
kind of pickups strums: a humbucker (or a P-90 or an active pickup) about 2 V, a single coil about
1 V. Guitars differ, so the result is within about 6 dB; choose your interface from the list for
better.

If you own a multimeter, you can measure your interface exactly: play a 1 kHz sine at 0 dBFS out of
the interface, read its voltage across tip and sleeve, then loop it into the instrument input and
read the level. Measured figures are welcome additions to the list.

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
2. Open input calibration and set **Pickup** to the pickups you will play: **Humbucker**
   (also P-90 and active) or **Single-coil**.
3. Turn the guitar's volume and tone all the way up and select one pickup, then click
   **Measure by playing**.
4. Play as hard as you play in a song, on all strings, until the countdown ends.
5. Retry if the input clips or nothing is heard.

Rock Hero waits up to ten seconds for you to start, then listens for ten seconds from your first
strum and sets the gain from the loudest of your playing, ignoring a single stray spike.

## Best Results

Use the same guitar volume, pickup selection, cable, interface input, and Windows recording level
that will be used while playing. Recalibrate after changing the input device, input channel,
hardware gain, or operating-system recording level.
