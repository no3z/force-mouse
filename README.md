# force-mouse

USB mouse support for the Akai Force running MockbaMod:
an `LD_PRELOAD` addon that draws a hardware cursor, turns clicks and drags into touch events,
turns the mouse wheel into pinch-to-zoom, and maps extra mouse buttons to keys or MIDI CC.

Credits: original idea and cursor hack by **@no3z**, MockbaMod addon adaptation by **Amit Talwar
(@locrian)**, cursor bitmap from [bonsaipanda](https://github.com/bonsaipanda/Code-Snippets).

## Status

| Force firmware | Status |
|---|---|
| **3.9.1.2** (MockbaMod 4.51, OS az0x 5.0.17, kernel 6.18.26-az01-rt4) | Supported since 3.0.0. The author reported it working on the device on 2026-10-05; the checks made with a synthetic mouse are listed in [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md). |
| Firmware of January 2026 | The original hook path is still in the code; not re-tested with 3.0.0. |

Versions before 3.0.0 did nothing on 3.9.1.2: MPC no longer calls the legacy `drmModeSetCursor*`
functions the addon hooked, it drives the display with atomic commits and reads input through
`libinput`. 3.0.0 starts from a library constructor, adds the hardware cursor plane to MPC's own
atomic commits, and sends clicks, drags and zoom through a virtual multi-touch screen. The evidence
and what is and is not verified are in [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md).

## Features

- Visible 64x64 cursor with alpha (`src/mouse_cursor.h`), rotatable (`CURSOR_ROTATE`)
- Mouse movement with a speed multiplier, mapped to the portrait 800x1280 panel
- Left/middle/right click and continuous drag as touch events
- Wheel up/down as a two-finger pinch (zoom in/out)
- Mouse buttons to keyboard keys or MIDI CC (sent to MidiLoop through ALSA)
- Works when the physical touch controller did not load at boot: input goes through its own
  virtual touch screen, and the mouse is found by capability, not by `eventN` number
- Crash-loop guard: a bug in the addon cannot leave the Force without a display

## Quick start

Requirements on the PC: Docker. On the Force: MockbaMod on the SD card and SSH as root. Without
Docker, download the zip from the Releases page and follow [docs/INSTALL.md](docs/INSTALL.md#install-from-a-release-no-docker).

```bash
tools/build.sh                     # cross-compiles for the Force, stages dist/mouseCursor
tools/install.sh <force-ip>        # copies to the SD card, enables, restarts the Force app
```

`install.sh` asks ssh for the password; credentials are never stored in this repository.
Details, manual installation, rollback and verification: [docs/INSTALL.md](docs/INSTALL.md).

## Documentation

| | |
|---|---|
| [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) | What changed in firmware 3.9.1.2, how 3.0.0 adapts, what is verified |
| [docs/INSTALL.md](docs/INSTALL.md) | Build, install, enable/disable, uninstall, verify |
| [docs/CONFIGURATION.md](docs/CONFIGURATION.md) | `device.txt` format, options, button mappings, MIDI CC list |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How it works, crash-loop guard, known quirks |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Logs, cursor or clicks not working, recovery |
| [docs/TOUCH_NOT_LOADING.md](docs/TOUCH_NOT_LOADING.md) | Why the touchscreen sometimes does not load at boot (the reason to use a mouse) |
| [CHANGELOG.md](CHANGELOG.md) | History and provenance of the code |

## Repository layout

```
src/force_cursor.c    the LD_PRELOAD library
src/input_probe.h     device lookup by capability / name
src/mouse_cursor.h    cursor bitmap
addon/                files that go to <SD>/AddOns/mouseCursor (manage.sh, run script, device.txt)
tools/                build.sh, install.sh, Dockerfile, and on-device tools:
                      probe_inputs, drm_planes, evdump, fake_mouse, test_mouse_buttons.sh
docs/                 documentation
```

## License

MIT, Copyright (c) 2026 no3productionz: see [LICENSE](LICENSE). Parts that come from other people
(cursor bitmap, MockbaMod integration) are listed in [NOTICE.md](NOTICE.md); read it before
redistributing.
