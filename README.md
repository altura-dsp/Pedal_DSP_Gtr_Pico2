# Pedal DSP GTR

> Guitar multi-effects pedal firmware for Raspberry Pi Pico 2 (RP2350): amp simulation, conditioning and mojo DSP stages, OLED UI, NeoPixels.

**Status:** First public release · **Target:** Raspberry Pi Pico 2 (RP2350) · **License:** [GPL-3.0](LICENSE)

## ☕ Support this project

Building and maintaining open DSP hardware takes real time, real components, and real love.
If Altura DSP has been useful to you, consider supporting — crypto only, no borders:

| Network | Address |
|---------|---------|
| ⚡ BTC Lightning | `AlturaDSP@coinos.io` |

Lightning is the recommended option: zero fee, instant, works from any Lightning wallet.

## Build

```
pio run
```

Requires [PlatformIO](https://platformio.org/) CLI (or the VS Code extension).

## Hardware

Schematics, netlists and the SKiDL source live in [`Hardware_PCB/`](Hardware_PCB/).

## Third-Party Components

This repository includes the following third-party software. Each entry lists its
upstream source and license; file headers are retained verbatim.

### Airwindows plugins (ported)

- **Purpose:** DSP algorithms ported for RP2350
- **Upstream:** https://www.airwindows.com
- **License:** MIT (attribution headers in each ported file)
- **Location in this repo:** `src/dsp/`

## License

Copyright (C) 2026 altura-dsp

Released under the GNU General Public License v3 — see [LICENSE](LICENSE).

---

Explore more open DSP hardware projects at [Altura DSP](https://github.com/altura-dsp)
