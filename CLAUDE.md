# CLAUDE.md

Offline converter: higher-order ambisonics (AmbiX, ACN order, SN3D or N3D, 1st–7th order, up to
64 ch) rendered from Reaper → Dolby Atmos ADM BWF that Logic Pro imports (File ▸ Import ▸ ADM BWF).
Three targets share one core: the GUI app `HOAtoAtmos` ("HOA to Atmos.app"), the CLI `hoa2adm`,
and the tests `hoa_tests`.

## Why it works this way (settled decisions)

- **Offline, not a plugin:** Logic tracks max out at 7.1.4 (12 ch) and Logic's own ambisonics stops
  at 3rd order, so 64 channels can't be routed into Logic. A Logic AU that streams the HOA file
  from disk was considered as a possible later addition; it wasn't built.
- **7.1 bed + static objects, not a 7.1.4 bed:** Dolby Atmos beds stop at 7.1.2. Ear-level 7.1
  (+ LFE) is the bed. The heights and all extra positions are static objects acting as virtual
  speakers. Presets: `7.1.4` (12 tracks), `extended` (default, 20), `dense` (38). `--no-bed`
  makes everything objects.
- **Decoder:** AllRAD. Decode to 5200 Fibonacci-sphere directions, then energy-normalised VBAP
  onto a brute-force convex hull. Imaginary nadir always (folded into the ear-level ring, or
  dropped); imaginary zenith if the layout has none (folded into the top layer). max-rE by default.
  Normalised so a plane wave averaged over the sphere has unit energy.
- **Room-cube mapping (`polarToCube`):** piecewise-linear on the square perimeter, anchored so the
  7.1.4 reference lands on standard spots: ±30 → front corners (the front corner angle moves from
  30° at el 0 to 45° at el 45), ±90 → side walls, ±135 → rear corners, el 45 → ceiling edge,
  el > 45 → moves in across the ceiling, zenith → (0,0,1). Below the horizon is clamped to z = 0.
  ADM cartesian: X + right, Y + front, Z + up; ambisonic azimuth + = left, so left means X < 0.
- **ADM writer (own code, not JUCE):** 24-bit PCM, chunks are JUNK/ds64, fmt, chna, axml, data.
  Switches to RF64 above 4 GB. IDs are custom (pack/channel IDs from 0x1001; bed type 0001, objects
  type 0003). Bed speaker labels follow Dolby's `RC_*` style with cartesian positions. One
  audioObject per object, one bed object.
- Only 48 or 96 kHz input is accepted (Atmos requirement); there is no resampler. Two passes: the
  first finds the peak, then auto-trim to −0.1 dBFS if the decode would clip.

## Status / open items

- **Logic Pro import is untested.** The output parses and renders with the EBU ADM Renderer
  (`ear-render -s 4+7+0 --strict`), and sources land in the right places. EAR warns that it
  doesn't recognise `RC_LFE` as an LFE label; that's expected. If Logic rejects a file, likely
  suspects are the custom (non-common-definition) bed IDs, `<cartesian>` inside DirectSpeakers
  blocks, or the LFE label. Fallback for the user: `--stems <dir>` writes mono WAVs +
  `positions.txt`.
- Measured at 7th order, `extended`: energy-vector direction error 3.6° mean, rE 0.93, loudness
  within ±1.1 dB (there's a dip at el 10–35 between the ear-level and height layers; normal for
  AllRAD here). Test thresholds are set per layout in `Tests/CoreTests.cpp`.

## Build / test

JUCE 8.0.4 comes from `../beads-au-plugin/third_party/JUCE` (override with `-DJUCE_DIR=`). JUCE
modules are compiled into each target that links them, so the core sources (`HOA_CORE_SOURCES`)
are added to each target; don't turn them into a shared static library.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release        # universal by default; add -DCMAKE_OSX_ARCHITECTURES=arm64 for faster builds
cmake --build build -j
./build/hoa_tests_artefacts/Release/hoa_tests    # exit 0 == pass; writes test WAVs to ~/Library/Caches/hoa_tests/
./build/hoa2adm_artefacts/Release/hoa2adm in.wav --info
```

The GUI target is named `HOAtoAtmos`, not `HOA2ADM`, because on the case-insensitive filesystem
its build folder collided with the CLI target `hoa2adm`.

## Layout

- `Source/Core/` – `SphericalHarmonics`, `AtmosLayout` (presets + cube mapping), `AllRadDecoder`,
  `AdmBwfWriter`, `Converter` (JUCE reader → decode → LFE/trim → writer/stems)
- `Source/App/` – JUCE GUI (drag & drop, top-down layout view, background worker thread)
- `Source/Cli/Main.cpp` – `hoa2adm` argument parsing
- `Tests/CoreTests.cpp` – self-checking tests, including an end-to-end 64-ch conversion
