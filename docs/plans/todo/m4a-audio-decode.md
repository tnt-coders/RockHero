# AAC/.m4a Backing-Audio Decode

The plan to make `.gp` imports with AAC/`.m4a` backing audio decode for real. User-ruled
2026-09-02: this WILL be built — "it definitely CAN be decoded on Windows" — a new external
dependency is authorized if needed, and the long-term solution should preferably work on Linux
too. Until it lands, the import refuses loudly and names the format (the honest-refusal change of
the same date), and `canDecodeAudioExtension` is the one gate — when a decoder registers, the
refusal widens itself out of the way with no importer change.

## Current state (analyzed 2026-09-02, source-verified)

- JUCE 8.0.12 ships NO AAC/MP4 reader for Windows or Linux: `CoreAudioFormat` (the only m4a
  reader in the tree) is Apple-only, and `WindowsMediaAudioFormat` is the legacy `wmvcore`
  ASF/WMA reader, which cannot open an MP4 container regardless of extension lists. Tracktion
  adds no decoder (its ffmpeg/LAME formats are encode-only and off by default). No Conan
  dependency decodes AAC. VERIFIED UPSTREAM TOO (2026-09-02): AAC-on-Windows has been an open
  JUCE feature request across a decade of forum threads and was never shipped in any JUCE
  version — a submodule upgrade cannot provide it; JUCE's policy leans on OS decoders, which
  works on Apple and was never modernized past the ASF-era SDK on Windows.
- macOS already imports m4a through `CoreAudioFormat`; Windows and Linux refuse.
- Corpus impact at analysis time: 7 of 115 `.gp` files carry `.m4a` backing tracks (~6%), plus
  4 on `.mp3` riding the untested `wmvcore` path.
- The transcode seam (`rock-hero-common/audio/src/song/audio_transcode.cpp`, the reader-creation
  site) is the ONLY place the app decodes non-FLAC audio — the package reader enforces FLAC and
  normalization runs post-import on the FLAC — so the decoder integrates at exactly one point.

## Cost/benefit (user question 2026-09-02): what the dependency actually buys

**For GP import alone, ffmpeg is oversized.** Guitar Pro embeds what the charter fed it — in
practice mp3, m4a, or wav (corpus: 102 wav / 7 m4a / 4 mp3, nothing else) — so the whole GP-side
gap is m4a plus making mp3 reliable (today it rides the untested legacy `wmvcore` reader on
Windows and does not decode on Linux at all). If GP import were the only consumer, the MF seam or
hand-conversion would compete.

**For from-scratch authoring — the stated future — the question flips.** The input becomes
whatever lives in a user's music folder. The coverage matrix that decides this (decode-side,
after the JUCE mp3 flag co-move below):

