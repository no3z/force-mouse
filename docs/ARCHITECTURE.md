# Architecture

## Overview

`libforce_cursor.so` is preloaded into the MPC process (`LD_PRELOAD`). It starts from a library
constructor and works through three channels: the DRM cursor plane, a virtual touch screen and a
background thread that reads the mouse.

```
constructor (only when /proc/self/exe is MPC)
  |- crash-loop guard check
  |- create "Virtual Mouse Touch" (uinput), before MPC builds its libinput context
  `- bootstrap thread
        find the DRM fd MPC opened + an active CRTC
        start_addon():
          init_cursor()          read /dev/shm/.mouseCursor, 64x64 ARGB dumb buffer
          setup_atomic_cursor()  cursor-type plane, property ids, framebuffer
          input_monitor thread   mouse -> cursor position, touch events, keys, MIDI CC
          show_cursor()          legacy SetCursor2 + MoveCursor

MPC's own commits
  drmModeAtomicCommit(fd, req, ...)  --hook-->  + cursor plane properties  -->  real commit
```

The hooks for `drmModeSetCursor2`, `drmModeSetCursor` and `drmModeMoveCursor` are still there: on
firmware where MPC calls `drmModeSetCursor2(bo_handle = 0)` they start the same `start_addon()`.
`start_addon()` runs once, from whichever entry comes first.

## Components

**Finding the display.** `find_drm_fd()` walks `/proc/self/fd` for a `/dev/dri/card*` whose CRTC has
a valid mode, retrying every 100 ms for up to 60 s while MPC brings the display up. It uses MPC's own
fd, which is the DRM master; the cursor ioctls and atomic commits need that.

**Cursor plane.** `setup_atomic_cursor()` looks for the plane of type cursor that supports ARGB8888
and the CRTC, reads its property ids (`FB_ID`, `CRTC_ID`, `CRTC_X/Y/W/H`, `SRC_X/Y/W/H`) and wraps
the cursor buffer with `drmModeAddFB2`. MPC switches off every plane it does not use on each atomic
commit, so a cursor set once is wiped at the next frame. The `drmModeAtomicCommit` hook therefore
adds this plane to each real commit (not to `TEST_ONLY` ones), then restores MPC's request with
`drmModeAtomicSetCursor`. If the kernel rejects a commit with our plane but accepts MPC's alone,
that is logged; after three of those the injection is switched off. Between commits the position
is updated with the legacy `drmModeMoveCursor`; in the tests the plane state in debugfs followed the
mouse that way. The hook on `drmModeAtomicAddProperty` only logs the first 24 writes MPC
makes to the cursor plane.

**Cursor bitmap.** `mouse_cursor.h` holds `cursor_data[4096]` (64x64, `0xAARRGGBB`), tip at pixel
(0,0). `CURSOR_ROTATE` (default 270) rotates it in panel space and the hot spot follows the tip:
the plane is placed at `(cursor_x - hot_x, cursor_y - hot_y)`. The panel is portrait while MPC draws
its interface rotated by 90 degrees, hence the default.

**Coordinates.**

```
REL_X  ->  cursor_y -= value * speed   (inverted), clamped to 0..1279
REL_Y  ->  cursor_x += value * speed,               clamped to 0..799
```

The initial position is (50, 1230).

**Virtual touch screen.** `Virtual Mouse Touch`: `ABS_X/Y` and `ABS_MT_SLOT` (0..9),
`ABS_MT_TRACKING_ID`, `ABS_MT_POSITION_X/Y`, ranges 0..799 x 0..1279 (panel coordinates),
`BTN_TOUCH`, and `INPUT_PROP_DIRECT`, so udev tags it `ID_INPUT_TOUCHSCREEN=1` and libinput treats
it as a touchscreen. It is created in the constructor, before MPC enumerates its inputs, with a
0.3 s wait for udev. Because it is a separate device, clicks and zoom do not depend on the physical
touch controller, which sometimes does not load at boot.

- Click / drag: finger 1 (slot 0, tracking ids from 100) goes down at the cursor on
  `BTN_LEFT`/`RIGHT`/`MIDDLE` press, follows every motion while held, lifts on release.
- Wheel: `animate_pinch_gesture()` sends 5 frames 16 ms apart with two fingers (slots 0 and 1,
  tracking ids 10 and 11) on the diagonal around the cursor, spacing 30 px to 100 px (wheel up, zoom
  in) or back (wheel down), then lifts both.

**Mouse.** The first line of `device.txt` (`auto`, `name:<text>` or a path) selects the node; it is
opened with `O_CLOEXEC` and, with `GRAB=1` (default), grabbed with `EVIOCGRAB`. MPC's libinput
has it open too but no longer receives its events, so MPC does not also move an invisible pointer
and click on top of our touches. The grab ends when MPC exits or the fd closes.

**Button mappings.** A mapped button is not sent as a touch. A key mapping writes to the node named
`Amit's Input Provider` (found by name); a MIDI CC mapping goes through ALSA to client 129 port 0.

**Threading.** The input thread polls the mouse every 1 ms. A gesture blocks it about 110 ms; wheel
ticks during one are dropped.

## Crash-loop guard

The library lives inside MPC. If MPC dies soon after a start with the addon, restarting with it
would likely die again, and after a few rapid restarts systemd gives up on `acvs`: no display, and
no touch to recover with. So the constructor creates `/dev/shm/.mouseCursor.guard`, and removes it
after a clean exit or once MPC has run for 30 s. A start that finds the marker younger than 45 s
leaves the addon off for that run and logs `[GUARD]`. Remove the marker to override.

## Device lookup

`input_probe.h` opens `/dev/input/event0..31` read-only and inspects `EVIOCGNAME` and `EVIOCGBIT`:

| Role | Rule |
|---|---|
| mouse | `EV_REL` with `REL_X` and `REL_Y`, and `BTN_LEFT`; not `Amit's Input Provider` or `Virtual Mouse Touch` |
| mouse by name | the same, and the name contains the given text |
| key injection | name equals `Amit's Input Provider` |

`tools/probe_inputs` prints the table and the selection.

## Known quirks

- `animate_pinch_gesture()` declares `static int base_tracking_id` twice, in different scopes: the
  copy that is incremented is not the one that is used, so pinch tracking ids are always 10 and 11.
- The MIDI destination 129:0 is hard-coded.
- Every key and wheel event is logged to stdout (the journal of `acvs`).
- `open_keyboard_monitor()` is unused.
- MIDI CC value is always 127 and only press events are sent.
- The cursor bitmap is not premultiplied by alpha, so semi-transparent edge pixels may look
  slightly bright on the cursor plane.
