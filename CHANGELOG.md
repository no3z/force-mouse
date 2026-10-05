# Changelog

## Unreleased

- New tool `touch_reset.sh`: pulses the touch controller's reset line when the kernel did not bind it
  at boot. On the author's Force it brought the ILI2117 back on its normal address, and a reboot then
  gave a working touchscreen; one observation, no control. See `docs/TOUCH_NOT_LOADING.md`.
- `drm_planes` and `drm_screenshot` pick the display card themselves: it is `card0` on some boots and
  `card1` on others. The docs use `/sys/kernel/debug/dri/*/state`.

## 3.0.1 - 2026-10-05

- Fix: the cursor sometimes glitched and jumped around. The legacy cursor move ioctl ignores the hot
  spot while the atomic commits apply it, so the arrow was drawn 63 px off while the mouse moved and
  snapped back every time MPC committed a frame. Both paths now subtract the hot spot. Measured by
  sampling the plane position during steady motion: before, a +57 px jump and a final position
  63 px off; after, a smooth 1167 to 807 with no jump.
- The input thread drains every pending event before sleeping (it slept 1 ms after each event, which
  capped throughput at about 1000 events/s and would lag with a fast mouse).
- New tool `drm_screenshot`: saves what the Force is showing, rotated to landscape.
- MIT license (Copyright (c) 2026 no3productionz), `NOTICE.md` for third-party parts, and a GitHub
  Actions workflow that builds and publishes a Release when a `v*` tag is pushed.

## 3.0.0 - 2026-10-05

Rework for Force firmware 3.9.1.2, where versions before 3.0.0 did nothing (MPC no longer calls the
legacy `drmModeSetCursor*` functions the addon hooked; see [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md)).

- Starts from a library constructor inside MPC, finds the DRM fd MPC opened, and still keeps the old
  hook path for firmware that uses it.
- The cursor plane is added to every real `drmModeAtomicCommit` MPC makes (MPC switches it off on each
  commit), with automatic fallback if the kernel rejects it.
- Clicks, drags and the wheel pinch go through a virtual multi-touch screen created before MPC builds
  its libinput context (`INPUT_PROP_DIRECT`, MT protocol B), so they work without the physical touch
  controller. The mouse is grabbed exclusively (`GRAB`, default 1).
- `CURSOR_ROTATE` (default 270) with a hot spot that follows the arrow tip; `name:<text>` for the mouse
  line of `device.txt`.
- Crash-loop guard (`/dev/shm/.mouseCursor.guard`).
- New tools: `drm_planes`, `evdump`, `fake_mouse`; the library links `libdrm` explicitly.
- Verified with a synthetic mouse at the DRM-state and input-event level, then confirmed working on
  the device by the author.

## 2.1.0 - 2026-10-05

Changes relative to the 2.0 sources of January 2026 (imported unchanged in the first commit):

- Find the mouse, the multi-touch screen and `Amit's Input Provider` by capability or name instead
  of fixed `event2`, `event0` and `event3` (`src/input_probe.h`). `device.txt` line 1 accepts
  `auto`; a configured path that is not a mouse falls back to auto-detection.
- Zoom gestures are disabled with a log line when no multi-touch screen exists, instead of writing
  into whatever `event0` is.
- `manage.sh DISABLE` / `UNINSTALL` also remove the library from `/dev/shm/.LD_PRELOAD`.
- Guard against a missing `/dev/shm/.mouseCursor`.
- Speed validation used the uninitialised output value instead of the parsed one.
- Add `probe_inputs`, a Docker based build (`tools/build.sh`, Debian bookworm) and `tools/install.sh`.
- `tools/test_mouse_buttons.sh` takes the device as an argument.
- Documentation, including the compatibility finding for MPC 3.9.1.2
  ([docs/COMPATIBILITY.md](docs/COMPATIBILITY.md)).

Verified on 2026-10-05 on a Force with MPC 3.9.1.2 (kernel 6.18.26-az01-rt4): builds, installs,
is preloaded into MPC, `probe_inputs` selects the right devices. The hooks never fire on that
firmware, so the cursor, click, zoom and mapping features could not be exercised there.

## 2.0 - January 2026

Imported from the local `mockba-cursor-3.7/mouseCursor` folder (`force_cursor.c` of
2026-01-24 18:14). Compared with the earlier iterations:

- Cursor bitmap with alpha from `mouse_cursor.h` (the first version drew a white 24 px circle).
- Continuous drag while the left button is held.
- Mouse wheel as a pinch gesture, injected into the real touchscreen.
- Button mappings to keyboard keys (through `Amit's Input Provider`) and to MIDI CC (ALSA, 129:0).
- MockbaMod addon packaging (`manage.sh`, `run_mouseCursor.sh`).

Earlier iterations that are not imported (they remain in the original working folder): the
first prototype `our_work/force_cursor.c` (white circle, click as single touch), an intermediate
root-level `force_cursor.c` with pinch gestures (2026-01-23), and `force_cursor_two_devices.c`.

No binary is committed. The `libforce_cursor.so` shipped next to the 2.0 sources was built at
18:07, seven minutes before the last edit of `force_cursor.c` (18:14), so it may not match the
source; build from source with `tools/build.sh`.
