\page user_input_calibration Input Calibration

Input calibration sets the pre-effects guitar input gain used by Rock Hero. The calibration target
is **-12 dBFS average** with peaks no higher than **-6 dBFS**.

## Recommended Method

Use manual calibration when the exact specifications for the device are known. Set the input gain
so the specified device level maps to the target level in Rock Hero.

Use automatic calibration when the device does not provide reliable gain specifications or when
using a Windows audio device such as a Real Tone cable.

## Known Unity-Gain Device Settings

These settings are starting points for manual calibration when the hardware input gain is at
minimum or `0.0 dB`, the guitar is connected to the instrument or Hi-Z input, and any pad, boost,
compressor, vintage mode, Air mode, or operating-system input boost is off unless the row says
otherwise.

The table targets Rock Hero's **-12 dBFS** input reference. NAM calibration metadata expresses
interface input calibration as the dBu level of a 1 kHz sine wave that reaches `0 dBFS` peak.
A 1 V peak sine wave is 0.707 V RMS, which is `-0.79 dBu`, so a 1 V peak sine wave reading
`-12 dBFS` means the equivalent `0 dBFS` input calibration level is `+11.21 dBu`. The manual
Rock Hero gain is therefore:

```text
Rock Hero gain = device maximum instrument input level - 11.2 dBu
```

This differs from Neural DSP plugin calibration guidance, which maps the same 1 V peak sine wave
to `-13 dBFS` and therefore implies `+12.21 dBu = 0 dBFS`. To mimic that Neural DSP plugin
reference instead, subtract **1.0 dB** from every Rock Hero gain in this table.

Use the exact model and generation. Interface families reuse names, but their instrument input
headroom can change between generations. Rows based on manufacturer maximum-input specifications
are higher confidence than rows inferred from measured dBFS behavior.

| Device | Unity input mode | Device level at 0 dBFS | 1V peak sine reads | Rock Hero gain | Basis |
|--------|------------------|------------------------|--------------------|----------------|-------|
| Focusrite Scarlett Solo 3rd Gen | Instrument input, minimum gain | +12.5 dBu | -13.3 dBFS | +1.3 dB | Manufacturer spec |
| Focusrite Scarlett 2i2 3rd Gen | Instrument input, minimum gain | +12.5 dBu | -13.3 dBFS | +1.3 dB | Manufacturer spec |
| Focusrite Scarlett Solo 4th Gen | Instrument input, minimum gain | +12.0 dBu | -12.8 dBFS | +0.8 dB | Manufacturer spec |
| Focusrite Scarlett 2i2 4th Gen | Instrument input, minimum gain | +12.0 dBu | -12.8 dBFS | +0.8 dB | Manufacturer spec |
| MOTU M2, M4, M6 | Combo TRS guitar input, minimum gain | +16.0 dBu | -16.8 dBFS | +4.8 dB | Manufacturer spec |
| Neural DSP Quad Cortex | Instrument input, 1 MOhm, 0.0 dB input level | +14.3 dBu | -15.1 dBFS | +3.1 dB | Inferred; not published in public specs |
| Neural DSP Quad Cortex mini | Input 1 or input 2 TRS, 1 MOhm, minimum gain | +14.5 dBu | -15.3 dBFS | +3.3 dB | Inferred; not published in public specs |
| Neural DSP Nano Cortex | Instrument or capture input, minimum gain | +10.0 dBu | -10.8 dBFS | -1.2 dB | Inferred; not published in public specs |
| Universal Audio Volt desktop interfaces | Instrument input, minimum gain | +12.5 dBu | -13.3 dBFS | +1.3 dB | Manufacturer spec |
| Arturia MiniFuse interfaces | Instrument input, minimum gain | +11.5 dBu | -12.3 dBFS | +0.3 dB | Manufacturer spec |
| Audient iD4 MKII | D.I. / instrument input, minimum gain | +12.0 dBu | -12.8 dBFS | +0.8 dB | Manufacturer spec |
| Audient iD4 MKI | D.I. input, minimum gain | +12.0 dBu | -12.8 dBFS | +0.8 dB | Manufacturer spec |
| Solid State Logic SSL 2 / SSL 2+ MKII | Instrument input, minimum gain | +15.0 dBu | -15.8 dBFS | +3.8 dB | Manufacturer spec |
| PreSonus Studio 24c | Instrument input, minimum gain | +19.0 dBu | -19.8 dBFS | +7.8 dB | Manufacturer spec |
| Behringer U-Phoria UMC22 | Instrument input, minimum gain | +2.0 dBu | -2.8 dBFS | -9.2 dB | Manufacturer spec |
| Behringer UMC202HD / UMC204HD / UMC404HD | Instrument input, pad off, minimum gain | -3.0 dBu | +2.2 dBFS (clips) | -14.2 dB | Manufacturer spec |

The "1V peak sine reads" column shows where a 1 V peak 1 kHz sine wave (-0.79 dBu) naturally
lands on each device at unity gain. Rock Hero gain is the offset needed to bring that reading to
-12 dBFS.

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
2. Open input calibration and press **Start**.
3. Strum all strings open at a steady, moderate volume until the measurement completes.
4. Retry if the input clips, is too quiet, or varies too much during the measurement.

Rock Hero waits for a usable input signal before measuring. During measurement, it uses steady
active input, rejects clipping, and rejects input levels that are too inconsistent to produce a
stable gain.

## Best Results

Use the same guitar volume, pickup selection, cable, interface input, and Windows recording level
that will be used while playing. Recalibrate after changing the input device, input channel,
hardware gain, or operating-system recording level.