| Format | Today Win/Linux | Today macOS | ffmpeg-minimal | small-libs patchwork |
|---|---|---|---|---|
| wav / aiff / flac / vorbis | yes | yes | yes | yes (already) |
| mp3 | flag-gated / absent | yes | yes | yes (JUCE flag — patents expired 2017) |
| AAC / .m4a | NO | yes | yes | Windows-only via MF seam; **no Linux answer** |
| ALAC (.m4a) | NO | yes | yes | tiny Apache-2 decoder exists but needs the SAME ISO-BMFF demuxer — **no clean small answer** |
| Opus | NO (JUCE's Ogg reader is Vorbis-only) | NO | yes | opus/opusfile (BSD, small, Conan) |
| wma | legacy Windows reader | no | yes | Windows-only |

**The recurring failure of every non-ffmpeg path is the ISO-BMFF (MP4/M4A) demuxer**: AAC and
ALAC both live in that container, no good small standalone demuxer exists on Conan, and the MF
seam only answers it on Windows. Every patchwork ends with either a Linux hole or a hand-rolled
container parser — the exact hand-rolled-known-algorithm the project's own rules forbid.

**Alternatives considered, with verdicts:**
- **ffmpeg-minimal (Conan, decode-only slice)** — closes the entire matrix, one seam, one code
  path, all three CI platforms. Runtime cost: a few MB of linked libs (the "huge" reputation is
  the full build; `avformat`+`avcodec`+`swresample` with named decoders/demuxers only, no
  encoders/filters/devices/video). Real costs: a long first Conan build per configuration
  (cached after) on every CI platform, and the license-gate work. **The only single move that
  finishes the import problem.**
- **Small-libs patchwork** (JUCE mp3 flag + MF seam + opusfile + ALAC ref decoder) — three to
  four new seams, N licenses, N priming behaviours to verify separately, and STILL no AAC/ALAC
  on Linux. Rejected as an end state; its one free piece (the mp3 flag) is adopted below.
- **GStreamer** — cross-platform but a runtime plugin framework, operationally heavier than the
  thing it replaces. Rejected.
- **Per-OS decoders** (MF + CoreAudio + something-on-Linux) — three code paths for one concern,
  against the Minimize-Platform-Specific-Code rule, and Linux ends up needing a library anyway.
  Rejected.
- **libsndfile / SDL_mixer / dr_libs** — each covers a subset; none decodes AAC on Win/Linux.
  Rejected as the primary; nothing they add is missing from ffmpeg.
- **Stay at the refusal** (convert by hand) — the shipped floor; permanent Linux posture only if
  the authoring future is abandoned. Kept as the fallback of record, not the plan.

**Free co-move — DONE 2026-09-02**: `JUCE_USE_MP3AUDIOFORMAT=1` enabled
(cmake/RockHeroExternalModules.cmake) — JUCE's in-tree software mp3 decoder, patents expired
2017 — giving one deterministic cross-platform mp3 decode (it registers ahead of the legacy
Windows Media reader, retiring the untested wmvcore path for .mp3, and Linux gains mp3 it
never had). What the big decision must justify shrinks to the MP4 family plus Opus.

**Conclusion**: justified by the authoring-audio import matrix, not by the one format; the m4a
bug only moves the schedule up. The decision itself stays user-signed (roadmap M4A-Q1) behind
the license and priming gates.

## Candidates, in preference order

1. **A cross-platform decoder dependency via Conan** — the preferred shape per the Linux ruling:
   one seam, no OS guards, identical behavior on all three platforms, and CI exercises it
   everywhere. Verified 2026-09-02 (web, Conan Center):
   - **ffmpeg/8.1.1 is on Conan Center and is the front-runner**: libavformat demuxes MP4/M4A
     (and honors edit lists, the priming-trim mechanism) and libavcodec's NATIVE AAC decoder is
     LGPL — no fdk needed — while the recipe exposes granular options (component toggles plus
     disable-everything/enable-decoder/enable-demuxer style knobs) to build a minimal
     decode-only LGPL slice. One dependency also settles `.mp3` for good, retiring the untested
     `wmvcore` path.
   - libfdk_aac/2.0.3 exists on Conan Center but decodes raw AAC only — the ISO-BMFF demuxer
     gap has no clean Conan answer, and the FDK license carries the no-patent-grant quirk.
     Weaker fit.
   - faad2 is GPL-2: disqualifying for shipping.
   - patent posture: core AAC-LC patents have been expiring through the mid-2020s — verify the
     current state during the session rather than assuming either way.
2. **Windows Media Foundation seam** — the fallback if no acceptable library exists: an OS API,
   zero new dependencies, ~300 lines behind one `JUCE_WINDOWS` guard (`MFSourceReader` -> PCM
   16-bit -> the existing FLAC writer), fully designed in the 2026-09-02 analysis (task #154's
   record) including the clang-cl/CI consequences. Cost: Linux keeps refusing, and the platform
   guard the Minimize-Platform-Specific-Code rule tolerates only with a stated why.

## THE LICENSE GATE (user, 2026-09-02: "License must be considered as well")

License clearance is a SIGNING CONDITION equal to the priming gate — no decoder ships without it:

- **ffmpeg path — VERIFIED COMPATIBLE 2026-09-02**: Rock Hero is AGPL-3.0 (LICENSE, README),
  and ffmpeg's core build is LGPL-2.1-or-later — the easy direction of the compatibility
  matrix; incorporating LGPL libraries into an AGPL work is expressly permitted, and every
  decoder/demuxer this plan needs (native AAC, MP4/MOV demux, mp3, Opus, ALAC, Vorbis, FLAC)
  lives in the LGPL core with no `--enable-gpl` or `--enable-nonfree` switch. The earlier
  dynamic-linking caution was written for a closed-source shipper and is MOOT here: the
  relink-ability concern protects users of proprietary binaries, and this project publishes its
  source — static or dynamic both fine. Remaining obligations, all light: pin the Conan config
  LGPL-only (checkable via the recipe options; record the component census), carry ffmpeg's
  license texts and copyright notices in the distribution, and note the LGPL parts remain LGPL.
  Consistent with the stack already shipped on (JUCE free tier AGPL-3.0, Tracktion GPLv3).
- **fdk-aac**: the FDK license permits use but grants NO patent license explicitly — a real
  diligence item, not a footnote — one of the reasons it sits below ffmpeg.
- **faad2**: GPL-2, disqualifying for shipping. Stays excluded.
- **Patents**: verify the AAC-LC decode patent posture at build time (core patents expiring
  through the mid-2020s); record the conclusion and its date in this plan when checked.
- The final license conclusion is USER-SIGNED before the dependency enters conanfile.txt.

## THE GATE, whichever candidate wins: priming-delay correctness

AAC carries an encoder delay (commonly 2112 frames, ~48 ms at 44.1 kHz) that a correct decoder
trims via the MP4 edit list. The import is frame-exact by design (`FramePadding` applies in fixed
44.1 kHz frames), so an untrimmed priming head would desync every m4a song by a uniform audible
offset. Before the chosen decoder ships: decode one corpus m4a, cross-correlate the head against
a reference decode (e.g. a hand-converted WAV of the same file), and prove the offset is zero —
or compensate in the seam and prove the compensation. This is an empirical check, not a review.

## Integration shape

- The decoder registers as (or is wrapped into) a `juce::AudioFormat`/reader behind the ONE
  transcode seam; `canDecodeAudioExtension` then answers true and the import refusal retires
  itself for the format with no importer edit.
- The refusal's platform-split tests flip with it (the Windows/Linux `.m4a` expectations in
  `test_audio_transcode.cpp` and `test_gp_song_importer.cpp` are marked as this plan's to-flip
  sites).
- Same session: settle the mp3 question — import one of the mp3-backed corpus files to learn
  whether the legacy `wmvcore` path actually works, and fold `.mp3` into whatever decoder ships
  if it does not.
- A small checked-in `.m4a` test fixture (self-authored audio, never corpus material) beside the
  existing `drum_loop.wav` precedent.

## CI consequences (from the analysis)

A dependency path exercises one code path on all three CI platforms — the cleanest outcome. An
MF path compiles the guarded body only under clang-cl (Windows lint) and MSVC, needs unused-
parameter hygiene in the `#else` branch under GCC/Clang `-Werror`, and keeps Windows headers out
of public headers per the existing convention.
