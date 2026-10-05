# force-mouse

USB mouse support for the Akai Force running [MockbaMod](https://github.com/aud10slave/MockbaModular):
an `LD_PRELOAD` addon that draws a hardware cursor, turns clicks and drags into touch events,
turns the mouse wheel into pinch-to-zoom, and maps extra mouse buttons to keys or MIDI CC.

Credits: original idea and cursor hack by **@no3z**, MockbaMod addon adaptation by **Amit Talwar
(@locrian)**, cursor bitmap from [bonsaipanda](https://github.com/bonsaipanda/Code-Snippets).

## Read this first: firmware compatibility

| Force firmware | Result |
|---|---|
| The one it was developed on (January 2026; the original working folder is named "3.7") | Worked at the time, per the original notes. Not re-tested. |
| **3.9.1.2** (MockbaMod 4.51, OS az0x 5.0.17, kernel 6.18.26-az01-rt4) | **The addon builds, installs and loads, but does nothing.** |

On 3.9.1.2 MPC no longer calls `drmModeSetCursor`, `drmModeSetCursor2` or `drmModeMoveCursor`
(it uses atomic DRM only), and those calls are the addon's only entry point. MPC also reads
mice itself through `libinput` (relative motion, buttons, wheel and touch), so the addon is
redundant for a plain mouse on that firmware. The evidence and the commands to reproduce it are
in [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md).

The addon may still be useful with an older MPC binary run through MockbaMod's `ALTMPC`
mechanism, or on any firmware where MPC still calls the legacy cursor functions. Check with
`tools/probe_inputs` and the log lines described in [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Features

- Visible 64x64 cursor with alpha (`src/mouse_cursor.h`)
- Mouse movement with a speed multiplier, mapped to the portrait 800x1280 screen
- Left/middle/right click and continuous drag as touch events
- Wheel up/down as a two-finger pinch (zoom in/out), injected into the real touchscreen
- Mouse buttons to keyboard keys or MIDI CC (sent to MidiLoop through ALSA)
- Mouse, touchscreen and keyboard-provider devices found by capability, not by `eventN` number
  (new in 2.1.0, see [CHANGELOG.md](CHANGELOG.md))

## Quick start

Requirements on the PC: Docker. On the Force: MockbaMod on the SD card and SSH as root.

```bash
tools/build.sh                     # cross-compiles for the Force, stages dist/mouseCursor
tools/install.sh <force-ip>        # copies to the SD card, enables, restarts the Force app
```

`install.sh` asks ssh for the password; credentials are never stored in this repository.
Details, manual installation, rollback and verification: [docs/INSTALL.md](docs/INSTALL.md).

## Documentation

| | |
|---|---|
| [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) | What works on which firmware, and the evidence for 3.9.1.2 |
| [docs/INSTALL.md](docs/INSTALL.md) | Build, install, enable/disable, uninstall, verify |
| [docs/CONFIGURATION.md](docs/CONFIGURATION.md) | `device.txt` format, button mappings, MIDI CC list |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How it works and known quirks |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Event numbers that move, logs, common failures |
| [docs/TOUCH_NOT_LOADING.md](docs/TOUCH_NOT_LOADING.md) | Why the touchscreen sometimes does not load at boot (the reason to use a mouse) |
| [CHANGELOG.md](CHANGELOG.md) | History and provenance of the code |

## Repository layout

```
src/force_cursor.c    the LD_PRELOAD library
src/input_probe.h     device lookup by capability / name
src/mouse_cursor.h    cursor bitmap
addon/                files that go to <SD>/AddOns/mouseCursor (manage.sh, run script, device.txt)
tools/                build.sh, install.sh, Dockerfile, probe_inputs.c, test_mouse_buttons.sh
docs/                 documentation
```

## License

No license has been chosen yet. The cursor bitmap derives from bonsaipanda's Code-Snippets and
the MockbaMod integration is by Amit Talwar; check with them before redistributing.
