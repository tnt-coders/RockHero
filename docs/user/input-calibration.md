\page user_input_calibration Input Calibration

Input calibration sets the pre-effects guitar input gain used by Rock Hero. The calibration target
is **-12 dBFS average** with peaks no higher than **-6 dBFS**.

## Recommended Method

Use manual calibration when the exact specifications for the device are known. Set the input gain
so the specified device level maps to the target level in Rock Hero.

Use automatic calibration when the device does not provide reliable gain specifications or when
using a Windows audio device such as a Real Tone cable.

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
