# OVNI ORBIT 🛸 — free, open-source binaural movement plugin

[![Build & Validate](https://github.com/ovniaudio/orbita/actions/workflows/build_and_test.yml/badge.svg?branch=main)](https://github.com/ovniaudio/orbita/actions/workflows/build_and_test.yml)
[![Version](https://img.shields.io/github/v/tag/ovniaudio/orbita?label=version)](https://github.com/ovniaudio/ovni/releases/latest)
[![License: AGPLv3](https://img.shields.io/github/license/ovniaudio/orbita)](LICENSE)

**ORBIT** is the flagship of the OVNI catalog: a full binaural spatial engine. Place a
sound anywhere in real 3D, set it orbiting, or fly it past with real Doppler — on
headphones or speakers, always with a mono-safe path. Simple interface, pro sound backed
by physics.

**Free and open-source (AGPLv3)** · macOS: VST3 + AU, universal, 11+ · Windows: VST3, x64, 10+.

**→ [Download](https://github.com/ovniaudio/ovni/releases/latest)** — ORBIT's binaries ship
from the OVNI catalog releases: `OVNI-ORBIT-<version>.pkg` (macOS, double-click installer),
`OVNI-ORBIT-<version>-Windows.zip`, or the full-catalog `OVNI-<version>.pkg` with all seven
plugins. This repo carries the source and the version tags; it publishes no binaries of its
own. · **[ovniaudio.com](https://ovniaudio.com)**

---

## What it does

- **The radar** — drag to place the source in 3D: angle and distance, the whole field on one screen.
- **Real Doppler** — pitch emerges from a modulated propagation delay as the source flies close then far. No pitch-shifter.
- **Rate / Speed** — free (0.05–8 Hz), tempo-synced (1/4–1 bar), or fixed placement with FIXED ANGLE.
- **Chaos** — erratic, alien motion: darting speed and azimuth wobble.
- **Width / Room** — ear-shadow width plus decorrelated early reflections that push the sound outside your head.
- **Output / In Phase** — headphones or speakers (crosstalk-cancelled). IN PHASE guarantees mono.

## How it works

ORBIT is the conservative, pure binaural core — no tricks, just acoustics:

- **Variable-position HRIR spatializer** — two bracketing voices crossfade with fractional-delay ITD, so the source moves without clicks.
- **Near-field (Duda–Martens)** — under a metre, bass bloom and asymmetric ear-shadow. The source gets intimate.
- **Externalization** — decorrelated early reflections lower interaural coherence, so sound leaves your head.
- **Speakers (RACE)** — crosstalk cancellation that adds no delay of its own; IN PHASE drops it for guaranteed mono.
- **Clip-safe** — a stereo-linked sample-peak limiter with no lookahead (ceiling 0.85) holds the output; measured true-peak sits around −1.2 dBFS and the image never breaks.
- **Latency** — ORBIT reports **424 samples at 48 kHz (8.83 ms)** and the host compensates it exactly. The figure is constant: it does not change with the DOPPLER knob, and the dry path is delayed by the same amount, so a partial MIX blends two aligned copies instead of comb-filtering against itself. It scales with sample rate (it is the ITD floor plus the propagation line's centre, both in samples).

The baked HRIR derives from **CIPIC subject 003** (see [`NOTICE.md`](NOTICE.md) for the full
dataset attribution). Its interaural time difference is rebuilt from a spherical-head model
(Woodworth, 8.75 cm): CIPIC's own delay field is measured on an interaural-polar grid that does not
survive the conversion to a horizontal ring.

## Install

**macOS:** download `OVNI-ORBIT-<version>.pkg` from the
[latest release](https://github.com/ovniaudio/ovni/releases/latest) and double-click. It
installs VST3 + AU into `/Library/Audio/Plug-Ins` with **no quarantine flag** — no Terminal
needed. The installer itself is unsigned, so macOS may block it once: **right-click → Open**
(macOS 14 or earlier) or **System Settings → Privacy & Security → "Open Anyway"** (macOS 15+).
Installing from the catalog DMG instead? That manual path needs the
`xattr -dr com.apple.quarantine …` step described inside the DMG.

**Windows:** download `OVNI-ORBIT-<version>-Windows.zip`, right-click → **Properties** →
**Unblock** → Apply, then extract `OVNI ORBIT.vst3` into `C:\Program Files\Common Files\VST3`.
SmartScreen may warn the first time — **More info → Run anyway**.

Then rescan plugins in your DAW.

### Upgrading from 0.2.x

Since **0.3.0** the bundles are named **`OVNI ORBIT.vst3` / `OVNI ORBIT.component`** (they used to be
`ORBIT.*`). macOS keeps plug-ins in a case-insensitive folder, so the old name collided with any
other vendor's *Orbit* — the last installer to run overwrote the other one. The new name ends that.

The installer retires the old bundle for you: it removes `ORBIT.vst3` / `ORBIT.component` from
`/Library/Audio/Plug-Ins` and from your own `~/Library/Audio/Plug-Ins`, **but only if the bundle
identifies itself as `com.ovni.orbit`** — someone else's *Orbit* is never touched. Nothing is
deleted: the old bundle is moved to `…/Library/Application Support/OVNI Audio/replaced/<date>/`,
where you can delete it or restore it if you want to go back to 0.2.x.

macOS caches the Audio Unit registry by path, so after upgrading:

```bash
killall -9 AudioComponentRegistrar
```

then rescan plug-ins in your DAW. Your saved projects keep working: the AU identity
(`aufx / Orbt / Ovni`) and the VST3 unique ID are unchanged — only the file name moved.

**Windows:** there is no installer to retire the old bundle, so delete
`C:\Program Files\Common Files\VST3\ORBIT.vst3` by hand before copying `OVNI ORBIT.vst3` in.
If both stay, your DAW sees two plug-ins with the same identity and may show a duplicate or
load the old one.

## Build from source

ORBIT is a JUCE project (Pamplejuce layout) and uses git submodules, so clone with
`--recursive`:

```bash
git clone --recursive https://github.com/ovniaudio/orbita
cd orbita
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
ctest --test-dir build
```

Submodules: **JUCE**, `cmake` (Pamplejuce helpers), `modules/melatonin_inspector`,
`modules/clap-juce-extensions`. Since **v0.1.1** ORBIT links **no Intel IPP and no other
proprietary library** — every platform builds pure JUCE. The release ships **AU + VST3**
(CLAP is disabled).

## License

**GNU AGPLv3** — see [`LICENSE`](LICENSE). Third-party attributions (JUCE, the CIPIC HRIR
dataset, the embedded fonts, and the Pamplejuce template) are in [`NOTICE.md`](NOTICE.md).

ORBIT is the engine the rest of the OVNI catalog is born from. The whole catalog is free and
open-source at **[github.com/ovniaudio](https://github.com/ovniaudio)**.

🛸 **[ovniaudio.com](https://ovniaudio.com)**
