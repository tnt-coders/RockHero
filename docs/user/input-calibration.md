\page user_input_calibration Input Calibration

Input calibration sets the pre-effects guitar input gain used by Rock Hero. It calibrates your audio
interface, not your guitar: Rock Hero's reference is **+12 dBu reads 0 dBFS**, the level most
interfaces and amp simulators are built around, so a hot pickup drives the rig harder, exactly as it
would a real amp.

Live input stays off until the selected input is calibrated, so every player hears their guitar at
the same level. The editor opens input calibration by itself the first time it sees an input
without a calibration. **Later** closes it for that input until the input changes or the editor
restarts; the Calibrate button under the input meter opens it again at any time.

# Recommended Method

Use your interface's published figure when it has one: the table below gives the gain for the
interfaces whose input level is known, and the formula gives it for any other interface whose
manual states the dBu level its instrument input reaches at 0 dBFS.

Use the automatic measurement when the interface publishes no figure. It listens to you play and
estimates the reference from your hardest playing, so it is only as good as that playing is close
to a typical guitar's: about 6 dB either way across passive pickups, active pickups land hot, and
repeated runs agree to about 2 dB (these last figures are estimates awaiting measurement).

If you own a multimeter, you can measure your interface exactly: play a 1 kHz sine at 0 dBFS out of
the interface, read its voltage across tip and sleeve, then loop it into the instrument input and
read the level. Measured figures are welcome additions to the table.

# Known Unity-Gain Device Settings

These settings are starting points for manual calibration when the hardware input gain is at
minimum or `0.0 dB`, the guitar is connected to the instrument or Hi-Z input, and any pad, boost,
compressor, vintage mode, Air mode, or operating-system input boost is off unless the row says
otherwise.

NAM calibration metadata, like most interface manuals, expresses an input's calibration as the dBu
level of a 1 kHz sine wave that reaches `0 dBFS` peak. Rock Hero's reference is `+12 dBu`, so the
gain is:

```text
Rock Hero gain = device level at 0 dBFS (dBu) - 12 dB
```

The Quad Cortex, for example, reaches `0 dBFS` at `+14.3 dBu`, so its gain is
`14.3 - 12 = +2.3 dB`, the figure Neural DSP itself gives for its plugins.

Use the exact model and generation. Interface families reuse names, but their instrument input
headroom can change between generations. Rows based on manufacturer maximum-input specifications
are higher confidence than rows inferred from measured dBFS behavior.

| Device | Unity input mode | Device level at 0 dBFS | 1V peak sine reads | Rock Hero gain | Basis |
|--------|------------------|------------------------|--------------------|----------------|-------|
| Focusrite Scarlett Solo 3rd Gen | Instrument input, minimum gain | +12.5 dBu | -13.3 dBFS | +0.5 dB | Manufacturer spec |
| Focusrite Scarlett 2i2 3rd Gen | Instrument input, minimum gain | +12.5 dBu | -13.3 dBFS | +0.5 dB | Manufacturer spec |
| Focusrite Scarlett Solo 4th Gen | Instrument input, minimum gain | +12.0 dBu | -12.8 dBFS | 0.0 dB | Manufacturer spec |
| Focusrite Scarlett 2i2 4th Gen | Instrument input, minimum gain | +12.0 dBu | -12.8 dBFS | 0.0 dB | Manufacturer spec |
| MOTU M2, M4, M6 | Combo TRS guitar input, minimum gain | +16.0 dBu | -16.8 dBFS | +4.0 dB | Manufacturer spec |
| Neural DSP Quad Cortex | Instrument input, 1 MOhm, 0.0 dB input level | +14.3 dBu | -15.1 dBFS | +2.3 dB | Inferred; not published in public specs |
| Neural DSP Quad Cortex mini | Input 1 or input 2 TRS, 1 MOhm, minimum gain | +14.5 dBu | -15.3 dBFS | +2.5 dB | Inferred; not published in public specs |
| Neural DSP Nano Cortex | Instrument or capture input, minimum gain | +10.0 dBu | -10.8 dBFS | -2.0 dB | Inferred; not published in public specs |
| Universal Audio Volt desktop interfaces | Instrument input, minimum gain | +12.5 dBu | -13.3 dBFS | +0.5 dB | Manufacturer spec |
| Arturia MiniFuse interfaces | Instrument input, minimum gain | +11.5 dBu | -12.3 dBFS | -0.5 dB | Manufacturer spec |
| Audient iD4 MKII | D.I. / instrument input, minimum gain | +12.0 dBu | -12.8 dBFS | 0.0 dB | Manufacturer spec |
| Audient iD4 MKI | D.I. input, minimum gain | +12.0 dBu | -12.8 dBFS | 0.0 dB | Manufacturer spec |
| Solid State Logic SSL 2 / SSL 2+ MKII | Instrument input, minimum gain | +15.0 dBu | -15.8 dBFS | +3.0 dB | Manufacturer spec |
| PreSonus Studio 24c | Instrument input, minimum gain | +19.0 dBu | -19.8 dBFS | +7.0 dB | Manufacturer spec |
| Behringer U-Phoria UMC22 | Instrument input, minimum gain | +2.0 dBu | -2.8 dBFS | -10.0 dB | Manufacturer spec |
| Behringer UMC202HD / UMC204HD / UMC404HD | Instrument input, pad off, minimum gain | -3.0 dBu | +2.2 dBFS (clips) | -15.0 dB | Manufacturer spec |

