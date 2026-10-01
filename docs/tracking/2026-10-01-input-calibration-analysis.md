# Input calibration — analysis snapshot of 2026-10-01

A snapshot, not a registry: step 8 of `docs/plans/completed/input-calibration-simplification.md`,
written once steps 1–7 had landed (`8cd9c55d` … `13e74c2d`). Four read-only fact-gathering passes
traced the measurement, the documented-gain path, every status-to-text authority in both products,
and the repeatability evidence; every claim below was verified against the code before being
recorded. It ends in the decisions for the user; whatever is chosen becomes a plan, and the
calibration plan then moves to completed with a link here.

The one-line verdict: **the system has two reference levels that are not the same quantity, and
the strum measurement estimates the wrong one of them with a human as the signal source.** The
documented-gain table is built on the ecosystem's convention — a 1 V peak sine reads -12 dBFS —
which is a property of the interface. The strum fallback instead levels the player's "moderate"
strum to -12 dBFS, a property of the player, guitar and interface together, with a statistic the
code calls RMS and is not. D8 ruled for the interface; the fallback has not caught up, and that is
why it does not repeat. The fix deletes code.

## (a) What the strum fallback measures

One sample is the true sample peak of the raw input over the window since the previous read:
Tracktion's `LevelMeasurer` in `peakMode`, `getAndClearAudioLevel`, maxed across channels
(`shared/meter_reader.h:77-100`; `tracktion_LevelMeasurer.cpp:98-137`), read from
`WaveInputDevice::levelMeasurer` before the calibration gain and before every plugin
(`engine_live_input.cpp:294-312`). The window is the popup timer's interval, 30 Hz
(`input_calibration_window.cpp:161`).

The capture (`input_calibration.cpp:128-230`) discards 15 samples, waits up to 300 for the first
peak at or above -40 dBFS, then takes exactly 240 more, loud or quiet. Over the active windows
(peak >= -40) it sorts the peak values and computes

```
reference_peak = P90(peaks)
"active_rms"   = power mean of the peak values from P10 to P90      -- an RMS of PEAKS
gain           = clamp±24( quantize0.5( min( -12 - "active_rms", -6 - reference_peak ) ) )
```

So the "-12 dBFS RMS" target of the header, the user doc and the plan is not a signal RMS. It is
the typical window peak of a strummed guitar, which sits 8–15 dB above the signal's true RMS. The
`min()` switches to the peak term whenever P90 exceeds the trimmed mean by more than 6 dB, which a
decaying strum pattern does easily, so many runs are in fact `gain = -6 - P90(peaks)`.

**Variance between runs, ranked by the math** (every peak shifts by +K for a signal K dB hotter,
and both gain terms by -K, exactly):

1. Strum strength. "Moderate" is not a calibrated level; a player repeats it to perhaps ±3–6 dB,
   and the gain follows 1:1. This alone explains the inconsistency D6 records.
2. Which term wins. Two runs with slightly different dynamics can land on different sides of the
   6 dB switch, a discontinuity of several dB that no single-run rule can see.
3. The -40 dBFS activity floor and the fixed 240-window count. Decays and gaps between strums
   are dropped or kept depending on the player's rhythm, moving P10 and the trimmed mean.
4. Pickup output and guitar volume: also K, fixed within a run but deciding the result across
   guitars — by design under the current semantics, and the opposite of what D8 asks for.
5. Timer jitter and the 0.5 dB quantization: negligible. A 33 ms window captures the peak of any
   guitar fundamental regardless of a few milliseconds of jitter.

Nothing compares runs; the 14 dB spread rule is within one run only, and it rejects a wide dynamic
range rather than an unrepeatable one.

## (b) Can the fallback be made repeatable, and what is its bar

Averaging longer, trimming harder, or taking the median of N strums reduces item 3 and nothing
else: items 1 and 4 are systematic per run, not noise. A measurement that asks a human to be a
reference signal cannot repeat better than the human. Two honest options remain:

- **Change the instruction to one humans repeat.** "Strum as hard as you will play" is repeatable
  to roughly ±1–2 dB (maximum effort is a ceiling; "moderate" is a guess), and a ceiling is what
  headroom calibration wants anyway.
- **Change the measurand to the interface.** No strum reveals interface sensitivity without a known
  source, and players have no test gear. So the fallback can only ever estimate it through an
  assumption about the guitar.

The recommended fallback does both: treat the player's hardest strum as roughly a 1 V peak source
(passive pickups span about 0.5–2 V peak, so the assumption is honest to ±6 dB), and estimate the
table's own reference from it:

