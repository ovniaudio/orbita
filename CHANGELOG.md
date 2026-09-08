# Changelog

All notable changes to [ORBIT](https://github.com/ovniaudio/orbita) are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0/).

## [0.3.0] - 2026-09-07

**The binaural cues are real now.** ORBIT always moved sound around your head, but two of the
three things your ears actually use to locate a sound were wrong under the hood: the time
difference between your ears was a fraction of what a head produces — and for some angles it
pointed to the wrong side — and below 250 Hz the low end leaned toward the *far* ear. Both are
fixed. The plugin also stopped lying to your DAW about its latency. Nothing about the interface
changed, and the sound above 250 Hz keeps its timbre.

The bundle is renamed in this release. See **Changed** before you update.

### Fixed

- **The interaural time difference is real.** This is the dominant cue for placing a sound
  horizontally, and it was the weakest part of ORBIT. It is now rebuilt from a spherical-head
  model (Woodworth, 8.75 cm head): at 90° to one side it measures **656 µs**, which is what a
  head does. Before, the baked delay field carried about **90 µs** — roughly a seventh of it —
  it did not grow properly as the source moved out to the side, and at some angles it pointed
  to the wrong ear. Measured end to end over all 72 directions of the ring: worst deviation
  from the model went from **200 %** (70 of 72 directions out of tolerance) to **0.1 %** (none
  out of tolerance), with **no** wrong-side and **no** non-monotonic angles left.
- **Low bass no longer goes to the wrong ear.** Below **250 Hz** the level difference between
  your ears was inverted: with the source on your left, the low end was louder on the right.
  The measured HRIR simply cannot represent that region, so below 250 Hz ORBIT now imposes the
  level difference of a rigid spherical head (fading to 0 dB at DC, where a head casts no
  shadow). At +45° the 125–250 Hz band went from **−1.8 dB** (wrong side) to **+1.6 dB**.
  **Above 250 Hz nothing moved** — the timbre is the one you already know.
- **ORBIT reports its latency, and the host compensates it exactly.** It reports **424 samples
  at 48 kHz (8.83 ms)**, and the figure is **constant**: it no longer changes with the DOPPLER
  knob. Before, the plugin reported **0** while the wet path actually ran about **0.5 ms** late
  with DOPPLER at 0 and more than **9 ms** late with DOPPLER above 0 (25 and 445 samples at
  48 kHz in the fidelity test; the manual quoted 31 and 453 from an earlier measurement), so the
  DAW could not align it. The dry path is delayed by the same
  amount, so a partial **MIX no longer comb-filters**: the worst notch below 1 kHz at MIX 50 %
  dropped from **4.7 dB** to **0.1 dB**. At MIX 0 the dry output is still bit-exact.
- **The tail is declared.** `getTailLengthSeconds()` returned **0** while the early reflections
  ran for ~95 ms; it now reports **0.104 s** (reflections plus the reported latency), so hosts
  that rely on it stop truncating the last of a bounce or a freeze.
- **`Left Pocket` and `Right Pocket` were swapped.** `Left Pocket` placed the source on your
  right and `Right Pocket` on your left — a naming bug present since the presets shipped.
  Measured balance went from **−10.8 dB** to **+10.8 dB** for `Left Pocket`, and the mirror for
  `Right Pocket`. If you used either one in a saved project, it will now sound on the other
  side; the fix is to load the opposite preset.

### Changed

- **The plug-ins are now named `OVNI ORBIT.vst3` and `OVNI ORBIT.component`** (they were
  `ORBIT.*`). macOS keeps plug-ins in a case-insensitive folder, so our `ORBIT.component`
  and another vendor's `Orbit.component` were literally the same file name: whichever
  installer ran last overwrote the other, and users lost one of the two plug-ins.
  - The installer **retires the old bundle for you**, from `/Library/Audio/Plug-Ins` and from
    the `~/Library/Audio/Plug-Ins` of the user running it — but **only if it identifies itself as
    `com.ovni.orbit`**. Someone else's *Orbit* is never touched. Nothing is deleted: the old
    bundle is moved to `…/Library/Application Support/OVNI Audio/replaced/<date>/`.
  - **After updating, run `killall -9 AudioComponentRegistrar` and rescan in your DAW** —
    macOS caches the Audio Unit registry by path.
  - **Saved projects keep working.** The Audio Unit identity (`aufx / Orbt / Ovni`) and the
    VST3 unique ID are unchanged; only the file name moved.
  - **Windows has no installer to retire the old one:** delete
    `C:\Program Files\Common Files\VST3\ORBIT.vst3` by hand before copying `OVNI ORBIT.vst3`
    in, or your DAW will see two plug-ins with the same identity.

### Notes

- CPU is essentially unchanged: **+0.02 percentage points** on the full path.
- The reported latency scales with the sample rate: it is the ITD floor plus the centre of the
  propagation delay line, both counted in samples.

## [0.2.1] - 2026-09-03

### Fixed

- **The installer could leave you without the Audio Unit.** The bundles inside
  `OVNI-ORBIT-v0.2.0.pkg` still declared version 0.1.1, so macOS Installer compared that
  against what you already had and skipped the component. The packaging step now refuses to
  build a package whose bundles do not declare the version being shipped.

### Added

- The CIPIC copyright notice required by the dataset's terms of use, in
  [`NOTICE.md`](NOTICE.md).

## [0.1.1] - 2026-07-09

### Changed

- **ORBIT links no Intel IPP and no other proprietary library** — every platform builds pure
  JUCE, which is what AGPLv3 requires of the binaries we publish.

## [0.1.0] - 2026-07-08

- First public release: binaural movement engine, AGPLv3, macOS (VST3 + AU) and Windows (VST3).

[0.3.0]: https://github.com/ovniaudio/orbita/compare/v0.2.1...v0.3.0
[0.2.1]: https://github.com/ovniaudio/orbita/compare/v0.1.1...v0.2.1
[0.1.1]: https://github.com/ovniaudio/orbita/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/ovniaudio/orbita/releases/tag/v0.1.0
