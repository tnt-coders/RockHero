# Input level calibration — research snapshot of 2026-10-01

A snapshot, not a registry. Companion to `2026-10-01-input-calibration-analysis.md`: that file
analysed what the code does; this one asks what the rest of the world does, with sources. Source
strength is marked per claim: **vendor** (manufacturer doc or spec), **maintainer** (the project's
own docs or release notes), **community** (forum, relayed support mail, blog), **INFERRED** (my
reading, not found anywhere).

## The answer

There is no better way than a typed or chosen interface sensitivity, because the quantity we want
— how many dBu the interface's instrument input reads at 0 dBFS with its gain at minimum — is
reported by nothing: not the USB Audio Class descriptors, not ASIO, not CoreAudio, not any vendor
control panel, and no shipping amp sim identifies the interface by name. Every serious product
that calibrates input level at all (NAM 0.7.12, TONE3000, Two notes GENOME, Soundshed, the Helix
Native and Neural DSP communities) converged in 2023–2025 on exactly our primary path: the user
finds their interface's dBu-at-0-dBFS figure and enters it, the software derives the gain. The
only real improvement available to that path is in the *data* — the public tables disagree with
each other by 0.5–20 dB on the same devices, so a row's basis (manufacturer spec vs community
measurement vs inference) decides its accuracy, not the method. For players without a row, every
hardware vendor's instruction is the same one the analysis proposed: play as hard as you will play
and set gain from the PEAK (UA Auto-Gain targets -8 dBFS peak, Focusrite Auto Gain "peaks 12 dB
below full scale", Helix/Quad Cortex/Fractal "tickle the red", Kemper "play aggressively until it
stops red-lighting", Soundshed trains a profile from loud playing to -12 dBFS peak). That method
is honest to about ±6 dB across passive guitars and cannot be made more accurate, because the
guitar's own output is the unknown. The gold standard — a 1 kHz tone and a multimeter — exists and
is what the NAM and Helix communities use, but needs test gear no player owns; and no source a
player already owns (a phone's headphone output, a pedal) has a voltage known to better than
±6 dB without identifying its exact model and region.

## Methods, ranked

Accuracy is relative to the one target: the interface's level at 0 dBFS at minimum gain.

### Type A — the interface's sensitivity, authored or looked up

1. **Known-interface table with a chooser** (what we do, minus the typing). Accuracy ±0.5 dB when
   the row is a manufacturer spec at minimum gain and the knob is at minimum: Focusrite's +12.5 dBu
   spec, Neural DSP's measured -13.1 dBFS for 1 Vp (= +12.3 dBu) and GENOME's community +12.3 dB
   agree within 0.3 dB for the Scarlett 3rd Gen ([vendor spec][focusrite-solo3]; [community,
   relaying NDSP support][ndsp-unity]; [vendor table][genome]). Accuracy collapses to ±3–5 dB where
   rows are community or inferred: GENOME lists the Quad Cortex at +14.8, our table +14.3; Audient
   iD4 at +17.5 where Audient's own sheet says "12dBu = 0dBFS" ([vendor][audient-id4]); MOTU M at
   +17.5 vs our +16.0; Behringer UMC at +16.8 vs the Behringer sheet's -3 dBu
   ([vendor][behringer-umc]) — a 20 dB disagreement that only a meter can settle. Effort: one
   pick, knob to minimum. Coverage: ~25 popular interfaces across the public tables; nothing for
   the rest. Cost for us: small; it is Phase 1 of the analysis.
2. **Multimeter and a test tone** — the gold standard every calibration guide describes: play a
   1 kHz sine at 0 dBFS out of the interface, measure RMS volts across tip and sleeve, loop it into
   the instrument input, read dBFS ([maintainer][nam-cal]; [community][gearforum-p1]; [community
   method, simplest variant: raise gain until the tone reads +30 dB to find unity][gearforum-unity]).
   Accuracy ±0.2 dB with any cheap multimeter (a sine needs no true-RMS meter). Effort: a meter, a
   second cable, ten minutes. Coverage: anyone with a meter — i.e. not players. Cost for us: zero
   code; a paragraph in the user doc for the few who will do it, feeding rows back into the table.
3. **Digitally controlled gain as an exact "minimum".** Scarlett 4th Gen preamps are encoders with
   a 69 dB range, so "minimum" is exact rather than a knob stop ([community review][sos-4thgen]);
   the Linux `scarlett2` driver exposes the gain as a mixer control ([maintainer][alsa-scarlett]),
   but Windows has no public API for it. Not a method of its own — it only removes the knob-position
   uncertainty from method 1, and only on Linux could it be read.

### Type B — a known-voltage source the player might own

4. **Phone headphone adapter at full volume.** Apple's US USB-C adapter is a 1.0 Vrms (+2.2 dBu)
   source; the EU variant is 0.5 Vrms by regulation; Google's is 1.88 Vrms; Android drivers may
   impose their own limit ([community measurements][headfi-apple]; [community][archimago];
   [community][xda-apple]). A 6 dB regional spread and a 5.5 dB inter-vendor spread mean the source
   is only known once the player identifies the exact adapter model and disables every volume
   limiter — then INFERRED ±1 dB. Coverage: owners of one of two Apple adapters. Cost for us:
   medium (a tone file the phone plays, a capture of its level, a model picker). Not worth building
   before a sighting proves players would do it; a documented advanced path at most.
5. **Tuner or pedal test tone.** No consumer pedal was found that specifies its tone output
   voltage (INFERRED from the absence of any such claim across the calibration threads above). Not
   viable.
6. **Loopback through the interface's own output** (ruled out in the analysis; confirmed). It
   measures output level minus input sensitivity, so it isolates sensitivity only when the output's
   0 dBFS level is published and not behind the monitor knob — exactly the interfaces that already
   have a published input spec. NAM's own output-calibration recipe assumes a measured send level
   for this reason ([maintainer][nam-cal]). Stays reserved for latency.

### Type C — measure the guitar-plus-interface chain by playing

7. **Hardest-playing peak ceiling** ("strum hard", gain = target − P95 of window peaks). The
   industry's universal fallback, in both hardware auto-gain features and every manual guide: UA
   Preamp Auto-Gain listens 10 s (5 s steps), performer plays "at their loudest", default Peak
   Target -8 dB, with a Listening Threshold ([vendor][ua-autogain]; [community][wlpr-ua]);
   Focusrite Auto Gain listens 10 s and sets gain so wanted audio "peaks 12 dB below full scale"
   ([community review][sos-4thgen]; Focusrite's own page words it as an average of -18 dBFS
   ([vendor][focusrite-autogain]) — the two statements are consistent for a 6 dB crest factor,
   INFERRED); Helix Native's optimal-input indicator is -36 to -12 dB with the meter red above
   ([vendor][helix-pilot]); Quad Cortex "play harder than usual, tickle the red"
   ([community][qc-wiki]); Fractal "ideally no clipping warning; common for a guitar never to hit
   red" ([community][fractal-levels]); Kemper "play aggressively, adjust Clean Sens until it stops
   red-lighting" ([community][kemper-input]); Soundshed "train a profile by playing loudly while
   the app watches your raw input peak", targeting about -12 dBFS peak ([maintainer][soundshed]);
   AmpliTube/Guitar Rig guidance "hardest strums peak around -12 dBFS" ([community][kvr]).
   Accuracy against the interface reference: bounded by the guitar. Measured chord peaks on
   passive pickups run 0.2–0.85 V, single notes 0.05–0.8 V, maximum "about 1 volt peak"
   ([community measurement, oscilloscope][sound-au]); an EMG 81 is 4.5 V peak
   ([community][emg81]). So treating the hardest strum as 1 V peak is ±6 dB across passive guitars
   and ~13 dB off for actives. Run-to-run: no vendor publishes a figure; INFERRED ±1–2 dB for a
   ceiling (maximum effort repeats; "moderate" does not). Capture length: 10 s is the shipping
   norm (UA, Focusrite); a peak statistic converges in a few seconds of hard playing. Effort: 10 s.
   Coverage: everyone. Cost for us: negative — it deletes the RMS-of-peaks, trim, spread and
   two-target code (analysis, decision 1).
8. **Average or RMS targets from playing.** Only Focusrite words its target as an average, and it
   is a preamp-gain setter, not a reference calibration. No product was found that estimates
   interface sensitivity from the RMS of playing, and the analysis already shows why: RMS follows
   the player's dynamics 1:1. Not viable as a reference estimate.
9. **Per-guitar leveling on top of the interface calibration** (Kemper Clean Sens, Fractal
   Input 1 Gain, Quad Cortex per-instrument input level, Soundshed per-profile offset). Every
   hardware modeler separates the two: an A/D level that the unit compensates so it "does NOT
   affect volume, tone or amp gain", and a separate per-instrument gain that does
   ([community, moderator-written][fractal-levels]; [community][kemper-input]). That is the
   interface-vs-guitar split D8 ruled for, and it says the strum fallback is estimating the second
   knob when it should estimate the first.

### Type D — automatic identification

10. **Driver or OS data.** None carries absolute sensitivity. USB Audio Class 2 Feature Units
    expose a Volume Control in dB relative to the device's own 0 dB with CUR/MIN/MAX/RES attributes
    and no reference-level field ([spec][uac2]; [vendor driver doc][ms-uac2]). ASIO's
    `ASIOChannelInfo` carries channel, isInput, isActive, channelGroup, sample type and a 32-char
    name, nothing else ([SDK header][asio-h]). CoreAudio's `kAudioDevicePropertyVolumeDecibels`
    is likewise a control value in dB, not a sensitivity ([vendor][apple-tn2332]).
11. **Identification by name, then a table lookup.** Feasible only through endpoint names: the
    Scarlett 2i2 4th Gen appears as "Scarlett 2i2 4th Gen" in some hosts and as the generic
    "Focusrite USB ASIO" driver in others ([vendor, not fetched — 403; from the search
    summary][focusrite-daw]), so the ASIO driver name cannot distinguish generations and the WASAPI
    friendly name or ASIO channel names must be sighted on hardware before any pattern table is
    authored — the analysis's Phase 2 gate stands.
12. **Crowdsourced or vendor databases.** Three exist, none machine-readable or licensed as data:
    GENOME's 23-row table (5 manufacturer, 18 community, "guidance only and should not be
    considered definitive") ([vendor][genome]); MirrorProfiles' "Interface and Amp Sim Input Level
    Table" spreadsheet distributed with Ghost Note Audio's plugin docs ([community][ghostnote]);
    the 50-page Gear Forum thread the others draw from ([community][gearforum-p20]). We curate our
    own, as today, citing basis per row.

## What the leading products do

- **NAM** (file spec 0.5.4+): a model carries `input_level_dbu`, "the level being input to the
  gear, in dBu, corresponding to a 1 kHz sine wave with 0 dBFS peak"; the plugin's "Calibrate
  Input" (v0.7.12) takes the user's interface dBu at minimum gain and compensates per model; off by
  default; nothing is assumed when a model lacks the field ([maintainer][nam-spec];
  [maintainer][nam-0712]; [community][overdriven]).
- **TONE3000 plugin**: a manual dBu text box, "typical +12 dBu for pro gear, +4 dBu for semi-pro",
  with a gauge icon on calibrated captures; beginners are told to skip it ([maintainer][tone3000]).
- **Two notes GENOME**: a dBu preference defaulting to +10 dBu, "match the maximum input level of
  the audio interface input you are using when its gain is set to 0 dB", with the table above
  ([vendor][genome]).
- **Neural DSP plugins**: developed against a UAD Apollo at minimum gain, +12.2 dBu, i.e. 1 Vp =
  -13 dBFS; the Quad Cortex reads -15.1 dBFS at input level 0, so NDSP advises +2.3 dB before the
  plugin ([community, relaying NDSP support][ndsp-unity]; [community][overdriven]). The Quad
  Cortex itself: "tickle the red", re-set per instrument, no auto level ([community][qc-wiki]).
- **Line 6 Helix / Helix Native**: hardware guitar input is +11.5 dBu full scale with a 5.5 dB
  pad; Native is calibrated by matching a 500 mV RMS tone to the -15.3 dB gate-open point;
  Native's indicator wants -36 to -12 dB ([community][gearforum-p1]; [community][helix-levels];
  [vendor][helix-pilot]).
- **Fractal**: instrument input about +17.5 dBu; A/D Input Level is compensated and tone-neutral;
  Input 1 Gain is the per-guitar trim; no auto level ([community, moderator-written][fractal-levels]).
- **Kemper**: Clean Sens is a per-guitar level the user sets by playing hard against the input
  LED; no auto level ([community][kemper-input]; [vendor FAQ][kemper-faq]).
- **IK TONEX / AmpliTube**: an input trim of ±15 dB with a LOW/OK/HI indicator; the manual tunes
  the default for stock single coils and tells humbucker and active players to lower it; max input
  about +8 dBu on the pedals ([community][tonex-trim]; [community][ik-forum]).
- **Universal Audio Apollo X Gen 2** and **Focusrite Scarlett 4th Gen**: hardware auto-gain from
  10 s of loud playing, peak-targeted (UA -8 dB) or average-targeted (Focusrite -18 dBFS); both
  set the preamp, not a reference ([vendor][ua-autogain]; [vendor][focusrite-autogain];
  [community review][sos-4thgen]).
- **Jam Origin MIDI Guitar**: no auto calibration; users set interface gain for dynamic range and
  touch the plugin's input knob only when the signal is weak ([vendor][jamorigin]).
- **Yousician**: manual — raise the interface gain against the peak light; an input-activity meter
  ([vendor][yousician]).
- **Real-guitar rhythm games**: the best-known one calibrates by prompting "play louder" from a
  fixed-gain USB cable and sets a software gain; its forum reports the routine locking the volume
  at 100 and failing, worked around with a manual "input gain overdrive" offset (community
  forum report; source withheld, since it would name the game). That is a player-leveling auto-gain of the kind in method 7, with
  no interface reference at all.

## Is our reference convention right

Every product above states its reference as **dBu at 0 dBFS peak**, never as "1 V peak reads x
dBFS"; only the Neural DSP thread uses the 1 Vp form, and there as a bridge to the same dBu number.
The ecosystem's de-facto interface is +12 to +12.5 dBu (Apollo +12.2, Scarlett +12.5, Helix
+11.5, TONE3000's "+12 dBu for pro gear"), so 1 Vp lands at -12.3 to -13.3 dBFS; our -12 dBFS
(= +11.21 dBu) is within 1 dB of all of them and about 1 dB hot against Neural DSP's own
reference. The competing convention, "1 V peak = 0 dBFS", is a 2010s SPICE-plugin claim that its
own thread could not source to any manual ([community, disputed in-thread][kvr]); no current
product uses it. NAM is reference-free by design: each model says what it was captured at, and
the host compensates — which is why the one number that matters for a NAM-hosting rack is "what
dBu is 0 dBFS in Rock Hero's calibrated domain", stated as a dBu.

## Recommendation

Keep the measurand (the interface) and both paths; change the data and the words, and delete the
fallback's statistics, as decisions 1 and 2 of the analysis already propose. Specifically:

1. **Primary: the chooser over a code-owned table, with per-row basis as a first-class field.**
   Rows from a manufacturer spec at minimum gain are trustworthy to ±0.5 dB; community and
   inferred rows are not, and two of ours disagree with a vendor table by 1.5–20 dB. Prefer the
   spec, mark the rest, and let the popup show the basis so the player knows when to distrust it.
   Nothing to auto-detect; Phase 2 stays gated on a sighted device-name string.
2. **State the reference as a dBu figure: "0 dBFS in Rock Hero is +12 dBu."** It replaces the
   derived 11.21 with the round number TONE3000, GENOME's "pro" bracket and Neural DSP's +12.2
   all approximate, moves every row's gain by -0.8 dB (within the tolerance of the method), makes
   the Scarlett 3rd Gen +0.5 dB and an Apollo +0.2 dB, and is the exact value a NAM "Calibrate
   Input" box would take downstream of our gain. A user decision, since the analysis just ruled
   for the -12 dBFS wording; the net tonal effect is under 1 dB either way.
3. **Fallback: the hardest-playing peak ceiling, exactly as decision 1,** plus two things the
   shipping auto-gains have and ours lacks: a listening threshold (ignore windows below a floor —
   we have -40 dBFS, keep it) and a fixed 10 s listen (drop the "wait up to 300 windows for the
   first peak" shape for "listen N seconds from the first active window"). Say in the doc what UA
   and Focusrite say: play as hard as you will play. State the bar as ±6 dB across passive
   guitars, actives land hot, ±2 dB run to run.
4. **Delete:** `inputCalibrationTargetRmsDb`, the RMS-of-peaks accumulator, the P10 trim, the
   spread rule and `InputInconsistent`, the two-target `min()`, the "moderate" instruction and the
   "-12 dBFS average / RMS" wording — the analysis's list, now with the industry's method behind
   it. Do not build the phone-adapter or loopback paths for gain.
5. **Document the multimeter method** in two sentences for the players who have one, as the way to
   contribute a row. It costs no code and is the only path to a measured row for an unlisted
   interface.

## Open questions only hardware can settle

- The Behringer UMC202HD/204HD instrument input: the spec sheet's -3 dBu would clip on a hot
  humbucker, GENOME's community figure is +16.8 dBu. One meter reading decides a 20 dB row.
- MOTU M-series (+16.0 spec in our table vs +17.5 "manufacturer" in GENOME's) and Quad Cortex
  (+14.3 inferred vs +14.8 community): 0.5–1.5 dB, resolvable only by measurement.
- What `AudioDeviceStatus::device_name` and the ASIO channel names actually read for a Scarlett
  and a Quad Cortex under ASIO and WASAPI — the Phase 2 gate.
- The run-to-run spread of "strum hard, P95 peak" on real hardware: five runs each on a single
  coil, a humbucker and an active guitar through one interface, to replace the INFERRED ±2 dB.
- Whether the Windows shared-mode volume or "microphone boost" on class-driver devices (the
  fixed-gain USB cables) shifts the raw level under WASAPI, which would make a table row for such
  a cable meaningless and the fallback mandatory there.

[nam-cal]: https://neural-amp-modeler.readthedocs.io/en/stable/tutorials/calibration.html
[nam-spec]: https://neural-amp-modeler.readthedocs.io/en/stable/model-file.html
[nam-0712]: https://www.neuralampmodeler.com/post/neuralampmodelerplugin-v0-7-12-is-released
[tone3000]: https://www.tone3000.com/guides/tone3000-plugin
[genome]: https://helpdesk.two-notes.com/portal/en/kb/articles/calibrating-genome-s-input-your-audio-interface
[ndsp-unity]: https://unity.neuraldsp.com/t/optimal-input-level-for-highest-accuracy-when-using-ndsp-plugins/11048
[overdriven]: https://overdriven.fr/overdriven/index.php/2025/08/17/using-nam-part-3/
[gearforum-p1]: https://thegearforum.com/threads/calibrating-input-level-for-plugins.816/
[gearforum-p20]: https://thegearforum.com/threads/calibrating-input-level-for-plugins.816/page-20
[gearforum-unity]: https://thegearforum.com/threads/nam-calibration-find-audio-interface-unity-gain-level.9425/
[ghostnote]: https://ghostnoteaudio.uk/products/transistor-legacy-plugin
[soundshed]: https://guitar.soundshed.com/docs/gain-levelling-guide
[helix-pilot]: https://line6.com/data/6/0a020a410ad46078912adaa0e/application/pdf/Helix%20Native%20Pilot's%20Guide%20-%20English%20.pdf
[helix-levels]: https://line6.com/support/topic/48167-helix-input-and-output-levels-research-almost-solved/
[fractal-levels]: https://forum.fractalaudio.com/threads/everything-youve-always-wanted-to-know-about-levels-iii-fm3-fm9.168584/
[kemper-input]: https://forum.kemper-amps.com/forum/thread/27577-how-do-you-optimize-the-input-level/
[kemper-faq]: https://www.kemper-amps.com/faqs
[qc-wiki]: https://quadcortex.wiki/Setting_the_Input_Level_for_Your_Instrument
[tonex-trim]: https://www.komposition101.com/blog/how-to-set-the-correct-input-trim-on-tonex
[ik-forum]: https://forum.ikmultimedia.com/viewtopic.php?f=5&t=33878
[kvr]: https://www.kvraudio.com/forum/viewtopic.php?t=482600
[jamorigin]: https://www.jamorigin.com/docs/midi-guitar-for-ios/
[yousician]: https://support.yousician.com/hc/en-us/articles/202194932-Using-an-audio-interface-with-Yousician
[ua-autogain]: https://help.uaudio.com/hc/en-us/articles/30921736610836-Preamp-Auto-Gain
[wlpr-ua]: https://whylogicprorules.com/ua-apollo-x-getting-started-pt-2/
[focusrite-autogain]: https://support.focusrite.com/hc/en-gb/articles/21671741279762-Scarlett-4th-Gen-Multichannel-Auto-Gain
[sos-4thgen]: https://www.soundonsound.com/reviews/focusrite-scarlett-4th-gen
[focusrite-solo3]: https://userguides.focusrite.com/hc/en-gb/articles/23031457381138-Scarlett-Solo-3rd-Gen-specifications
[focusrite-daw]: https://userguides.focusrite.com/hc/en-gb/articles/19640385719570-Setting-up-your-DAW-Recording-Software-with-your-Scarlett-2i2
[alsa-scarlett]: https://github.com/geoffreybennett/alsa-scarlett-gui
[audient-id4]: https://audient.com/products/audio-interfaces/id4/tech-specs/
[behringer-umc]: https://www.manualowl.com/m/Behringer/UMC202HD/Manual/461898?page=21
[sound-au]: https://sound-au.com/articles/guitar-voltage.htm
[emg81]: https://en.wikipedia.org/wiki/EMG_81
[headfi-apple]: https://www.head-fi.org/showcase/apple-usb-c-to-3-5-mm-headphone-jack-adapter.23420/reviews
[archimago]: http://archimago.blogspot.com/2018/01/measurements-apples-lightning-to-35mm.html
[xda-apple]: https://xdaforums.com/t/apple-usb-c-to-3-5mm-dac-dongle-and-android-remove-the-power-limit.3917495/
[uac2]: https://www.usb.org/sites/default/files/Audio2_with_Errata_and_ECN_through_Apr_2_2025.pdf
[ms-uac2]: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers
[asio-h]: https://ccrma.stanford.edu/workshops/dsp2008/prc/Week1Labs/stk-4.1.3/src/asio/asio.h
[apple-tn2332]: https://developer.apple.com/library/archive/technotes/tn2332/_index.html
