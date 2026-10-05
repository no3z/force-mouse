# Compatibility

## Summary

| Force firmware | Status |
|---|---|
| **3.9.1.2** (MockbaMod 4.51, OS az0x 5.0.17, kernel 6.18.26-az01-rt4) | Supported since 3.0.0. Loads, shows the cursor plane and emits touch events; verified with a synthetic mouse at the DRM-state and input-event level (see [Verification](#verification)). The on-screen result (look and orientation of the cursor, whether MPC acts on the touches) needs confirmation on the device. |
| Firmware of January 2026 (the original working folder is named "3.7") | The original hook path (`drmModeSetCursor2`) is still in the code. Worked at the time per the original notes; not re-tested with 3.0.0. |

The 3.9.1.2 measurements were taken on 2026-10-05 on a Force with glibc 2.39, `libdrm.so.2.4.0`,
`libasound.so.2.0.0`, `libinput.so.10.13.0` and a Microsoft Trackball Explorer on the Force's USB hub.

## What changed in firmware 3.9.1.2

Versions of this addon before 3.0.0 did nothing on it. Evidence, with commands to reproduce.

### MPC no longer uses the legacy cursor API

The DRM functions imported by `/usr/bin/MPC`:

```
drmClose drmDropMaster drmGetCap drmHandleEvent drmIoctl drmModeAddFB2
drmModeAtomicAddProperty drmModeAtomicAlloc drmModeAtomicCommit drmModeAtomicFree
drmModeCreatePropertyBlob drmModeDestroyPropertyBlob drmModeFree* drmModeGetConnector
drmModeGetEncoder drmModeGetPlane drmModeGetPlaneResources drmModeGetProperty
drmModeGetResources drmModeObjectGetProperties drmModeRmFB drmSetClientCap drmSetMaster
```

No `drmModeSetCursor`, `drmModeSetCursor2` or `drmModeMoveCursor`, which were the addon's only
entry point.

```sh
strings -a /usr/bin/MPC | grep -E '^drm[A-Za-z0-9_]+$' | sort -u
```

### MPC reads input through libinput

`libinput`, `libevdev`, `libudev` and `libmtdev` are mapped into MPC, and MPC imports
`libinput_event_pointer_get_dx/dy/button/scroll_value/absolute_*` and
`libinput_event_touch_get_slot/x/y`. It holds every `/dev/input/eventN` open, so any new input
device is a candidate input source. udev tags the trackball `ID_INPUT_MOUSE=1`,
`ID_INPUT_TRACKBALL=1`. Whether MPC draws a cursor for a plain mouse was not observed; in
practice the user saw no cursor.

```sh
strings -a /usr/bin/MPC | grep -E '^libinput_event_(pointer|touch)_[a-z_]+$' | sort -u
ls -l /proc/$(pidof MPC)/fd | grep input/event
```

### How MPC uses the display planes

`/sys/kernel/debug/dri/1/state` and the `drm_planes` tool show one CRTC (id 37, 800x1280, portrait)
and four planes:

| Plane | Type | State |
|---|---|---|
| 33 | primary | in use: 800x1280 `XR24` dumb framebuffers created by MPC, flipped between two buffers |
| 35 | **cursor** | free, formats include `AR24` (ARGB8888) |
| 38, 40 | overlay | free |

MPC renders its interface at 1280x800 and rotates it into the portrait buffer (the kernel log has
`rockchip-rga` converting 1280x800 to 800x1280).

A cursor enabled once through the legacy ioctls does not survive: MPC writes two properties of the
cursor plane (ids 17 and 20) to 0 in its atomic commits, which leaves it with `crtc=(null) fb=0`.
The `[ATOMIC]` lines in the journal show this (the first 24 writes are logged).

```sh
/media/662522/AddOns/mouseCursor/tools/drm_planes /dev/dri/card1
awk '/^plane\[35\]/{f=1} /^plane\[38\]/{f=0} f' /sys/kernel/debug/dri/1/state
```

### Input event numbers move

Without the touch controller (see [TOUCH_NOT_LOADING.md](TOUCH_NOT_LOADING.md)) the nodes are
`event0` gpio-keys, `event1` mouse, `event2` Amit's Input Provider; with it, one higher.

## How 3.0.0 adapts

- Starts from a library constructor inside MPC instead of waiting for the legacy cursor call.
- Adds the cursor plane to every real `drmModeAtomicCommit` MPC makes, so the plane stays on.
- Creates a virtual multi-touch screen before MPC builds its libinput context, and grabs the
  mouse so MPC does not also act on it.

Details in [ARCHITECTURE.md](ARCHITECTURE.md).

## Verification

Done on 2026-10-05 with a synthetic mouse (`tools/fake_mouse`) and the real trackball:

- The library loads in MPC; the constructor, DRM discovery, cursor buffer, framebuffer and
  atomic setup run (`[BOOT]` lines).
- Cursor plane 35 reports `crtc=crtc-0`, a 64x64 `AR24` framebuffer and a position that follows
  the mouse with the expected mapping and clamping, while MPC keeps committing frames.
- `Virtual Mouse Touch` exists with `INPUT_PROP_DIRECT`, udev tags it `ID_INPUT_TOUCHSCREEN=1`,
  and MPC holds it open.
- A click, a drag and wheel up/down produce the expected touch frames (`tools/evdump`): single
  touch with tracking id and position, a held touch that moves, and a two-finger pinch growing
  from 30 px to 100 px or shrinking back.
- The mouse is grabbed (`Mouse grabbed` in the log).

Not verified (needs eyes on the screen): that the cursor is visible and upright, that clicks land
under the cursor, that MPC reacts to the virtual touches and to the pinch, button mappings and MIDI
CC delivery.

## Other MockbaMod addons on 3.9.1.2

`mockbaMagic` logs `Your MPC Version is currently not supported ... Your Version : 3.9.1.2` on
the same Force, so addons built for older firmware are generally out of step with it.
