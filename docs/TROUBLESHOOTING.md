# Troubleshooting

All commands run on the Force over SSH. The addon's output goes to the journal of `acvs`:

```sh
journalctl -u acvs --no-pager | grep -E "\[BOOT\]|\[GUARD\]|\[INIT\]|\[ATOMIC\]|\[CONFIG\]|\[WHEEL\]|\[KEYBOARD\]|\[MIDI\]|WARNING|ERROR"
```

A healthy start on firmware 3.9.1.2 logs, in this order:

```
[BOOT] Cursor bitmap rotated 270 degrees, hot spot (0,63)
[BOOT] Atomic cursor ready: plane 35, fb 52, pitch 256
[BOOT] Cursor shown on crtc 37 (drm fd 16)
[INIT] Auto-detected mouse: /dev/input/event1 ('...')
[INIT] Mouse grabbed, MPC no longer sees it directly
[INIT] Virtual touch screen ready, fd=3
```

## No cursor on the screen

1. Is the library loaded in MPC? `grep -c libforce_cursor /proc/$(pidof MPC)/maps` must be > 0 and
   `/dev/shm/.LD_PRELOAD` must list it. If not, the addon is not enabled: run
   `sh /media/662522/AddOns/mouseCursor/manage.sh ENABLE`.
2. Did it start? `[GUARD] MPC restarted within 45 s ...` means the crash-loop guard kept the addon
   off for this run (MPC was restarted soon after the previous start). Wait a minute and restart
   the application again, or `rm /dev/shm/.mouseCursor.guard` first.
3. What is the cursor plane doing? It should be on, with a 64x64 `AR24` framebuffer:

   ```sh
   awk '/^plane\[35\]/{f=1} /^plane\[38\]/{f=0} f' /sys/kernel/debug/dri/1/state | grep -E 'crtc=|fb=|crtc-pos|format='
   ```

   `crtc=(null) fb=0` means something switched it off. MPC does that on every commit, and the addon
   counters it by adding the plane to MPC's commits; if `[ATOMIC] Commit with the cursor failed ...`
   is in the log the kernel rejected the plane and injection stops after three such failures.
4. `[BOOT] No cursor plane found` or `No active DRM device found in this process`: run
   `tools/drm_planes /dev/dri/card1`, which should list a plane of `type=cursor` with `AR24`,
   and send its output.

## The cursor looks rotated or mirrored

Set `CURSOR_ROTATE=0`, `90`, `180` or `270` in `device.txt` (default 270) and restart the
application. The active point (where a click lands) follows the tip of the arrow for every value.

## Clicks do not land under the cursor, or do nothing

- The cursor position is in panel coordinates (portrait 800x1280); the virtual touch screen reports
  the same axes. If MPC maps touch coordinates differently the click lands elsewhere; report where
  the cursor was and where the click landed.
- Check that the addon emits the touches: `tools/evdump /dev/input/eventN 10` on the node of
  `Virtual Mouse Touch` (find it in `/proc/bus/input/devices`), then click. You should see
  `ABS_MT_TRACKING_ID`, `BTN_TOUCH 1`, positions, then `ABS_MT_TRACKING_ID -1`.
- Check that MPC picked the device up: `ls -l /proc/$(pidof MPC)/fd | grep input/event` should list
  the same node, and `udevadm info /dev/input/eventN` should show `ID_INPUT_TOUCHSCREEN=1`.
- If the mouse also does something on its own in MPC, the grab failed: look for
  `WARNING: could not grab the mouse`.

## The mouse is not the device you configured

The event numbers shift when a device is missing at boot (typically the touch controller, see
[TOUCH_NOT_LOADING.md](TOUCH_NOT_LOADING.md)). Look at the real state:

```sh
/media/662522/AddOns/mouseCursor/tools/probe_inputs
```

```
  /dev/input/event0    gpio-keys                          rel=0 btn_left=0 abs_mt=0 key=1
  /dev/input/event1    Microsoft Microsoft Trackball Explorer® rel=1 btn_left=1 abs_mt=0 key=1
  /dev/input/event2    Amit's Input Provider              rel=0 btn_left=0 abs_mt=0 key=1

Selection used by libforce_cursor.so:
  mouse        -> /dev/input/event1 (Microsoft Microsoft Trackball Explorer®)
  touchscreen  -> NOT FOUND
  keyboard     -> /dev/input/event2 (Amit's Input Provider)
```

Use `auto` (or `name:<text>`) on the first line of `device.txt`.

| Log line | Meaning |
|---|---|
| `[INIT] Auto-detected mouse: /dev/input/eventN (...)` | `auto` found it |
| `[INIT] Mouse matching '<text>': ...` | `name:<text>` found it |
| `[INIT] WARNING: ... is not a mouse, falling back to auto-detection` | stale path in `device.txt` |
| `[INIT] No mouse found by auto-detection` | no relative pointer with `BTN_LEFT` is connected |
| `[INIT] FAILED: no virtual touch screen (/dev/uinput)` | clicks and zoom disabled, the cursor still shows |
| `[KEYBOARD] Device 'Amit's Input Provider' not found` | MidiLoop not running, key mappings skipped |

## Button mappings do nothing

- Wrong code: run `tools/test_mouse_buttons.sh <device>` and compare with the names in
  [CONFIGURATION.md](CONFIGURATION.md). Names are case-insensitive in the mapping parser.
- Key mappings need the MidiLoop addon (it creates `Amit's Input Provider`).
- MIDI CC mappings go to ALSA client 129. Check it is the MidiLoop client:
  `aconnect -l | grep -E "^client 129"` should print `'Mockba'` (and a `MouseButtonMIDI` client
  appears once a CC mapping is initialized). If another MIDI device took the numbering, the
  destination in `send_midi_cc()` has to change.
- MidiLoop must have the CC assigned in `midiloop.config`.

## Cursor is erratic or too fast

Change the speed on line 2 of `device.txt` (0.1 to 5.0) and restart the application. A value
outside that range falls back to 1.0.

## Recovery: the Force shows nothing after installing

sshd does not depend on MPC. From the PC:

```sh
ssh root@<force-ip> 'sh /media/662522/AddOns/mouseCursor/manage.sh DISABLE'
ssh root@<force-ip> 'systemctl reset-failed acvs; systemctl restart acvs'
```

`DISABLE` removes the run script, the RAM configuration and the preload entry. The crash-loop guard
is there to make this unnecessary: it keeps the addon off when MPC restarts shortly after a start.

## Restart or inspect the application

```sh
systemctl restart acvs          # or: respawn
systemctl status acvs --no-pager
```
