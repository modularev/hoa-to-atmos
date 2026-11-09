# HOA → Atmos (hoa2adm)

Turns a higher-order ambisonic mix (AmbiX, 1st–7th order, up to 64 channels) rendered
from Reaper into a **Dolby Atmos ADM BWF** that Logic Pro opens as an Atmos project.

```
Reaper (HOA bus, ≤ 7th order)  ──render 64-ch WAV @ 48 kHz──▶  hoa2adm  ──ADM BWF──▶  Logic Pro
                                                               (AllRAD)               File ▸ Import ▸ ADM BWF
```

Why offline: Logic tracks are at most 7.1.4 (12 ch) and Logic's own ambisonics stops at
3rd order, so a 64-channel signal can't be routed into Logic. The decode happens here and
Logic receives ordinary Atmos bed + object tracks.

## Output layouts

Dolby Atmos beds stop at 7.1.2, so the ear-level 7.1 goes into a **bed** and every
height or extra position becomes a **static object** acting as a virtual loudspeaker.

| preset      | tracks | contents |
|-------------|-------:|----------|
| `7.1.4`     | 12 | 7.1 bed + Ltf/Rtf/Ltr/Rtr objects |
| `extended` (default) | 20 | + wides M±60, rear centre, top front/side/rear centres, zenith |
| `dense`     | 38 | + ear-level fill-ins (±15, ±112.5, ±157.5), mid-height ring (el 22.5), upper ring (el 67.5) |

Use `extended` or `dense` for 7th-order material: they keep more spatial resolution, and the
Atmos renderer scales them down to 7.1.4 and smaller rooms while using bigger rooms fully.
`--no-bed` writes the ear-level speakers as objects too (no LFE).

Positions are mapped into the Atmos room cube so that the 7.1.4 reference speakers land on
their standard spots (L/R ±30 → front corners, Lss ±90 → side walls, Lrs ±135 → rear
corners, tops ±45/±135 el 45 → ceiling corners, zenith → ceiling centre).

## Decoder

* AllRAD (Zotter & Frank): decode to 5200 near-uniform directions, VBAP those onto the
  virtual speakers.
* max-rE weighting (default), or `--basic`.
* Imaginary speakers: the nadir is folded into the ear-level ring (default) or dropped
  (`--discard-below`). If a layout has no zenith speaker, an imaginary one is folded into the top layer.
* Input normalisation SN3D (AmbiX, default) or N3D, ACN channel order.
* `--yaw` rotates the scene. `--lfe <dB>` feeds a 120 Hz LR4 low-passed W into the bed LFE (off by default).
* Two passes: the first finds the decoded peak, and if it would clip, everything is trimmed to −0.1 dBFS.

7th order, `extended`: energy-vector error ≈ 3.6° mean, rE ≈ 0.93, loudness within ±1.1 dB over the upper hemisphere.

## Build

Uses the JUCE checkout in `../beads-au-plugin/third_party/JUCE` (override with `-DJUCE_DIR=`).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/hoa_tests_artefacts/Release/hoa_tests          # self-checking tests
```

Artefacts:
* `build/HOAtoAtmos_artefacts/Release/HOA to Atmos.app`: GUI (drag-and-drop, layout preview)
* `build/hoa2adm_artefacts/Release/hoa2adm`: CLI

## Usage

In Reaper: render the HOA master (e.g. IEM/SPARTA bus, 64 ch for 7th order) as a 64-channel
WAV at **48 kHz** with no master-channel downmix ("Master mix", channels = 64).

```sh
hoa2adm mix_hoa7.wav --info                     # inspect input + layout
hoa2adm mix_hoa7.wav -o mix_atmos.wav           # extended layout, 7.1 bed + objects
hoa2adm mix_hoa7.wav --layout dense --yaw 0 --lfe -10 --stems stems/
```

In Logic Pro: **File ▸ Import ▸ ADM BWF…**, choose the output. Logic creates an Atmos
project with the bed on a 7.1 track and each object on its own track with its fixed 3D
panner position.

If Logic ever refuses a file, `--stems <dir>` writes one mono WAV per track plus
`positions.txt` (bed label or object X/Y/Z), so the session can be built by hand.

## Validation

* `hoa_tests`: SH orthonormality, AmbiX conventions, cube mapping, per-speaker localisation for
  every preset, yaw, and an end-to-end 64-ch conversion (plane waves at known directions land
  on the right bed channel or object, below-horizon content folds down, chunks present).
* The ADM output parses and renders in strict mode with the EBU ADM Renderer (`ear-render -s 4+7+0 --strict`).
