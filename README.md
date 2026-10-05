# force-mouse

Two independent fixes for the **Akai Force** running [MockbaMod](docs/MOCKBAMOD.md). Use either one, or both.

| | Mouse support | Touchscreen recovery (touchFix) |
|---|---|---|
| **Problem** | The Force has no mouse pointer | Sometimes it boots with no touchscreen |
| **What it does** | Hardware cursor, clicks and drags, wheel as pinch-to-zoom, mouse buttons to keys or MIDI CC | Notices at boot that the touch controller was not detected, resets it and reboots once |
| **Is it** | A MockbaMod add-on (`AddOns/mouseCursor`) | Two shell scripts on the SD card root |
| **Turn on** | `adm` (Enable) or `manage.sh ENABLE` | Copy `touchfix.sh` and `autoexec.sh` to the SD card |
| **Turn off** | `adm` (Disable) or `manage.sh DISABLE` | Delete `autoexec.sh` |
| **Needs a restart to start** | Yes, of the Force application | No, it acts at the next power-up |
| **Docs** | [Install](docs/INSTALL.md), [configuration](docs/CONFIGURATION.md), [how it works](docs/ARCHITECTURE.md) | [touchFix](docs/TOUCHFIX.md), [the evidence](docs/TOUCH_NOT_LOADING.md) |

They also work well together: with the touchscreen failing at boot, the mouse still lets you drive the Force
while touchFix tries to bring the touchscreen back. The mouse does not depend on the touchscreen.

Status: developed and verified on **MPC 3.9.1.2** (MockbaMod 4.51, kernel 6.18.26-az01-rt4), reported working on
the author's Force. Not tested on other firmware; see [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md).

## Install

**Easiest: from the Force itself** (a terminal on the Force, which needs internet access; no PC tools, no Docker):

```sh
cd /tmp && curl -L -o fm.zip https://github.com/no3z/force-mouse/releases/latest/download/force-mouse-armv7.zip
unzip -oq fm.zip
# touchFix: two files on the SD card root (keeps an autoexec.sh you already have)
cp touchFix/touchfix.sh /media/662522/ && [ -f /media/662522/autoexec.sh ] || cp touchFix/autoexec.sh /media/662522/
# mouse: copy the add-on and enable it (restarts the Force application)
cp -r mouseCursor /media/662522/AddOns/ && sh /media/662522/AddOns/mouseCursor/manage.sh ENABLE
```

**From a PC**, after `git clone`: `tools/build.sh` (needs Docker) and then

```bash
tools/install.sh <force-ip> mouse touchfix      # either word alone installs just that one
```

It asks for the SSH password once and never stores it. More options, upgrades and rollback:
[docs/INSTALL.md](docs/INSTALL.md).

## Mouse support

USB mouse (or trackball) as a pointer for the Force: a visible arrow, left/right/middle click, drag, and the
wheel as a two-finger pinch for zoom. Extra mouse buttons can be mapped to keyboard keys or to MIDI CC through
MidiLoop. It keeps working when the physical touchscreen did not load, because clicks go through its own virtual
touchscreen. Configure it in `device.txt` ([docs/CONFIGURATION.md](docs/CONFIGURATION.md)).

## Touchscreen recovery

At power-up the Ilitek touch controller sometimes does not answer, and the Force boots without a touchscreen.
`touchfix.sh` runs from MockbaMod's own `autoexec.sh` hook, before MPC starts: if the touch is missing it pulses the
controller's reset line and, if the controller answers again, reboots **once**. Every boot is logged in
`touchfix.log`. It never writes to the controller and never reboots twice in a row.
Details and the measurements behind it: [docs/TOUCHFIX.md](docs/TOUCHFIX.md).

## Documentation

| | |
|---|---|
| [docs/MOCKBAMOD.md](docs/MOCKBAMOD.md) | How both fit into MockbaMod: what runs when, enabling, automation, and what is not touched |
| [docs/INSTALL.md](docs/INSTALL.md) | Build, install, upgrade, uninstall |
| [docs/TOUCHFIX.md](docs/TOUCHFIX.md) | Touchscreen recovery |
| [docs/TOUCH_NOT_LOADING.md](docs/TOUCH_NOT_LOADING.md) | The touch controller investigation: evidence and caveats |
| [docs/CONFIGURATION.md](docs/CONFIGURATION.md) | Mouse `device.txt`, options, button mappings, MIDI CC list |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How the mouse add-on works, crash-loop guard, known quirks |
| [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) | What changed in firmware 3.9.1.2 and what is verified |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Logs, cursor or clicks not working, recovery |
| [CHANGELOG.md](CHANGELOG.md) | History |

## Repository layout

```
src/               the mouse library (force_cursor.c, input_probe.h, mouse_cursor.h)
addon/             files of the mouse add-on that go to <SD>/AddOns/mouseCursor
touchfix/          touchfix.sh, autoexec.sh: they go to the SD card root
tools/             build.sh, install.sh, Dockerfile, on-device diagnostic tools
docs/              documentation
VERSION            one version number for the whole package
```

## License

MIT, Copyright (c) 2026 no3productionz: see [LICENSE](LICENSE). Parts that come from other people
(cursor bitmap, MockbaMod integration) are listed in [NOTICE.md](NOTICE.md); read it before redistributing.

Credits: original mouse idea and cursor hack by **@no3z**, MockbaMod add-on adaptation by **Amit Talwar
(@locrian)**, cursor bitmap from [bonsaipanda](https://github.com/bonsaipanda/Code-Snippets).
