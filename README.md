# OVNI ORBIT 🛸 — free, open-source binaural movement plugin

**ORBIT** is the flagship of the OVNI catalog: a full binaural spatial engine. Place a
sound anywhere in real 3D, set it orbiting, or fly it past with real Doppler — on
headphones or speakers, always mono-safe. Simple interface, pro sound backed by physics.

**Free and open-source (AGPLv3)** · VST3 + AU · universal for macOS 11+ (Apple Silicon + Intel).

**→ [Download](https://github.com/ovniaudio/ovni/releases/latest)** (ORBIT ships in the OVNI
catalog DMG, with all seven plugins) · **[ovniaudio.com](https://ovniaudio.com)**

---

## What it does

- **The radar** — drag to place the source in 3D: angle and distance, the whole field on one screen.
- **Real Doppler** — pitch emerges from propagation delay as the source flies close then far. Zero latency, no pitch-shifter.
- **Rate / Speed** — free, tempo-synced (1/4–1 bar), or fixed placement. The orbit follows your track.
- **Chaos** — erratic, alien motion: darting speed and azimuth wobble.
- **Width / Room** — ear-shadow width plus decorrelated early reflections that push the sound outside your head.
- **Output / In Phase** — headphones or speakers (crosstalk-cancelled). IN PHASE guarantees mono.

## How it works

ORBIT is the conservative, pure binaural core — no tricks, just acoustics:

- **Variable-position HRIR spatializer** — two bracketing voices crossfade with fractional-delay ITD, so the source moves without clicks.
- **Near-field (Duda–Martens)** — under a metre, bass bloom and asymmetric ear-shadow. The source gets intimate.
- **Externalization** — early reflections lower interaural coherence, so sound leaves your head.
- **Speakers (RACE)** — latency-free crosstalk cancellation; IN PHASE drops it for mono.
- **True-peak safe** — a stereo-linked limiter with a true-peak ceiling never clips and never breaks the image.

The baked HRIR derives from **CIPIC subject 003** (see [`NOTICE.md`](NOTICE.md) for the full
dataset and third-party attribution).

## Install

ORBIT is unsigned. On first use, if macOS Gatekeeper blocks it, clear the quarantine flag:

```bash
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/ORBIT.vst3"
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/ORBIT.component"
```

Then rescan plugins in your DAW. macOS 11+ · Apple Silicon + Intel · Windows soon.

## Build from source

ORBIT is a JUCE project (Pamplejuce layout) and uses git submodules, so clone with
`--recursive`:

```bash
git clone --recursive https://github.com/ovniaudio/orbita
cd orbita
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

Submodules: **JUCE**, `cmake` (Pamplejuce helpers), `modules/melatonin_inspector`,
`modules/clap-juce-extensions`. Intel IPP is optional and accelerates the x86_64 slice only;
the arm64 slice uses pure JUCE. The release ships **AU + VST3** (CLAP is disabled).

## License

**GNU AGPLv3** — see [`LICENSE`](LICENSE). Third-party attributions (JUCE, Intel IPP, the
HRIR dataset, fonts, and the Pamplejuce template) are in [`NOTICE.md`](NOTICE.md).

ORBIT is the engine the rest of the OVNI catalog is born from. The whole catalog is free and
open-source at **[github.com/ovniaudio](https://github.com/ovniaudio)**.

🛸 **[ovniaudio.com](https://ovniaudio.com)**
