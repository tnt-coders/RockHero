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

- **ffmpeg path**: pin the Conan recipe to an LGPL-only configuration (no `--enable-gpl`, no GPL
  components — the granular options make this checkable, and the build session records the exact
  component census of the minimal slice). LGPL-2.1 compliance posture for a SHIPPED game:
  dynamic linking is the safe default (the recipe's shared option); static linking imposes
  relink-ability obligations that must be deliberately accepted if chosen. Attribution/notice
  requirements recorded in whatever third-party-licenses surface the game ships.
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
