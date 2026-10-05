# Compatibility

## Summary

| Force firmware | Build | Library loads in MPC | Hook entry point fires | Mouse works through this addon |
|---|---|---|---|---|
| January 2026 (original folder named "3.7") | yes | yes | yes (per original notes) | yes (per original notes) |
| 3.9.1.2 | yes | yes | **no** | **no** (MPC handles the mouse itself, see below) |

The 3.9.1.2 row was measured on 2026-10-05 on a Force with:

- MPC 3.9.1.2, MockbaMod 4.51
- OS `az0x 5.0.17 (scarthgap)`, kernel `6.18.26-az01-2026-04-30-rt4` (armv7l, PREEMPT_RT)
- glibc 2.39, `libdrm.so.2.4.0`, `libasound.so.2.0.0`, `libinput.so.10.13.0`
- Microsoft Trackball Explorer on the Force's USB hub

The first row comes from the development notes of the original addon (the firmware version is
not stated there; the original working folder is named `mockba-cursor-3.7`); it was not re-tested.

## Why it does nothing on 3.9.1.2

The addon only wakes up when MPC calls `drmModeSetCursor2()` with `bo_handle == 0` (MPC "hides"
the hardware cursor). That call starts the input thread, creates the uinput touch device and
draws the cursor. If MPC never makes that call, nothing else in the library ever runs.

### 1. MPC no longer uses the legacy cursor API

The DRM functions imported by `/usr/bin/MPC` on 3.9.1.2:

```
drmClose drmDropMaster drmGetCap drmHandleEvent drmIoctl drmModeAddFB2
drmModeAtomicAddProperty drmModeAtomicAlloc drmModeAtomicCommit drmModeAtomicFree
drmModeCreatePropertyBlob drmModeDestroyPropertyBlob drmModeFree* drmModeGetConnector
drmModeGetEncoder drmModeGetPlane drmModeGetPlaneResources drmModeGetProperty
drmModeGetResources drmModeObjectGetProperties drmModeRmFB drmSetClientCap drmSetMaster
```

No `drmModeSetCursor`, `drmModeSetCursor2` or `drmModeMoveCursor`. Reproduce on the Force:

```sh
strings -a /usr/bin/MPC | grep -E '^drm[A-Za-z0-9_]+$' | sort -u
```

With the addon enabled and MPC restarted, the library is mapped into MPC
(`grep libforce_cursor /proc/$(pidof MPC)/maps`) and `/dev/shm/.LD_PRELOAD` lists it, but:

- the journal has no `[INIT]`, `[CURSOR_PATCH]` or `MockbaMod Mouse Cursor` lines
  (`journalctl -u acvs`), and
- `/proc/bus/input/devices` has no `Virtual Mouse Touch` device.

### 2. MPC reads mice itself

MPC has `libinput`, `libevdev`, `libudev` and `libmtdev` mapped, and imports:

```
libinput_event_pointer_get_dx / get_dy        relative motion (mouse, trackball)
libinput_event_pointer_get_button(_state)     buttons
libinput_event_pointer_get_scroll_value       wheel
libinput_event_pointer_get_absolute_*         absolute pointers
libinput_event_touch_get_slot / x / y         multi-touch
```

`udev` tags the trackball as `ID_INPUT_MOUSE=1`, `ID_INPUT_TRACKBALL=1`, and MPC holds the
trackball's `/dev/input/eventN` open. So a plain USB mouse is already a pointer for MPC. Whether
it draws a visible cursor, and what the wheel does, was not observed (no screen access during
this investigation); try it before installing this addon.

```sh
strings -a /usr/bin/MPC | grep -E '^libinput_event_(pointer|touch)_[a-z_]+$' | sort -u
awk '{print $6}' /proc/$(pidof MPC)/maps | sort -u | grep -E 'libinput|libevdev|libudev'
```

## When the addon can still help

- An older MPC binary run with MockbaMod's `ALTMPC` (the binary must still call the legacy cursor
  functions). Not tested.
- Features MPC does not provide natively: button-to-MIDI-CC mappings, wheel-as-pinch. Whether
  3.9.1.2 handles the wheel as zoom natively was not checked.

A future version would need a different entry point (a library constructor plus a hook on
`drmModeAtomicCommit` to learn the DRM fd, and a way to draw a cursor with the atomic API) and
a way to avoid double-handling events that MPC already reads through `libinput`. That is not
implemented.

## Other MockbaMod addons on 3.9.1.2

`mockbaMagic` logs `Your MPC Version is currently not supported ... Your Version : 3.9.1.2` on
the same Force, so MockbaMod add-ons built for older firmware are generally out of step with it.
