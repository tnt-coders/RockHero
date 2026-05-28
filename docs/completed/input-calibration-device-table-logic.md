# Input Calibration Device Table Logic

Status: completed audit note. Keep this aligned with the known unity-gain device settings in
`docs/user/input-calibration.md` while that table is still being reviewed.

## Goal

Document the logic used to generate the manual device-gain table so the values can be audited,
recomputed, or corrected without relying on conversational context.

The current table is intended to target the NAM-style `-12 dBFS` calibration point requested for
Rock Hero. It is not using the Neural DSP plugin `-13 dBFS` reference.

## Reference Target

The table answers this question:

```text
How much Rock Hero input gain should be applied at unity hardware gain so a 1 V peak
1 kHz sine wave would read -12 dBFS?
```

The unit conversion is:

```text
dBu = 20 * log10(Vrms / 0.7746)
Vrms = Vpeak / sqrt(2) for a sine wave
```

For a 1 V peak sine wave:

```text
Vrms = 1 / sqrt(2) = 0.70710678 V
dBu = 20 * log10(0.70710678 / 0.7746) = -0.7918 dBu
```

If that sine wave should read `-12 dBFS`, the equivalent full-scale input calibration level is:

```text
0 dBFS input level = -0.7918 dBu + 12.0 dB = +11.2082 dBu
```

The table rounds this to `+11.2 dBu`.

## Row Formula

For rows based on manufacturer maximum input level:

```text
Rock Hero gain = device maximum instrument input level at 0 dBFS - 11.2082 dBu
```

The displayed value is rounded to the nearest `0.1 dB` for documentation. This is separate from
the automatic calibration code, which may quantize stored calibration results differently.

Examples:

```text
Focusrite +12.5 dBu: 12.5 - 11.2082 = +1.2918 dB -> +1.3 dB
MOTU +16.0 dBu:      16.0 - 11.2082 = +4.7918 dB -> +4.8 dB
Nano Cortex +10 dBu: 10.0 - 11.2082 = -1.2082 dB -> -1.2 dB
```

## Quad Cortex Inference

The full-size Quad Cortex row is not based on a clean manufacturer maximum-input-level spec in
the current source list. It is inferred from the Neural DSP calibration discussion reporting that
a 1 V peak sine wave reads about `-15.1 dBFS` through Quad Cortex at 1 MOhm and `0.0 dB` input
level.

For the Rock Hero/NAM `-12 dBFS` target:

```text
needed gain = -12.0 - (-15.1) = +3.1 dB
```

The same measurement implies an equivalent full-scale input level:

```text
0 dBFS input level = -0.7918 dBu + 15.1 dB = +14.3082 dBu
gain = +14.3082 dBu - +11.2082 dBu = +3.1 dB
```

The earlier `+2.1 dB` value was only correct for a `-13 dBFS` reference:

```text
-15.1 dBFS + 2.1 dB = -13.0 dBFS
```

Because Rock Hero is explicitly targeting `-12 dBFS`, the table should use `+3.1 dB` for this
Quad Cortex measurement.

## Current Value Audit

These are the current calculations behind the table rows. Duplicate device families share the
same calculation when the source specs report the same instrument-input headroom.

| Device or family | Source level at 0 dBFS | Calculation | Displayed gain |
|------------------|------------------------|-------------|----------------|
| Focusrite Scarlett Solo 3rd Gen | +12.5 dBu | 12.5 - 11.2082 | +1.3 dB |
| Focusrite Scarlett 2i2 3rd Gen | +12.5 dBu | 12.5 - 11.2082 | +1.3 dB |
| Focusrite Scarlett Solo 4th Gen | +12.0 dBu | 12.0 - 11.2082 | +0.8 dB |
| Focusrite Scarlett 2i2 4th Gen | +12.0 dBu | 12.0 - 11.2082 | +0.8 dB |
| MOTU M2, M4, M6 | +16.0 dBu | 16.0 - 11.2082 | +4.8 dB |
| Neural DSP Quad Cortex | +14.3 dBu | 14.3082 - 11.2082 | +3.1 dB |
| Neural DSP Quad Cortex mini | +14.5 dBu | 14.5 - 11.2082 | +3.3 dB |
| Neural DSP Nano Cortex | +10.0 dBu | 10.0 - 11.2082 | -1.2 dB |
| Universal Audio Volt desktop interfaces | +12.5 dBu | 12.5 - 11.2082 | +1.3 dB |
| Arturia MiniFuse interfaces | +11.5 dBu | 11.5 - 11.2082 | +0.3 dB |
| Audient iD4 MKII | +12.0 dBu | 12.0 - 11.2082 | +0.8 dB |
| Audient iD4 MKI | +12.0 dBu | 12.0 - 11.2082 | +0.8 dB |
| SSL 2 / SSL 2+ MKII | +15.0 dBu | 15.0 - 11.2082 | +3.8 dB |
| PreSonus Studio 24c | +19.0 dBu | 19.0 - 11.2082 | +7.8 dB |
| Behringer U-Phoria UMC22 | +2.0 dBu | 2.0 - 11.2082 | -9.2 dB |
| Behringer UMC202HD / UMC204HD / UMC404HD | -3.0 dBu | -3.0 - 11.2082 | -14.2 dB |