```
gain = clamp±24( quantize0.5( -12 - P95(active window peaks) ) )      -- "strum hard"
```

This aligns the fallback with the one reference level in (c), replaces "moderate" with "hard",
and deletes the RMS-of-peaks accumulator, the P10 trim, the spread rule and `InputInconsistent`,
the two-target `min()`, and `inputCalibrationTargetRmsDb` (roughly 120 lines of
`input_calibration.{h,cpp}` and their tests). The 15/300/240 windows can shrink: a ceiling needs
about 3 s of hard strums, not 8. The bar to state in the user doc: ±6 dB from the guitar
assumption, ±2 dB run to run — adequate for an amp sim, which tolerates a guitar volume knob, and
far better than today's "moderate" run-to-run spread. Players with a documented interface never
take this path.

## (c) The documented-gain primary path

Today the "?" opens the generated `user_input_calibration.html`; the user finds their row, reads
"+3.1 dB", types it into the slider and presses Apply (`input_calibration_window.cpp:38-114`,
`docs/user/input-calibration.md:36-53`). The table's "Rock Hero gain" column is hand-computed per
row from one formula the doc then fails to state (`:30` ends "is therefore:" with nothing after
it). All sixteen rows check out as `gain = level_at_0dBFS(dBu) - 11.21`, equivalently
`-12 - (1 V peak sine reading in dBFS)`, which is the NAM / Neural DSP calibration convention the
sources cite. That convention is THE reference level of the system; the measurement in (a) should
estimate it and the doc should say it once.

Design, in the order "derived over authored":

- **The data.** A known-interface table in `common/audio` (`input/known_interfaces.{h,cpp}`),
  one authored row per interface: model, unity-input conditions, `level_at_0dbfs_dbu`, basis
  (manufacturer spec / inferred), source. The gain is **derived** by one function from the dBu
  figure and the -12 dBFS reference; no row stores a gain. The user doc's table is generated from
  it or reduced to prose plus the formula — never a second hand-maintained copy of sixteen gains.
- **Phase 1 — the chooser.** The calibration popup gains an "Interface" combo listing the table;
  picking one fills the slider with the derived gain, and Apply commits it through the existing
  `commitManualCalibration`. The pick is the user's (authored); the store keeps exactly what it
  keeps today, `InputCalibrationState{gain, route}` — how the gain was obtained is not state.
  The game's wizard (plan 26 P8) renders the same list from the same table. No name matching is
  needed, so this ships without knowing what ASIO or WASAPI call any device.
- **Phase 2 — preselection, after a sighting.** Whether `AudioDeviceStatus::device_name` (ASIO
  registry description, WASAPI friendly name) or `InputDeviceIdentity::input_device_name` contains
  "Quad Cortex" is unknown; only real hardware can say. If it does, rows gain name patterns and
  the combo preselects the match. Build this only once a device name has been read from real
  hardware; a pattern table authored from guesses would be speculative complexity.
- **Pre-release note.** Because the store keeps the gain, a later correction to a row does not
  reach users who already applied it. Acceptable before the first release; after it, storing the
  interface id beside the gain would let the gain be re-derived — a plan-10 concern, not now.

## (d) Are the targets right

- **-12 dBFS as "1 V peak sine reads -12 dBFS"**: yes. It is the convention NAM captures and
  Neural DSP's own guidance assume, so the rack's amp sims expect it, and it is what the table
  already encodes. State it in exactly those words in the header and the user doc; "-12 dBFS
  average" and "RMS" are false today.
- **-6 dBFS peak**: a headroom ceiling the fallback no longer needs as a second target once the
  measurement is a ceiling estimate; keep it only as the clipping-margin check (reject a run whose
  P95 lands above -6 after the gain would be applied, i.e. a gain below -6 - P95 is impossible by
  construction, so the check collapses to "the run clipped").
- **Detection (plan 22, D4).** The tap is post-input-gain and pre-rack (22:295-296, 478-481) and
  plan 22 sets no absolute dBFS thresholds. Under interface semantics the calibrated domain is
  interface-normalized, not guitar-normalized: a hot pickup still arrives up to 6 dB hotter than a
  weak one, exactly as it would at an amp. Plan 22 must therefore use relative or adaptive
  thresholds (per-session noise floor, onset ratios), never an absolute dBFS gate tuned to one
  guitar. Record this in plan 22 when D4 is picked up.

## (e) Status-to-text: one authority or two

