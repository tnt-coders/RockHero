# Hard-strum peak voltage — research snapshot of 2026-10-01

A snapshot, not a registry. Companion to `2026-10-01-input-level-calibration-research.md`, which
settled the method (estimate the interface's dBu-at-0-dBFS; fall back to the hardest-playing peak)
and left one constant unexamined: `V_hard`, the true sample peak a hard strum is assumed to
produce. That pass took 1 V peak from a misread of one article. This pass asks what `V_hard`
really is, per pickup class, whether P95 of 33 ms window peaks is the right statistic, whether an
RMS statistic would be tighter, and whether the fallback should estimate sensitivity at all.
Source strength per claim: **measurement** (a scope, a DMM, or a meter on an interface whose full
scale is known), **vendor**, **community** (forum claim without a stated calibration),
**INFERRED** (my arithmetic or reading). Every volt figure below is a sine-equivalent peak
(`Vrms × √2`; `dBu → Vrms = 0.7746 × 10^(dBu/20)`), stated with its arithmetic.

## The answer

`V_hard` for a hard-strummed passive humbucker is about **2 V peak, with a ±6 dB spread
(1–4 V)** — not 1 V. Every calibrated data point found sits in that band: a '57 Classic at
-1.2 dBFS on a +11.5 dBu input (3.6 V), the user's neck humbucker at 2.07 V, a 498T clipping a
4.1 V input, '57 Classic power chords at 1.1 V at *normal* intensity, and the sound-au article's
own 0.85 V chord plus its "at least 6 dB more when played hard". Single coils run about 6 dB
lower (**~1 V, 0.4–1.8 V**), P-90s sit at humbucker level (INFERRED from DiMarzio's one-scale mV
table), and actives are capped by their own preamp rail (**EMG 81 at 9 V: 2.1 V measured,
clipped**). The user's 2.07 V is therefore not an anomaly; it is the median case, and with
`V_hard = 2 V` the fallback would have produced +2.0 dB against the table's +2.3. The
statistic is not the problem: in a 10 s hard stretch P95 of window peaks sits 1–2 dB under the
loudest attack and settles within 1 dB after 2–3 s; the choice between P95, P99, max and an
onset-median moves the answer by ≤ 2 dB, the guitar moves it by ±6 dB. RMS is worse on every
axis: its per-stretch spread is 25–65 % wider than the peak statistics' on the same DI, it tracks
note density, and the crest factor it would have to assume spans 13–19 dB. Estimating sensitivity
from playing is honest to **±6 dB with one "single-coil / humbucker / active?" question and ±10 dB
without it**; a fixed headroom target (UA's -8 dBFS peak, Focusrite's -12 dBFS) is the same
single gain under a different name, and UA themselves say not to use it for amp plug-ins. A
"neck humbucker, volume and tone full, one pickup" instruction tightens that by about 1.5 dB
more (INFERRED ± 4.7 dB, one calibrated point) and, more usefully, pins the knobs — a volume
left at "8" silently costs 3–8 dB, more than the whole class question is worth.

## Evidence, per pickup class

Hard strumming means full chords struck as hard as the player strikes them in a song. Single-note
and normal-intensity figures are marked, because the two differ by 6–10 dB: Tom's scope found
"soft, medium-hard and hard picking ... about a factor of 2 to 3" (6–9.5 dB)
([measurement][toms]) and sound-au expects "at least 6 dB (×2) more level when played hard" over
its normal-playing table ([measurement][sound-au]).

| Class | Median `V_hard` (true peak) | Band | Basis |
|---|---|---|---|
| Passive humbucker, neck or bridge | **2 V** | 1–4 V (-6/+6 dB) | 6 calibrated points below; neck vs bridge not separable (SD's strum arm has them within 1 dB) |
| Single coil (Strat/Tele) | **1 V** | 0.4–1.8 V (-8/+5 dB) | 3 calibrated points + 2 INFERRED; ~6 dB under humbuckers on three independent scales |
| P-90 | **2 V** (INFERRED) | 1–3.5 V | no direct measurement; DiMarzio's mV scale puts P-90s level with PAF-class humbuckers |
| Active (EMG 81 class, 9 V) | **2.1 V** (rail-clipped) | 2–4.5 V | one calibrated measurement at its rail; vendor "4.5 V strum" spec; +7–9 dB over a '59 on the strum arm |

### Passive humbucker

- **'57 Classic, hard strumming, HX Stomp as interface, pad off: peaked at -1.2 dBFS**
  ([measurement][gf-p9], MirrorProfiles). The Helix/HX guitar input is +11.5 dBu full scale
  = 0.7746 × 10^(11.5/20) = 2.911 Vrms = **4.12 V peak** ([community, James Freeman][hx-clip]);
  4.12 × 10^(-1.2/20) = **3.59 V peak**. Same session: a 498T Les Paul "clips heavily" (> 4.1 V).
- **The user's neck humbucker on a Quad Cortex** (Input 1, 1 MΩ, level 0.0 dB): full scale
  +14.3 dBu = 0.7746 × 5.188 = 4.02 Vrms = 5.68 V peak (from Neural DSP's 1 Vp = -15.1 dBFS,
  [community relaying support][ndsp-unity]); measured P95 = -12.79 - (-4.0) = -8.79 dBFS; 5.68 ×
  10^(-8.79/20) = **2.07 V peak** (measurement, this project).
- **'57 Classic power chords at "normal intensity", RME UFX III, 20 dB instrument gain: -8.8
  dBFS** ([measurement][rme], Ramses). RME: max input +21 dBu at gain 8 dB ([vendor via
  forum admin][rme]) → +9 dBu at gain 20 = 2.183 Vrms = 3.09 V peak; × 10^(-8.8/20) =
  **1.12 V peak at normal intensity**; +6 dB for hard playing → ~2.2 V (INFERRED).
- **Quad Cortex meter at input level 0: "my higher gain guitars come in at -12 dB"**
  ([community][qc-9916], frankvhalen): 5.68 × 10^(-12/20) = **1.43 V**; "cleaner guitars ...
  -20 to -18 dB" = 0.57–0.72 V. Another user: "hard strums peak at -12 dB", pickup unstated
  ([community][qc-7401]). Playing intensity and meter ballistics unknown; weak.
- **sound-au scope table** (10 MΩ probe, volume and tone at max): Samick bridge humbucker, open
  E chord **850 mV peak / 128 mV RMS**; Maton DiMarzio humbucker 200–300 mV peak chords; "peak at
  just under 1 V" is the article's maximum, at normal playing ([measurement][sound-au]). With the
  article's own ≥ 6 dB hard-playing allowance: ≥ 1.7 V (INFERRED).
- **muzique scope**: Les Paul with "moderately hot pickups", one open D string, initial pluck
  **1.2 V peak-to-peak = 0.6 V peak** (424 mV RMS over the first cycles), 0.423 Vpp one second
  later — a decay of 20·log10(0.423/1.2) = **-9 dB/s** ([measurement][muzique]).
- **Tom's scope** (1 MΩ): Artec vintage-output bridge humbucker, hard strum, **280 mV peak**
  transient, 150 mV sustained ([measurement][toms]). The low tail: a low-output pickup, height
  unstated.
- **Steel guitar**: George L Pentad wired as a humbucker, 1 MΩ load, "attack-peaks above 3 V
  (3000 mV) on scope ... RMS was rarely above .250 V" ([measurement, different instrument,
  heavier strings][steel]); another player "up to ~5 volts" for highly wound steel pickups
  played "with authority" ([community][steel]).