## Source Selection Rules

Use sources in this order:

1. Manufacturer maximum input level for the exact model generation, instrument or Hi-Z input,
   hardware input gain at minimum or `0.0 dB`, pad disabled unless the row says otherwise.
2. Manufacturer manual or support article for the exact product family when the exact page is
   missing but the input hardware is clearly shared.
3. A measured dBFS value from a known sine-wave voltage, with the row marked as inferred.
4. Third-party or community measurements only if no better source exists, with a confidence note.

Do not mix generations unless the source states that the input specification is shared. Interface
families often reuse names while changing instrument-input headroom.

## Unit Conversion Notes

If a source reports dBV instead of dBu:

```text
dBu = dBV + 2.218
```

If a source reports RMS voltage at full scale:

```text
dBu = 20 * log10(Vrms / 0.7746)
```

If a source reports peak voltage for a sine wave:

```text
Vrms = Vpeak / sqrt(2)
dBu = 20 * log10(Vrms / 0.7746)
```

If a source reports a known sine level and measured dBFS:

```text
device 0 dBFS dBu = sine dBu + abs(measured dBFS)
Rock Hero gain = target dBFS - measured dBFS
```

For the current `-12 dBFS` target, this simplifies to:

```text
Rock Hero gain = -12.0 - measured dBFS
```

## Confidence Labels

Use these confidence levels when reviewing or expanding the table:

- High: exact manufacturer maximum input level for the exact model, generation, input mode, and
  gain setting.
- Medium: manufacturer information that appears to apply to the device family but does not fully
  spell out generation, input mode, or gain setting.
- Low: inferred from measured dBFS behavior, because impedance, firmware, routing, meters, and
  test setup can affect the result.

The full-size Quad Cortex and Quad Cortex mini rows should remain explicitly marked as inferred.
Neural DSP does not publish maximum instrument input levels in dBu in their public product pages
or user manuals for either device.

The Audient iD4 MKI uses `+12.0 dBu` (the ADC lineup level from the manufacturer detailed specs),
not the `+8.0 dBu` JFET DI THD threshold that appeared in earlier versions of this table. The
`+8.0 dBu` figure describes where the analog JFET stage introduces 0.6% harmonic distortion, not
where the ADC reaches `0 dBFS`. For calibration purposes the ADC clipping point is the relevant
number.

## Relationship To Automatic Calibration

The manual table and automatic calibration are related but not identical.

The manual table is an analog-reference alignment table for known devices. It asks what fixed
gain makes a known sine-wave reference land at `-12 dBFS`.

The automatic calibration code measures live playing, targets `-12 dBFS` active-window RMS, and
also limits gain so the robust hard-strum peak stays no higher than `-6 dBFS`. That produces a
performance-specific gain, not a direct manufacturer-spec conversion.

This distinction should stay visible in user docs so users do not assume the table and automatic
calibration are two implementations of the exact same measurement.

## Source Links

The user-facing table keeps the complete source list in `docs/user/input-calibration.md`. The most
important references for the table logic are:

- NAM calibration tutorial:
  <https://neural-amp-modeler.readthedocs.io/en/stable/tutorials/calibration.html>
- NAM model file specification:
  <https://neural-amp-modeler.readthedocs.io/en/stable/model-file.html>
- Neural DSP input calibration discussion:
  <https://unity.neuraldsp.com/t/optimal-input-level-for-highest-accuracy-when-using-ndsp-plugins/11048>