Twelve clusters say one cause in two or three wordings. The three that matter are the live-input
statuses: `MissingCalibration`, `NoInputDevice` and `BackendUnavailable` are the same sentence in
`input_calibration_text.cpp:12-24` and `gameplay_session.cpp:29-55`, a period apart;
`CalibrationStoreUnavailable` has three wordings, and the editor folds it into "backend
unavailable". "Surfaces must not diverge" applies: the cause is one fact.

Shape: one `liveInputStatusText(LiveInputMonitoringStatus) -> std::string_view` in `common/audio`
beside the enum, the only place the seven statuses get words; the editor's projection keeps its
four-way reduction but takes its `disabled_message` from it, and `liveInputOffError` takes its
message from it. Delete `inputCalibrationDisabledMessageFor` and the game's switch. The popup's
own sentence ("Live input stays off until this input is calibrated.") stays: it is a different
surface with a UI-expert-chosen register, not a restatement. The punctuation convention is a
decision below. The remaining clusters (device closed, saved-device-unavailable logs, the invalid
history string in two error enums) are small and can ride the same change.

## (f) `refresh()` idempotence and the watch item

The watch item "The game's live-input status goes stale after Ready" says a `refresh()` at play
would "disable and re-enable monitoring, which would churn the backend". That premise is false,
and I endorsed it without checking: `Engine::Impl::setMonitoringChannelEnabled`
(`engine_live_input.cpp:267-272`) returns without touching the graph when a request leaves both
flags unchanged, and the Active path of `LiveInputMonitor::refresh` (`live_input_monitor.cpp:54-101`)
is a calibration-disable that is already off, a store read, a plugin parameter write
(`Engine::setInputGain :321-336`) and a live-input enable that is already on. A refresh while
Active costs one store read. So the honest gate is one line: `play()`/`restart()` run
`m_live_input_monitor.refresh({.session_ready = true})` before `playRefusal()`, the status is
never stale, and the watch item and plan 26's "device listener" bullet are retired (a listener
remains useful for a live status display, but the gate no longer depends on it). Small; do it.

## Plain defects — fixed with this snapshot

All three were fixed in the commit that added this file.


1. `docs/user/input-calibration.md:30` ends "The manual Rock Hero gain is therefore:" with no
   formula. Add: `Rock Hero gain = device level at 0 dBFS (dBu) − 11.21 dBu`, which is
   `−12 dBFS − (1 V peak sine reading)`. Every row in the table satisfies it.
2. `input_calibration.cpp:142,197`: the `InputClipped` message is written twice in one file. One
   function per error code's words, both sites calling it.
3. `input_calibration.cpp:157,207`: `NoUsableSignal` carries two messages for what the user can
   only act on one way ("not enough signal; strum and try again"). One message, from the same
   function as item 2. (Under decision 1 both sites collapse anyway.)

## Decisions for the user

1. **Rewrite the fallback as a ceiling estimate of the table's reference** — "strum hard",
   `gain = -12 - P95(peaks)`, with the ±6 dB guitar assumption stated in the doc. Deletes the
   RMS-of-peaks, trim, spread and two-target code. Changes the popup's instruction words and the
   rejection set (`InputInconsistent` goes). Medium. **Recommend yes.** The alternative, keeping
   "moderate" and the current math, keeps a fallback that levels the player and cannot repeat.
2. **Known-interface table in code with a popup chooser (Phase 1)**; name-matched preselection
   (Phase 2) only after a device name has been sighted on real hardware. Medium. **Recommend
   yes**, Phase 1 now, Phase 2 parked with its trigger.
3. **One `liveInputStatusText` authority in `common/audio`**, and one punctuation convention for
   user-facing status sentences. **Recommend sentences with a period everywhere**, as the editor's
   tooltips and dialogs already do; the game's messages reach only logs today and will reach a
   surface in plan 26, where a sentence reads correctly. Small-medium.
4. **`refresh()` at play, retire the watch item and the plan-26 listener bullet.** Small.
   **Recommend yes**; it corrects a wrong claim this plan's own step 7 recorded.
5. **Target wording**: "-12 dBFS average/RMS" becomes "a 1 V peak sine reads -12 dBFS" in the
   header, the plan and the user doc. Part of 1; listed so the words change even if 1 is deferred.

## Outcome (2026-10-01)

The user took all five decisions plus the research snapshot's sixth (the reference stated as
"+12 dBu reads 0 dBFS", `docs/tracking/2026-10-01-input-level-calibration-research.md`). The
plan is `docs/plans/in-progress/input-calibration-rework.md`.