- **Fractal forum**: "Medium output humbuckers can generate up to 8–10 volts peak to peak"
  (4–5 V peak) ([community][fractal-emg]) — the upper tail, consistent with the 498T clipping
  4.1 V and with Line 6 shipping a 5.5 dB pad precisely for "guitars with hot pickups".
- **Seymour Duncan strum-arm chart** (peak on a storage scope, spring-loaded pick, pickup at
  0.093" = 2.4 mm, Telecaster test bed): '59 neck/bridge **572/593 mV**, Jazz 501/571, JB 737,
  Custom 784, Distortion 732/792, Dimebucker 1160 ([vendor data, forum-published][sd-mv]).
  The stimulus strength is unstated ("corresponds closely to actual playing"), so these are
  relative; they say neck ≈ bridge within 1 dB and vintage-to-hot spans 572→1160 = 6 dB.
- **Pickup height** explains much of the band: SD's recommended 2.4 mm ([vendor][sd-height])
  is where the chart was taken; players set bridge humbuckers at 1.5–2 mm, and the forum rule
  of thumb is "every 1 mm ... like a 3 dB change" ([community][sd-height-db]). Two guitars with
  the same pickup can differ by 3 dB on height alone.

### Single coil

- **Telecaster, hard strumming, HX Stomp: -7 to -8 dBFS** ([measurement][gf-p9],
  MirrorProfiles) = 4.12 × 10^(-7/20) … 10^(-8/20) = **1.84–1.64 V peak**. Same player and
  session as the 3.59 V '57 Classic: a 6.4 dB class gap.
- **Texas Specials vs '57 Classic, same player, RME: 22 dB vs 20 dB gain** to meter alike
  ([measurement][rme]) → single coil ≈ 2 dB under a vintage humbucker for that player;
  normal intensity ≈ 0.9 V, hard ≈ 1.8 V (INFERRED).
- **sound-au**: Samick single-coil neck/middle, E chord **450 / 400 mV peak** (76/72 mV RMS) at
  normal playing ([measurement][sound-au]); ≥ 6 dB for hard → ≥ 0.8–0.9 V (INFERRED).
- **Quad Cortex, vintage Strat pickups, level 0: "I peak at maybe -25 dB"**
  ([community][qc-9916]) = 0.32 V; intensity unknown — the low tail.
- **DiMarzio's one-scale mV table** ([vendor][dm-faq]; method unpublished, "compare only to other
  DiMarzios"): Area 58 125 mV, True Velvet 130–142, Injector 160–185, Twang King bridge 198
  ([vendor][dm-area58], [vendor][dm-tv], [vendor][dm-tk]) against PAF 36th 250–285
  ([vendor via review][dm-paf]) and Super Distortion 425 ([vendor][dm-sd]): single coils sit
  **-5 to -7 dB** under PAF-class humbuckers (20·log10(150/285) = -5.6 dB).

### P-90

No scope or calibrated-meter measurement of a hard-strummed P-90 was found. On DiMarzio's scale
the Soapbar is 270 mV, Fantom P90 285, DLX Plus 380/400 ([vendor][dm-soap], [vendor][dm-fantom],
[vendor][dm-dlx]) — level with or above its PAF 36th (250–285). Wikipedia's "241 mV RMS" for the
P-90 is a field-coil resonance figure, not playing, and is not used. **INFERRED: treat P-90 as
humbucker-class, 2 V ± 6 dB.** Line 6 users report P-90s clipping the 4.1 V input "when playing
heavily on the low E" alongside humbuckers ([community][l6-clip]).

### Active (EMG 81 class)

- **EMG 81, hard strumming, Helix, pad off: "the internal EMG81 opamp is clipping but not Helix
  Input, without the pad I max at -5.9 dBFS"** ([measurement][gf-p9], James Freeman) = 4.12 ×
  10^(-5.9/20) = **2.09 V peak, flat-topped**. The pickup's own preamp is the ceiling.
- Fractal thread: EMG's "rail to rail voltage is ... about 4 volts. Less with most batteries"
  ([community][fractal-emg]) — a 4 V swing is 2 V peak, matching the measurement; an 18 V mod
  doubles it.
- EMG's published spec: "Output Voltage (String) 3.00 V, (Strum) 4.50 V"
  ([vendor][emg-ds]; [community transcription][emg-wiki]). Not reconcilable with a 9 V
  single-supply swing unless read as peak-to-peak or at a higher supply; INFERRED, not used
  for the median.
- SD Blackouts on the same strum arm as the passives: 1283/1598 mV vs the '59's 572/593
  ([vendor data][sd-mv]) = +7.0/+8.6 dB. Applied to a 2 V passive median that would be 4.5–5.4 V,
  which the rail prevents — actives are the one class whose spread is *capped*, so they are
  tighter (about +5/-1.5 dB around 2.5 V) but sit 2–6 dB above a passive humbucker.

### Crest factor, hard strumming (for the RMS question)

| Source | Peak − RMS | Note |
|---|---|---|
| sound-au chords, humbucker (200/36, 300/36, 850/128 mV) | 14.9–18.4 dB | scope RMS over the trace |
| sound-au chords, single coil (450/76, 400/72) | 15.4–14.9 dB | same |
| sound-au single notes (150/40 … 120/12) | 5.5–20 dB | note-dependent; useless as a constant |
| steel guitar humbucker (> 3 V / < 0.25 V) | > 21.6 dB | heavy strings |
| DI file, 26 × 10 s stretches, max − active-window RMS | 13.7–19.3 dB (mean 15.9, sd 1.8) | computed below |
| DI file, P95 − active-window RMS | 11.6–16.7 dB (mean 13.2, sd 1.4) | computed below |

No class difference in crest factor is provable from this data (humbucker and single-coil chords
both land at 15–18 dB on sound-au's scope).

## The statistic: P95 of 33 ms window peaks

Two computations, both on relative dB so the unknown calibration of the file does not matter.

**Real DI.** `Data/train/ht1-input.wav` from Alec Wright's amp-modelling dataset
([maintainer][wright]; 16-bit, 44.1 kHz, 340 s of mixed electric-guitar playing, one guitar,
calibration unstated). 33 ms windows at 30 Hz, active = window peak > -40 dBFS (74 % of
windows). Whole file: max -5.18 dBFS, P99.9 -5.35, **P99 -6.56, P95 -8.13**, P90 -9.11, median
-14.62. Note the ceiling: every 10 s stretch tops out at -5.2 to -5.4 dBFS, so the file was
limited or clipped at capture and "max" is flattered. In the loudest 10 s stretch (by P95):
max -5.73, P99 -6.10, **P95 -6.73**, P90 -7.50, median -10.52 — P95 sits 1.0 dB under the
loudest attack. Convergence of P95 inside that stretch: 1 s -9.16, 2 s -7.03, 3 s -7.56,
5 s -7.56, 7 s -7.45, 10 s -6.73 — within 1 dB of its 10 s value after 2 s. Onset attacks
(windows rising ≥ 6 dB over the previous and maximal within ±150 ms; n = 271): median -10.55,
P90 -6.85, P95 -6.32. Across the 26 full 10 s stretches, the stretch-to-stretch standard
deviation: max 1.20 dB, P99 1.28, P95 1.50 — against active-window RMS 1.86, 3 s short-term
loudness 2.14, median window RMS 2.46.

**Simulation** (attack peaks log-normal with σ = 2 or 4 dB, 9 dB/s decay from muzique's trace,
10 s, 400 trials; values in dB relative to the attack *median*, mean ± sd across trials):

| strums/s | σ | window P95 | P99 | max | onset median |
|---|---|---|---|---|---|
| 1 | 2 | +0.9 ± 0.8 | +2.3 | +3.0 | 0.0 ± 0.7 |
| 2 | 2 | +1.6 ± 0.7 | +2.9 | +3.6 | 0.0 ± 0.5 |
| 4 | 2 | +2.3 ± 0.6 | +3.6 | +4.2 | 0.0 ± 0.4 |
| 8 | 2 | +3.0 ± 0.5 | +4.1 | +4.7 | 0.0 ± 0.3 |
| 2 | 4 | +4.6 ± 1.7 | +6.6 | +7.4 | 0.0 ± 1.1 |

Findings. (1) P95 is sound: it converges in 2–3 s of hard playing, lands 1–2 dB under the
loudest attack, and its run-to-run sd is 0.5–0.8 dB at a consistent 2 dB per-strum spread.
(2) It is tempo-dependent by about 2 dB between slow chords and fast strumming, because a strum's
peak occupies only ~3 windows before the 9 dB/s decay drops it; P99 and max are 1–2 dB higher and
less tempo-dependent but rest on 3 windows or 1. (3) The onset median is the only
tempo-independent statistic and is 0.2–0.4 dB tighter, but it needs onset detection and would
redefine `V_hard` as a *median attack* where every calibrated data point above is a *meter
maximum* while strumming hard — the constant must be defined against the statistic that
produced the data. (4) None of this matters at the scale of the problem: the statistics differ by
≤ 2 dB, the guitar by ± 6 dB. **Keep P95 of 33 ms window peaks, 10 s, -40 dBFS floor; do not
add onset detection.** Since the data points are maxima and P95 reads 1–2 dB under the max, a
`V_hard` taken from the table above carries a built-in 1–2 dB bias toward too much gain; the
user's case (+2.0 computed vs +2.3 table) is inside that.

**RMS instead of peak** (the coordinator's addition). No. On the same DI the RMS statistics'
stretch-to-stretch spread is 25–65 % wider than the peak statistics' (1.86–2.46 dB vs 1.20–1.50),
because RMS follows note density, sustain and muting while the attack peak follows only how hard
the pick hits. Across guitars the spread is comparable either way (sound-au chord RMS 36→128 mV
= 11 dB vs peak 200→850 mV = 12.6 dB), so RMS buys nothing there and costs the crest factor: to
turn an RMS reading into an interface sensitivity the code would have to assume a crest factor
that this data puts at 13–19 dB (± 3 dB), on top of the ± 6 dB guitar spread. The prior pass's
"RMS follows the player's dynamics 1:1" stands, now with numbers. The one honest use of RMS is a
gate (ignore stretches whose loudness says the player stopped), which the -40 dBFS floor already
is.

## Fallback design and its honest accuracy

**Is estimating sensitivity from playing sound?** As an estimate of a quantity the guitar does not
know, it is honest to the guitar's spread: **± 6 dB within a class, ± 10 dB across passive
classes** (0.4–4 V), with actives 2–6 dB above passives and clipped at their own rail. The
run-to-run part is small (0.5–0.8 dB in simulation, 1–1.5 dB across real stretches), so a
second take will not improve it; only knowing the guitar will.

**One question narrows it.** "Single-coil / humbucker-or-P-90 / active?" removes the ~6 dB class
offset that three independent scales agree on (HX Stomp 6.4 dB, DiMarzio 5.6 dB, sound-au 5.5
dB), shrinking the band from about ± 10 dB to ± 6 dB, and it is *required* for actives, whose
rail puts a hard strum at 2.1 V regardless of how hard it was hit — without the question an EMG
player reads as a loud humbucker and gets 0–3 dB too little gain, a vintage Strat reads as a
quiet humbucker and gets 6 dB too much. Suggested `V_hard`: single coil 1.0 V (target
-12.8 dBFS at +12 dBu = 0 dBFS, since 0 dBFS = 3.08 Vrms = 4.36 V peak and
20·log10(1.0/4.36) = -12.8), humbucker/P-90 2.0 V (-6.8 dBFS), active 2.1 V (-6.3 dBFS).
Three classes, not five: neck vs bridge is < 1 dB on SD's chart, P-90 is not separable from
humbucker by any data found.

**Headroom target instead of sensitivity?** Mathematically it is the same operation: setting the
player's P95 to a target `T` dBFS *is* assuming `V_hard = full_scale × 10^(T/20)`; UA's -8 dBFS
peak target and Focusrite's -12 dBFS ([vendor][ua-autogain]; [vendor][focusrite-autogain]) are
`V_hard` choices in disguise, made for preamp gain-setting on any source, not for a +12 dBu
reference. For an amp sim expecting +12 dBu = 0 dBFS the headroom wording hides the thing the
rack needs — a dBu figure — and UA's own article says not to use Auto-Gain on Hi-Z inputs with
amp plug-ins and to leave the gain at minimum instead ([vendor][ua-autogain]). For NAM's
per-model `input_level_dbu` compensation ([maintainer][nam-cal]) the estimate propagates 1:1: a
± 6 dB sensitivity error hits every model ± 6 dB hot or cold, audible as a gain change, the same
error every competing fallback carries. So: **keep estimating sensitivity**, from `V_hard` per
class, state the band as "± 6 dB — pick your interface from the list for better", and keep the
estimate as a table-shaped value so a later row replaces it.

## A reference-pickup instruction ("use your neck humbucker, volume and tone full")

**(a) Spreads.** The only vendor data that separates neck from bridge under one stimulus is SD's
strum-arm chart ([vendor data][sd-mv]). Passive full-size *neck* models span 382 (Whole Lotta)
to 744 mV (Black Winter), median ≈ 500: 20·log10(744/382) = **5.8 dB, ± 2.9 dB**. Passive
*bridge* models span 399 (Seth Lover) to 1160 mV (Dimebucker), median ≈ 650: **9.3 dB,
± 4.6 dB**. So pinning "neck" removes the hot-bridge tail and tightens the pickup-model term by
about 3.5 dB. The remaining terms are not in any data set: height (SD's 2.4 mm vs a player's
2–3 mm at the neck; "3 dB per mm" [community][sd-height-db]) ≈ ± 2 dB, and the player's
"hard" ≈ ± 3 dB (Tom's factor 2–3 between grades of picking [measurement][toms], halved by
the instruction). Root-sum-square: √(2.9² + 2² + 3²) ≈ **± 4.7 dB for "neck humbucker, knobs
full, hard strum" — INFERRED**, resting on one calibrated neck point (the user's 2.07 V) and
SD's neck ≈ bridge within 1 dB for the same model. Against that: humbucker class, any position,
± 6 dB (data: 1.1–4.1 V); single coil ± 6–7 dB (data: 0.4–1.8 V); active +5/-1.5 dB (rail);
all passives pooled ± 10 dB (0.4–4.1 V, 20 dB); everything pooled ± 10 dB (0.4–4.5 V). The
neck instruction buys about 1.5 dB of band over the class question and, more valuable,
*repeatability*: the same guitar in the same state next time.

**(b) Knobs and selector.** *Volume*: the largest free lever. Audio-taper pots sit near 10 %
resistance at half rotation and "turn down very quickly from 10 to 7" ([community][tdpri-taper];
tapers vary 7–36 % [community][bkp-taper]), so a volume left at "8" costs roughly 3–8 dB
depending on the pot (INFERRED from the taper figures) — more than the whole class question
is worth. *Tone*: the cap shunts the resonant peak; no measurement of its effect on the attack
peak was found; the attack carries much of its energy below the resonance, so expect 1–3 dB
(INFERRED). *Selector*: two pickups in parallel (Les Paul middle, Strat 2/4) read "slightly
lower" and hollow — each pickup is loaded by the other ([community][tdpri-parallel]), about
-2 to -3 dB in phase (INFERRED) and a large drop out of phase; neck vs bridge alone is within
1 dB on SD's rig but -3.5 dB on sound-au's Maton (200 vs 300 mV chord, [measurement][sound-au]),
i.e. setup-dependent. **Pin all three**: "volume and tone all the way up, one pickup selected"
is free, removes up to 8 dB of silent error, and every modeler's manual already says it.

**(c) Options and the band to print.**

| Option | `V_hard` | Honest band for the user guide | Verdict |
|---|---|---|---|
| 1. One `V_hard` for everyone (2 V) | 2.0 V | "within about 10 dB; humbucker players within 6" | Not acceptable: a vintage Strat gets +6 dB, an EMG reads as a loud humbucker; the first real test failed on exactly this |
| 2. Class question (single-coil / humbucker-or-P-90 / active) | 1.0 / 2.0 / 2.1 V | "within about 6 dB" | Sound; three independent scales put the class gap at 5.5–6.4 dB, and actives need it |
| 3. Neck humbucker preferred (knobs full, neck only), else the class question | 2.0 V on the neck path; 1.0 / 2.0 / 2.1 V otherwise | "within about 6 dB" on both paths until a measured neck set exists; ± 4.7 dB INFERRED for the neck path | **Recommended** — the band gain is modest (≈ 1.5 dB) but it standardizes the three biggest free variables and makes the measurement repeatable |

Print "within about 6 dB — choose your interface from the list for better" on both paths of
option 3 until experiment 3 below (neck of several guitars) replaces the INFERRED ± 4.7 dB with
data; do not print ± 4.7 on one data point. Keep the knob-and-selector sentence in every option,
including the class path, since it is the cheapest accuracy available.

## Sanity check of the reported case

2.07 V peak for a hard-strummed "fairly average" passive neck humbucker is the median of the
humbucker band: 1.12 V at normal intensity (RME) and 1.43 V (QC community) below it, 3.59 V ('57
Classic, HX Stomp) and > 4.1 V (498T) above, sound-au's ≥ 1.7 V beside it. Nothing needs
explaining; the -6.3 dB discrepancy (-4.0 measured vs +2.3 table) is 20·log10(2.07/1.0) = 6.3 dB,
exactly the 1 V assumption. Two residuals worth knowing: the table's +14.3 dBu Quad Cortex row is
itself INFERRED from one community-relayed support figure (GENOME says +14.8), so the "truth" the
fallback is compared against is ± 0.5 dB; and the user's P95 sits 1–2 dB under their loudest
strum, so their single hardest attack was about 2.5 V.

## Experiments on the Quad Cortex that add calibrated points

Each is one 10 s "Measure by playing" run, reading the P95 the app logs; convert with
`V = 5.68 × 10^((P95 + level_setting_dB)/20)` for Input 1 at 1 MΩ (level 0 → 5.68 V peak full
scale; at -6 dB the full scale is 11.4 V, which is why the setting must be added back).

1. **Same guitar, input level -6 dB.** The computed volts must repeat (2.07 ± 0.5 V) and the P95
   must drop by 6.0 ± 0.3 dB — this proves the level control is a clean pre-ADC pad and that the
   fallback is linear in it; a different drop means the QC's level control is not where we think.
2. **Five repeats, same guitar, same setting**, to replace the simulated 0.5–0.8 dB run-to-run sd
   with a real one; also one run of single notes only and one of chords only — the chord run
   should read 3–6 dB above the single-note run (sound-au, muzique).
3. **Bridge pickup of the same guitar** (expect within ± 1 dB of the neck per SD's chart; more if
   the bridge is set closer), then **a single-coil guitar** (expect 5–7 dB lower) and **any
   active guitar** (expect a flat-topped P95 near -8.6 dBFS at level 0 = 2.1 V, the rail, nearly
   independent of effort).
4. **Pickup height**: raise or lower the neck humbucker 1 mm and re-run; the community's 3 dB/mm
   rule is unverified and would bound how much of the ± 6 dB is setup rather than pickup.
5. **Software true-peak vs the QC's input meter**: note the QC meter's maximum during the run
   beside the app's P95 — a 1–2 dB gap is expected (P95 under max); more means the QC meter is
   not a sample-peak meter and its community readings above need a correction.

Log each as `guitar / pickup / class / level setting / P95 dBFS / volts` so the next revision of
the table above can carry a measured row from this project instead of a forum's.

[sound-au]: https://sound-au.com/articles/guitar-voltage.htm
[muzique]: http://www.muzique.com/lab/pick.htm
[toms]: http://tomsguitarprojects.blogspot.com/2014/12/electric-guitar-output-voltage-levels.html
[steel]: https://bb.steelguitarforum.com/viewtopic.php?t=372084
[hx-clip]: https://thegearforum.com/threads/hx-stomp-clipping.1889/
[gf-p9]: https://thegearforum.com/threads/calibrating-input-level-for-plugins.816/page-9
[l6-clip]: https://line6.com/support/topic/62132-guitar-input-clipping-hot-pickups/
[fractal-emg]: https://forum.fractalaudio.com/threads/emg-81-internal-preamp-clipping.117773/
[sd-mv]: https://forum.seymourduncan.com/forum/the-pickup-lounge/285455-seymour-duncan-mv-data
[sd-height]: https://seymourduncan.zendesk.com/hc/en-us/articles/35863782111895-What-is-the-recommended-pickup-height-for-Seymour-Duncan-humbucker-pickups
[sd-height-db]: https://forum.seymourduncan.com/forum/the-pickup-lounge/6302959-pickup-height-and-output
[dm-faq]: https://www.dimarzio.com/faq
[dm-sd]: https://www.dimarzio.com/pickups/high-power/super-distortion
[dm-paf]: https://darthphineas.com/2015/11/dimarzio-paf-36th-anniversary/
[dm-area58]: https://www.dimarzio.com/pickups/stacked-hum-canceling-strat/area-58
[dm-tv]: https://www.dimarzio.com/pickups/standard-strat/true-velvet-bridge
[dm-tk]: https://www.dimarzio.com/pickups/standard-tele/twang-king-bridge
[dm-soap]: https://www.dimarzio.com/pickups/p90-soap-bar
[dm-fantom]: https://www.dimarzio.com/pickups/p90-soap-bar/fantom-p90-soapbar
[dm-dlx]: https://www.dimarzio.com/pickups/soap-bar/dlx-plus-bridge
[emg-ds]: https://images.thomann.de/pics/atg/atgdata/document/installation/154102_datasheet.pdf
[emg-wiki]: https://en.wikipedia.org/wiki/EMG_81
[rme]: https://forum.rme-audio.de/viewtopic.php?id=40110
[qc-9916]: https://unity.neuraldsp.com/t/input-gain/9916
[qc-7401]: https://unity.neuraldsp.com/t/what-is-a-proper-input-level-and-how-to-choose-the-right-impedance/7401
[ndsp-unity]: https://unity.neuraldsp.com/t/optimal-input-level-for-highest-accuracy-when-using-ndsp-plugins/11048
[ua-autogain]: https://help.uaudio.com/hc/en-us/articles/30921736610836-Preamp-Auto-Gain
[focusrite-autogain]: https://support.focusrite.com/hc/en-gb/articles/21671741279762-Scarlett-4th-Gen-Multichannel-Auto-Gain
[nam-cal]: https://neural-amp-modeler.readthedocs.io/en/stable/tutorials/calibration.html
[wright]: https://github.com/Alec-Wright/Automated-GuitarAmpModelling
[tdpri-taper]: https://www.tdpri.com/threads/volume-pot-taper-revelation.468889/
[bkp-taper]: https://www.bareknucklepickups.co.uk/news/article/potentiometer-tapers-explained
[tdpri-parallel]: https://www.tdpri.com/threads/middle-pickup-position-volume-drop.505238/
