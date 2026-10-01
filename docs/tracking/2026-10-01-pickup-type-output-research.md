# Pickup types and `V_hard` — research snapshot of 2026-10-01

A snapshot, not a registry. Third pass after `2026-10-01-input-level-calibration-research.md`
(the method) and `2026-10-01-hard-strum-peak-research.md` (the constant: humbucker 2 V, single coil
1 V, P-90 2 V inferred, active 2.1 V). Two questions: is there a recognized, data-backed list of
pickup types longer than four, and is voltage the right thing to be measuring at all. Source
strength per claim as before: **measurement**, **vendor**, **community**, **INFERRED**. Volts are
true peaks; `dBu → Vrms = 0.7746 × 10^(dBu/20)`; the reference `+12 dBu = 0 dBFS` is a 3.08 Vrms
sine = **4.36 V peak at 0 dBFS**, so `V_hard` maps to a target of `20·log10(V_hard/4.36)` dBFS.

## The answer

There is no recognized cross-vendor list. Seymour Duncan and DiMarzio each publish an mV figure
per model, but on their own scale (DiMarzio: "relative loudness compared to other DiMarzio
pickups" [vendor][dm-faq]; the one pickup measured on both scales reads 5.4 dB apart), Fender
publishes a 1–5 "vintage/hot" ordinal and DC resistance, Bare Knuckle, TV Jones, EMG and Fishman
publish no output figure that maps to a strum, and no vendor states the force of its test stroke.
What the two mV scales do support is a small set of *between-type offsets*: single coils sit
5–7 dB under PAF-class humbuckers (three independent scales), mini-humbuckers 4–5 dB under
(both vendors agree), P-90s, Filter'Tron-sized and rail pickups sit level with them, stacked
"noiseless" single coils level with vintage single coils, and actives are capped by their supply
rail. Every other distinction a player might name — vintage vs hot humbucker, Strat vs Tele vs
Jazzmaster vs lipstick, 9 V vs 18 V, piezo, bass — is either inside the ±6 dB within-type spread
that height, pots and the player's hand already produce, or has no data behind it at all. The
list that is both supported and self-identifiable has **five** entries: Humbucker 2.0 V,
Single-coil 1.0 V, P-90 2.0 V, Mini-humbucker 1.2 V, Active 2.1 V — P-90 kept separate
because a P-90 *is* a single coil and its owner would otherwise pick the wrong row by 6 dB.
Voltage is the right measurand: an ADC reads `20·log10(V/V_fs)`, dBu is a voltage, and the amp
sims we feed define their reference by a 0 dBFS-peak sine in volts. Loading changes that voltage
by less than the question is worth once the input is an instrument input: 470 kΩ vs 1 MΩ is
under 0.5 dB broadband (measurement), 250 kΩ about 0.5–1 dB on the strum peak (INFERRED, 2–4 dB
only at the 2–5 kHz resonance), while a 27 kΩ or line-level input costs 6 dB (measurement) — so
the guide should say "instrument input, 1 MΩ, straight in, no pedals". Sample rate and
anti-aliasing do not matter: the pickup's own response falls off two octaves below Nyquist.

## Pickup-type list

`V_hard` is the P95 true peak of a hard strum into 1 MΩ, volume and tone full, one pickup
selected. "Vendor-relative" means the offset was read from an mV scale and applied to the 2 V
humbucker median; the arithmetic is in the notes below the table.

| Type | `V_hard` median | Band | Evidence | Basis |
|---|---|---|---|---|
| Passive humbucker (full size, any output tier) | **2.0 V** | 1–4 V | measurement | 6 calibrated points, prior pass |
| Single coil: Strat, Tele | **1.0 V** | 0.4–1.8 V | measurement | 3 calibrated + 2 INFERRED points, prior pass |
| Single coil: Jazzmaster, Jaguar | 1.0 V | as single coil | INFERRED | no mV data from any vendor; SD lists only DCR (7.8k/9k) [vendor][sd-jm]; wide flat coil, "hotter than a Jaguar" [community][reverb-jm] |
| Single coil: lipstick | ~0.8 V | as single coil | INFERRED | "low output and DC resistance" [vendor][sd-lip], DCR 3.7–4.3k [vendor][sd-sls1]; no mV figure anywhere |
| Stacked / noiseless single coil | 1.0 V | as single coil | vendor-relative | DiMarzio Area 58/61/67 120–142, Virtual Vintage 140–145 vs True Velvet 130–142: 0 dB; HS-2/3 at 90–93 are -3.5 dB [vendor][dm-stack] |
| P-90 (soapbar / dog-ear) | **2.0 V** | 1–3.5 V | vendor-relative | Vintage P90 287, Soapbar 270, Fantom 285 vs PAF 36th 250–285: 0 dB; hot P-90s (DLX Plus 380/400, Super Distortion 400) +3 dB [vendor][dm-p90] |
| Mini-humbucker (Firebird, LP Deluxe) | **1.2 V** | 0.6–2.4 V | vendor-relative, two vendors | SD SM-1 324/353 vs '59 572/593 = -4.9/-4.5 dB [vendor data][sd-mv]; DiMarzio Vintage Minibucker 155/180 vs PAF 36th 250–285 = -4.2/-4.0 dB [vendor][dm-mini]; 2.0 × 10^(-4.5/20) = 1.19 V |
| Filter'Tron and 'Tron-sized | 2.0 V | as humbucker | vendor-relative, one vendor, contested | DiMarzio PAF'Tron 200/255, New'Tron 275/290 = -2 to +0 dB vs PAF 36th [vendor][dm-tron]; but vintage-style units are 3.8–4.8k / 1.4–1.7 H [vendor][tvj] against a PAF's ~7.5k, and are called "lower output" [community][wiki-tron]; INFERRED -3 to 0 dB, not separable |
| Rails (single-coil-size humbucker) | 2.0 V | as humbucker | vendor-relative | DiMarzio Chopper 260, Pro Track 275, Tone Zone S 300, Fast Track 2 321 vs PAF 36th 250–285: 0 to +1.5 dB [vendor][dm-rails] |
| High-output humbucker (JB, Distortion, Invader, Dimebucker class) | 3 V | 2–5 V | vendor-relative + 2 community tails | SD JB 737, Custom 784, Distortion 792, Invader 813, Dimebucker 1160 vs '59 580: +2 to +6 dB [vendor data][sd-mv]; 498T "clips heavily" at > 4.1 V and "medium output humbuckers 8–10 V p-p" (prior pass). But the '57 Classic, a *vintage*-output model, read 3.59 V — tiers do not sort the calibrated points |
| Active, 9 V (EMG 81/85, Blackouts, Fluence) | **2.1 V** | 2–2.5 V, rail-capped | measurement (one) | EMG 81 flat-tops at 2.09 V on a Helix (prior pass); EMG: "will never put out more voltage than is being supplied", 9 V "limits the output signal ... to slightly less than 9 Volts", older units "4.5 Volts, one half of the supply" [vendor][emg-faq]; Blackouts 1283/1598 mV and Blackouts Metal 3648 on SD's arm [vendor data][sd-mv] all exceed the rail once scaled (below) |
| Active, 18 V mod | 2.1–4.2 V | unknown | INFERRED | 18 V "doubles the headroom" [vendor][emg-faq]; the 9 V reading was clipped, so the unclipped strum lies somewhere between 2.1 V and the new 4.2 V ceiling; EMG's "Strum 4.50 V" spec [vendor][emg-ds] fits an 18 V rail or a p-p reading; no measurement |
| Fishman Fluence | as active | — | community | no output spec published (2 kΩ output impedance, 9 V, 18 V compatible) [vendor][fluence]; "less output than the EMG 81", Modern set "insane output" [community][ss-fluence]; folds into Active |
| Piezo (acoustic-electric) | none | — | not applicable | a bare element gives "over 1 V peak" easily and some "up to 3.6 V RMS" [measurement][sound-au-piezo]; "max 2.5 V p-p" on one instrument [measurement][groupdiy]; but every acoustic-electric reaches the jack through its own preamp and volume, so the jack voltage is the preamp's gain, not the pickup's |
| Bass, passive (P, J) | ~1.0–1.5 V | unknown | INFERRED | sound-au P-copy 87–203 mV peak, home-made 122 mV, at normal playing, "finger-picked ... approximately double" [measurement][sound-au]; DiMarzio Model P 163, Model J 150, Area J 155 vs PAF 36th 250–285 = -4 to -5 dB on that scale [vendor][dm-bass]; "loudest peaks ... a couple of volts" [community][talkbass]; no calibrated hard-playing point |
| Bass, active (EMG, 18 V preamps) | rail-capped | — | community | "2+ volts, some as much as 4.7 V p-p" [community][talkbass]; most onboard preamps run 9 or 18 V; no measurement |

**Scaling the vendor mV figures.** Seymour Duncan's figures are peaks captured on a storage scope
from a spring-loaded strum arm of unstated force on a Telecaster test bed, pickup at 0.093"
[vendor][sd-method]; DiMarzio's method is unpublished. Fitting the one anchor we have: a
'59-class humbucker reads 572–593 mV on SD's arm and 2 V in the calibrated hard-strum set, so
`k_SD = 2.0 / 0.58 = 3.4` (+10.7 dB). Checks: Dimebucker 1.16 × 3.4 = 3.9 V, matching the > 4.1 V
and "4–5 V" hot-humbucker tails; Blackouts 1.6 × 3.4 = 5.4 V and Blackouts Metal 3.65 × 3.4 =
12 V, both above a 9 V rail — consistent with the measured EMG flat-top, and proof that SD's
stroke is lighter than a hard strum (the arm read the Blackouts unclipped). DiMarzio's scale is
not SD's: the Tone Zone reads 375 mV on DiMarzio's sheet and 700 mV measured beside a JB that
read 721 (SD's 737) [community measurement][sd-mv-thread], i.e. DiMarzio × 1.87 ≈ SD, so
`k_DM ≈ 6.4` (+16 dB). Checks: PAF 36th 0.27 × 6.4 = 1.7 V, Area 58 0.125 × 6.4 = 0.8 V, Model P
0.163 × 6.4 = 1.0 V — all within 2 dB of the calibrated medians. So **yes, a per-vendor factor
turns an mV table into type medians to about ±2 dB**, and no factor carries the ±6 dB within-type
spread, because that spread is height ("3 dB per mm", prior pass), pots and the player, none of
which the mV figure sees. One further caveat: a forum member doubts the "666 mV" Nazgûl row, so
SD's table may carry rounded or symbolic entries [community][sd-ref].

## Recommended dropdown

Order by how often a player will need the row; constants as the code stores them (true-peak
volts; the dBFS target follows from 4.36 V = 0 dBFS).

| # | Item | `V_hard` | Target at +12 dBu = 0 dBFS | Why it is its own row |
|---|---|---|---|---|
| 1 | Humbucker | 2.0 V | -6.8 dBFS | 6 calibrated points; the default |
| 2 | Single-coil | 1.0 V | -12.8 dBFS | 3 calibrated points; 5.5–6.4 dB under row 1 on three independent scales |
| 3 | P-90 | 2.0 V | -6.8 dBFS | same constant as row 1, but a P-90 is a single coil by construction and by name, so without the row its owner picks row 2 and lands 6 dB hot; DiMarzio's scale puts it level with a PAF |
| 4 | Mini-humbucker | 1.2 V | -11.2 dBFS | -4.5 dB vs row 1 on both vendor scales; identifiable by sight and by guitar model (Firebird, Les Paul Deluxe); without it the owner picks row 1 and lands 4.5 dB cold |
| 5 | Active (battery-powered) | 2.1 V | -6.3 dBFS | one measurement, but physics-capped at the 9 V rail, so the band is the tightest of all; the item must exist because an active guitar reads as "a loud humbucker" without it |

Guide text per row, not window text: row 1 also covers Filter'Trons and rail pickups ("Hot
Rails", "Fast Track"); row 2 covers Tele, Jazzmaster, Jaguar, lipstick and noiseless/stacked
pickups; row 5 covers EMG, Blackouts and Fluence at 9 V, and says that an 18 V mod will read
0–6 dB hot until measured (experiment 1 below). Every row keeps the prior pass's sentence:
volume and tone full, one pickup, instrument input at 1 MΩ, no pedals, strum as hard as you
will play. Honest band to print: "within about 6 dB — choose your interface from the list for
better", unchanged.

If the list must stay at four, drop row 4 (mini-humbuckers are the rarest of the five) and
keep P-90 — the P-90 row prevents a 6 dB error on a common guitar, the mini-humbucker row a
4.5 dB error on a rare one.

## Is voltage the right measurement

**dBFS is proportional to input voltage; dBu is a voltage.** 0 dBu is "the RMS voltage that
would dissipate 0 dBm (1 mW) in a 600 Ω load", 0.7746 V, and dBFS is amplitude relative to the
converter's clipping point [reference][wiki-db]. An interface's "+12 dBu at 0 dBFS" therefore
says: a 3.08 Vrms sine, 4.36 V at its crest, reaches the last code; any waveform whose
instantaneous voltage reaches 4.36 V reads 0 dBFS sample peak. The chain guitar → ADC is linear
up to clipping, so the fallback's P95 in dBFS *is* `20·log10(V_P95/4.36)` and the only unknown is
`V_P95` — exactly the quantity `V_hard` names. Confirmed, with one precision: the reference is
defined on a sine's RMS and the strum on a true peak, which is why every conversion in these
notes goes through the crest (× √2) and never through an RMS.

**Loading.** What the interface measures is the *loaded* voltage. A pickup is an inductor
(single coils ~2–3 H, humbuckers 4–9 H; DiMarzio Area 58 2.71 H resonating at 5.02 kHz, SD JB
8.73 H at 2.76 kHz [measurement][guitarhacking]) in series with its DC resistance, driving the
cable capacitance (300–1000 pF [community][heartland]) in parallel with the guitar's pots and
the input resistance. Two effects, very different in size:

- *Broadband divider.* The input resistance divides against the coil's impedance at the
  frequencies that carry the strum peak (the low partials, ~100–400 Hz). JB at 300 Hz:
  `|Z| = √(16.4² + (2π·300·8.73)²) kΩ = √(16.4² + 16.5²) = 23.3 kΩ`. With a Les Paul's pots
  (500k ‖ 500k = 250k) already in parallel, the external 1 MΩ makes 200k, 470k makes 163k,
  250k makes 125k: divider `R/|R + Z|` = -0.71, -0.86, -1.13 dB. **1 MΩ → 250 kΩ moves a
  humbucker's strum peak by 0.4 dB; a Strat (6 kΩ, 2.3 H, three 250k pots) by 0.2 dB.**
  Measurement agrees: "compared with 1 MOhm a 470 kOhm input impedance tames the guitar signal
  very slightly (less than 0.5 dB)" [community measurement][gf-z]; RME: "zero sound difference
  between 1M and 470k" [vendor][rme-z]. At 27 kΩ the same divider predicts
  `27/√(43.4² + 16.5²)` = -4.7 dB at 300 Hz, and sound-au measured "about 6 dB" on a chord peak
  [measurement][sound-au] — the extra 1.3 dB is the mid partials, which a low load hits
  harder. A line-level input (10–20 kΩ) is in this regime: -6 to -9 dB, on top of a different
  sensitivity.
- *Resonance.* The 2–5 kHz peak [reference][lemme] is 0–12 dB high depending on the resistive
  load [reference][lemme-peak], and the external resistance sets its Q: JB with `Z0 = 2π·f0·L` =
  151 kΩ, coil Q 9.2; combined with the 200k / 163k / 125k loads above, Q = 1.16 / 0.97 / 0.76,
  i.e. the peak falls **3.7 dB from 1 MΩ to 250 kΩ** (2.2 dB for the Strat). The same single-coil
  measurement reports "about 2 dB" at the resonance for 470k vs 1M [community measurement][gf-z].

The strum peak is set by the low partials (the 27 kΩ test: 4.7 dB predicted broadband, 6 dB
seen, so the resonance's share of the peak is small), so the net effect of 250 kΩ vs 1 MΩ on
`V_hard` is **INFERRED 0.5–1 dB**, and of 470 kΩ under 0.5 dB. Implications: (1) tell players
to use the instrument/Hi-Z input and, where selectable, 1 MΩ with any "Auto" impedance off —
every calibrated point was taken at 1 MΩ, and it also keeps the tone the amp sim was captured
with; (2) a guitar plugged into a LINE input or through a fuzz-style low-impedance pedal is
outside the model by 6+ dB, so "straight in, no pedals" is a correctness instruction, not
tidiness; (3) cable length is a tone question, not a level one — capacitance moves the
resonance (47 pF → ~8 kHz, 2200 pF → 2.4 kHz for a Strat pickup [reference][lemme]) and leaves
the low partials alone, so a 6 m cable is fine. No measured level data exists for the Helix or
Quad Cortex impedance settings; their threads are tone anecdotes [community][l6-z], [community][qc-z].

**Sample rate and anti-aliasing.** True peak can exceed sample peak between samples; BS.1770
over-samples ×4 at 48 kHz (×2 at 96 kHz) and even then under-reads by up to 0.688 dB for a tone
near half the sample rate [standard][bs1770]. That worst case needs energy near Nyquist. A
magnetic pickup's response peaks at 2–5 kHz and falls 12 dB/octave above it, so at 44.1 kHz the
content is two octaves or more below 22.05 kHz, where the inter-sample error is negligible
(INFERRED < 0.1 dB); the anti-alias filter is flat there. **The sample peak at any rate the
game runs is the true peak for this signal**; no over-sampling, no rate dependence. (The 33 ms
window and P95 statistic were settled in the prior pass and are unaffected.)

**Peak vs RMS vs band-limited.** The amp sim is a voltage-in nonlinear system, and the
calibration it exposes is a peak-voltage definition: NAM's `input_level_dbu` is "the level ...
corresponding to a 1 kHz sine wave with 0 dBFS peak" (prior pass). What the fallback estimates
is the interface's volts-per-dBFS, a single linear factor; any statistic of the player's
waveform estimates it equally well *if* that statistic's expected voltage is known. Peak is the
one whose expected value the calibrated set provides, and the prior pass showed it is also the
tighter one run to run. An RMS or energy measure would need a crest factor (13–19 dB, ±3 dB) on
top; a band-limited measure (A-weighted, or a 1 kHz band) would discard the low partials that
carry the peak and would need its own reference data, of which there is none. **Peak voltage
at the loaded input is the right quantity.**

## Not supportable

- **Humbucker output tiers (vintage / medium / hot).** The vendor offsets are real (+2 to
  +6 dB on SD's arm) but smaller than the within-type spread, the calibrated points do not sort
  by tier (a vintage '57 Classic at 3.59 V above an "average" humbucker at 2.07 V), and a
  player with stock pickups cannot say which tier they own. Fender's 1–5 "vintage/hot" ordinal
  is DCR-based [community transcription][fender-tiers]; Bare Knuckle "aren't rated in output"
  [community][bkp]. A separate row would promise ±2 dB and deliver ±6.
- **Strat vs Tele vs Jazzmaster vs Jaguar vs lipstick.** No vendor publishes an mV figure for
  Jazzmaster, Jaguar or lipstick models; lipstick is "low output" with a 3.7–4.3k coil
  (INFERRED ~2 dB under a Strat). One row at 1.0 V.
- **Noiseless / stacked as its own row.** DiMarzio's stacks sit at 0 dB to its vintage single
  coils (HS-2/3 at -3.5 dB); no second scale. Same row.
- **Filter'Tron and rails as their own rows.** 'Tron-sized DiMarzios are PAF-level, vintage-style
  Filter'Trons are plausibly 3 dB lower by inductance, and no one has measured either into a
  known full scale; rails are +0–1.5 dB on one scale. Both fold into Humbucker with a guide note.
- **Active 18 V.** Bounded (2.1–4.2 V) but unmeasured; a row would be a guess inside a 6 dB
  band. Guide note until experiment 1.
- **Fishman Fluence as its own row.** No output spec; community says under an EMG 81 and
  "insane" for the Modern set — contradictory and unquantified. Active row.
- **Piezo.** The jack voltage belongs to the acoustic guitar's onboard preamp and its volume
  knob, not to a pickup type; `V_hard` has no fixed value. Acoustic-electrics should use the
  interface table or be told the fallback is unreliable for them.
- **Bass.** Passive bass has one normal-playing scope set (87–203 mV, roughly a Strat's level
  at that intensity) and a vendor scale 4–5 dB under a PAF; active basses are rail-capped at
  whatever their preamp runs. When bass ships, two rows (Passive bass ~1.2 V INFERRED, Active
  bass rail-capped) will need their own calibrated points first.

## Experiments on the Quad Cortex

Same recipe as the prior pass: one 10 s "Measure by playing" run, Input 1, read the logged P95,
`V = 5.68 × 10^((P95 + level_dB)/20)` at 1 MΩ, log `guitar / pickup / type / impedance / level /
P95 / volts`. In priority order:

1. **Impedance sweep on the neck humbucker** (the datum behind every table above): 1 MΩ, then
   470k, 250k, 136k, 32k and 10k. Prediction from the arithmetic above: -0.2, -0.5, -1, -3,
   -6 dB (±1) relative to 1 MΩ. This replaces the INFERRED 0.5–1 dB with a measurement and
   decides whether the guide's "1 MΩ" line is a correctness rule or a tone preference.
2. **Any 18 V active guitar**, 1 MΩ: a flat-topped P95 near 4.2 V means the rail is still the
   ceiling and the row is 4.2 V; a soft reading between 2.1 and 4.2 V is the real strum and
   becomes the row.
3. **A P-90 guitar and a mini-humbucker guitar**, if either is reachable: the two
   vendor-relative rows are the weakest in the recommended list; one point each converts them
   to measurements (expect 2 ± 1.5 V and 1.2 ± 0.8 V).
4. **Cable length**: 1 m vs 6 m on the same guitar at 1 MΩ; prediction ≤ 0.3 dB on P95. Confirms
   that the guide need not mention cables.
5. **Line input vs instrument input** on any interface with the switch, same guitar: the
   expected 6–9 dB drop is the number the guide quotes when it says "use the instrument input".

[dm-faq]: https://www.dimarzio.com/faq
[dm-stack]: https://www.dimarzio.com/pickups/stacked-hum-canceling-strat
[dm-p90]: https://www.dimarzio.com/pickups/p90-soap-bar
[dm-mini]: https://www.dimarzio.com/pickups/mini-humbucker
[dm-tron]: https://www.dimarzio.com/pickups/filtertron
[dm-rails]: https://www.dimarzio.com/pickups/rail-hum-canceling-strat/fast-track-2
[dm-bass]: https://www.dimarzio.com/pickups/bass
[sd-mv]: https://forum.seymourduncan.com/forum/the-pickup-lounge/285455-seymour-duncan-mv-data
[sd-method]: https://www.seymourduncan.com/blog/latest-updates/pickup-resistance-vs-output
[sd-ref]: https://forum.seymourduncan.com/threads/sd-mv-pickup-ratings-reference.6149189/
[sd-mv-thread]: https://forum.seymourduncan.com/threads/pickups-and-milli-volts-high-output.311155/
[sd-jm]: https://www.seymourduncan.com/single-product/vintage-jazzmaster
[sd-lip]: https://www.seymourduncan.com/blog/latest-updates/never-mind-the-forums-heres-all-you-need-to-know-about-lipstick-tube-pickups
[sd-sls1]: https://www.thomannmusic.com/seymour_duncan_sls_1_rw_rp_lipstick_chrome.htm
[reverb-jm]: https://reverb.com/guide/jazzmaster-vs-jaguar-whats-the-difference
[tvj]: https://tvjones.com/ray-butts-ful-fidelity-filtertron-bridge-pickups
[wiki-tron]: https://en.wikipedia.org/wiki/Filter%27Tron
[emg-faq]: https://www.emgpickups.com/emg-faq
[emg-ds]: https://www.manua.ls/emg/81/manual
[fluence]: https://fishman.com/dp/fluence-modern-6-string-pickups/
[ss-fluence]: https://sevenstring.org/threads/fishman-fluence-modern-or-emg-81x-60x.307518/
[sound-au]: https://sound-au.com/articles/guitar-voltage.htm
[sound-au-piezo]: https://sound-au.com/project202.htm
[groupdiy]: https://groupdiy.com/threads/tube-piezo-pickup-preamp.29204/
[talkbass]: https://www.talkbass.com/threads/bass-guitar-signal-voltage-output.1179027/
[wiki-db]: https://en.wikipedia.org/wiki/Decibel
[guitarhacking]: https://guitarhacking.net/2021/04/10/on-pickups-and-tone-controls/
[heartland]: https://www.heartlandpickups.com/blog/resonant-peak
[lemme]: http://buildyourguitar.com/resources/lemme/
[lemme-peak]: https://strat-talk.com/threads/some-notes-on-interpreting-pickup-frequency-response-plots.396396/
[gf-z]: https://thegearforum.com/threads/usb-audio-interface-1m-ohm-impedance-vs-470kohm-impedance-differences.10070/
[rme-z]: https://forum.rme-audio.de/viewtopic.php?id=3716
[l6-z]: https://line6.com/support/topic/56280-guitar-in-z-full-clarification/
[qc-z]: https://unity.neuraldsp.com/t/what-is-a-proper-input-level-and-how-to-choose-the-right-impedance/7401
[bs1770]: https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-E.pdf
[fender-tiers]: https://myguitarlair.com/custom-shop-stratocaster-pickups/
[bkp]: https://forum.bareknucklepickups.co.uk/index.php?topic=31562.0
