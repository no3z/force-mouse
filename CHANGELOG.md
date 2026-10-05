# Changelog

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