The "1V peak sine reads" column shows where a 1 V peak 1 kHz sine wave (-0.79 dBu) naturally
lands on each device at unity gain. Rock Hero gain brings that reading to -12.8 dBFS, where a 1 V
peak sine sits against the +12 dBu reference.

Sources for the table:

- [Neural DSP input calibration guidance](https://unity.neuraldsp.com/t/optimal-input-level-for-highest-accuracy-when-using-ndsp-plugins/11048)
- [NAM calibration tutorial](https://neural-amp-modeler.readthedocs.io/en/stable/tutorials/calibration.html)
- [NAM file specification](https://neural-amp-modeler.readthedocs.io/en/stable/model-file.html)
- [Focusrite Scarlett Solo 4th Gen specifications](https://userguides.focusrite.com/hc/en-gb/articles/17505454908562-Scarlett-Solo-Specifications)
- [Focusrite Scarlett 2i2 4th Gen specifications](https://focusrite.com/products/scarlett-2i2)
- [Focusrite Scarlett Solo 3rd Gen specifications](https://userguides.focusrite.com/hc/en-gb/articles/23031457381138-Scarlett-Solo-3rd-Gen-specifications)
- [Focusrite Scarlett 2i2 3rd Gen specifications](https://us.focusrite.com/products/scarlett-2i2-3rd-gen)
- [MOTU M Series user guide](https://cdn-data.motu.com/manuals/usb-c-audio/M_Series_User_Guide.pdf)
- [Neural DSP Quad Cortex manual](https://neuraldsp.com/manual/quad-cortex)
- [Neural DSP Quad Cortex mini manual](https://neuraldsp.com/manual/quad-cortex-mini)
- [Neural DSP Nano Cortex manual](https://neuraldsp.com/manual/nano-cortex)
- [Universal Audio Volt specifications](https://help.uaudio.com/hc/en-us/articles/4409522227092-Volt-Specifications)
- [Arturia MiniFuse 2 specifications](https://www.arturia.com/products/audio/minifuse/minifuse-2)
- [Audient iD4 MKII specifications](https://audient.com/products/audio-interfaces/id4/tech-specs/)
- [Audient iD4 MKI specifications](https://support.audient.com/hc/en-us/articles/210725106-iD4-Detailed-Specs)
- [SSL 2 MKII user guide](https://support.solidstatelogic.com/hc/en-gb/articles/19991374319773-SSL-2-MKII-User-Guide)
- [PreSonus Studio 24c specifications](https://www.presonus.com/en-US/interfaces/usb-audio-interfaces/studio-series/2777700403.html)
- [Behringer U-Phoria quick start guide](https://manualmachine.com/behringer/umc22/1942648-quick-start-guide/)

## Automatic Calibration

1. Select the correct input device and input channel in the audio settings.
2. Open input calibration and press **Calibrate**.
3. Play as hard as you play in a song, on all strings, until the countdown ends.
4. Retry if the input clips or nothing is heard.

Rock Hero waits up to ten seconds for you to start, then listens for ten seconds from your first
strum and sets the gain from the loudest of your playing, ignoring a single stray spike.

## Best Results

Use the same guitar volume, pickup selection, cable, interface input, and Windows recording level
that will be used while playing. Recalibrate after changing the input device, input channel,
hardware gain, or operating-system recording level.
