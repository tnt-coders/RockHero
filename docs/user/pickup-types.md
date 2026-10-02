\page user_pickup_types Pickup Types

Automatic calibration assumes how hard a typical hard strum peaks on each kind of pickup. Pick the
kind you will play; finer differences (vintage against hot output, Strat against Tele) are smaller
than the spread every guitar has anyway.

| Pickup | Covers | Assumed hard-strum peak | How sure |
|---|---|---|---|
| **Humbucker** | Passive humbuckers of any size: full-size, Filter'Trons, and single-coil-sized ones, whether rails (Hot Rails, Fast Track) or side-by-side (Little '59) | 2.0 V | Six calibrated measurements; rails read level with full-size; side-by-side inferred |
| **Single-coil** | Strat, Tele, Jazzmaster, Jaguar and lipstick pickups, and stacked noiseless pickups, which are humbuckers inside but built to sound and measure like single coils | 1.0 V | Three calibrated measurements; stacked read level with vintage single coils |
| **P-90** | Soapbar and dog-ear P-90s: single coils by construction that measure like humbuckers | 2.0 V | Inferred from pickup makers' output figures |
| **Mini-humbucker** | Narrow humbuckers, as on a Firebird or a Les Paul Deluxe | 1.2 V | Inferred from two makers' output figures |
| **Active** | Battery-powered pickups: EMG, Fishman Fluence | 2.1 V | One measurement; the battery's voltage caps the output |

The assumed peak is the voltage at the guitar's jack on a hard strum, which no pickup maker
publishes: their output figures in mV come from their own test rigs and read several times lower.

Two cases the list cannot cover well:

- **Actives modified to run on 18 V** can reach about twice the voltage and may calibrate up to
  6 dB low.
- **Acoustic-electric guitars** reach the jack through their own preamp and volume control, so no
  pickup type fits; use the audio device list or type a gain instead.

The sources and the arithmetic are in `docs/tracking/2026-10-01-hard-strum-peak-research.md` and
`docs/tracking/2026-10-01-pickup-type-output-research.md`.
