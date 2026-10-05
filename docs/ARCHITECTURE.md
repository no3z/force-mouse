# Architecture

## Overview

`libforce_cursor.so` is preloaded into the MPC process (`LD_PRELOAD`). It overrides three libdrm
functions and, from the first intercepted call, runs one background thread.

```
MPC process
  |  drmModeSetCursor2(fd, crtc, bo_handle = 0, ...)   "hide the cursor"
  v
libforce_cursor.so  (loaded first, wins symbol lookup)
  |- init_cursor()        reads /dev/shm/.mouseCursor, builds a 64x64 ARGB DRM dumb buffer
  |- shows that buffer as the hardware cursor via the real drmModeSetCursor2
  `- input_monitor thread
        mouse event node  --REL_X/REL_Y-->  cursor position --> real drmModeMoveCursor
                          --BTN_*-------->  touch (uinput) | key | MIDI CC
                          --REL_WHEEL---->  two-finger pinch --> real touchscreen node
```

The hooks are `drmModeSetCursor2`, `drmModeSetCursor` (forwards to the first) and
`drmModeMoveCursor`. Every other cursor call is passed through unchanged.

## Components

**Cursor.** `mouse_cursor.h` holds `cursor_data[4096]` (64x64, `0xAARRGGBB`). It is copied into a
dumb buffer created with `DRM_IOCTL_MODE_CREATE_DUMB` and shown with the real
`drmModeSetCursor2(fd, crtc, handle, 64, 64, 0, 0)`; the hot spot is the top-left pixel. The
initial position is (50, 1230), bottom left.

**Coordinates.** The panel is 800x1280 portrait while the mouse thinks in landscape:

```
REL_X  ->  cursor_y -= value * speed   (inverted), clamped to 0..1279
REL_Y  ->  cursor_x += value * speed,               clamped to 0..799
```

**Clicks and drag.** A uinput device named `Virtual Mouse Touch` (single touch: `ABS_X`
0..799, `ABS_Y` 0..1279, `BTN_TOUCH`) receives a touch at the cursor on `BTN_LEFT`/`RIGHT`/
`MIDDLE` press and release. While the left button is held, every motion sends another touch
position, which gives continuous drag.

**Pinch zoom.** A wheel tick starts `animate_pinch_gesture()`: 5 frames 16 ms apart, two fingers on
the diagonal around the cursor, spacing 30 px to 100 px (zoom in, wheel up) or 100 px to 30 px
(zoom out), written to the real touchscreen as multi-touch protocol B (`ABS_MT_SLOT`,
`ABS_MT_TRACKING_ID`, `ABS_MT_POSITION_X/Y`, plus `BTN_TOUCH` and `ABS_X/Y` like a real panel).
The original design injects into the real touchscreen instead of creating a virtual multi-touch
device (the reason is not documented). Consequence: **no real multi-touch screen, no zoom.**
Whether MPC would accept a virtual multi-touch device was not tested. Since 2.1.0 the library looks for it
by capability (`ABS_MT_POSITION_X/Y`) and logs that gestures are disabled when it is absent.

**Button mappings.** A mapped button is not sent as a touch. `MAPPING_TYPE_KEY` writes a key event
to `Amit's Input Provider` (found by name); `MAPPING_TYPE_MIDI_CC` sends a CC through ALSA to
client 129 port 0.

**Threading.** The input thread polls the mouse fd (non-blocking) every 1 ms. A gesture blocks
that thread for about 110 ms, so wheel ticks during a gesture are dropped (`gesture_in_progress`).

## Device lookup (since 2.1.0)

`input_probe.h` opens `/dev/input/event0..31` read-only and inspects `EVIOCGNAME` and
`EVIOCGBIT`:

| Role | Rule |
|---|---|
| mouse | `EV_REL` with `REL_X` and `REL_Y`, and `BTN_LEFT`; not `Amit's Input Provider` or `Virtual Mouse Touch` |
| touchscreen | `ABS_MT_POSITION_X` and `ABS_MT_POSITION_Y`; not `Virtual Mouse Touch` |
| key injection | name equals `Amit's Input Provider` |

`tools/probe_inputs.c` prints the table and the selection; run `probe_inputs` on the Force.

## Known quirks (left as in the original on purpose)

- `animate_pinch_gesture()` declares `static int base_tracking_id` twice, in different scopes: the
  copy that is incremented is not the one that is used, so tracking ids are always 10 and 11.
- The MIDI destination 129:0 is hard-coded.
- Every key and wheel event is logged to stdout, which ends up in the journal (`acvs`).
- `open_keyboard_monitor()` is unused (hardware button monitoring was disabled).
- MIDI CC value is always 127 and only press events are sent.
- The thread is started even if `/dev/shm/.mouseCursor` is missing; it then logs an error and exits.

## Entry point and limits

Everything depends on MPC calling `drmModeSetCursor2` with `bo_handle == 0`. Firmware 3.9.1.2 does
not, so nothing starts there: see [COMPATIBILITY.md](COMPATIBILITY.md).
