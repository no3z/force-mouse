# Troubleshooting

All commands run on the Force over SSH. The MPC output goes to the journal of `acvs`:

```sh
journalctl -u acvs --no-pager | grep -E "Mouse Cursor|\[INIT\]|\[TOUCHSCREEN\]|\[KEYBOARD\]|\[MIDI\]|\[CONFIG\]|\[WHEEL\]|CURSOR_PATCH|WARNING|ERROR"
```

## Nothing in the log, no `Virtual Mouse Touch` device

The addon only starts when MPC calls `drmModeSetCursor2` with handle 0. Check that the library is
loaded and whether MPC imports that function:

```sh
grep -c libforce_cursor /proc/$(pidof MPC)/maps          # > 0 means it is preloaded
strings -a /usr/bin/MPC | grep -E '^drmMode.*Cursor'     # empty: MPC never calls it
```

On firmware 3.9.1.2 the second command prints nothing, so the addon cannot start there. See
[COMPATIBILITY.md](COMPATIBILITY.md). Test the mouse without the addon first; MPC reads mice
through `libinput` on that firmware.

## The mouse is not the device you configured

The event numbers shift when a device is missing at boot (typically the touch controller, see
[TOUCH_NOT_LOADING.md](TOUCH_NOT_LOADING.md)). Look at the real state with `probe_inputs`:

```sh
/media/662522/AddOns/mouseCursor/probe_inputs
```

Example from a boot without the touch controller:

```
  /dev/input/event0    gpio-keys                          rel=0 btn_left=0 abs_mt=0 key=1
  /dev/input/event1    Microsoft Microsoft Trackball Explorer® rel=1 btn_left=1 abs_mt=0 key=1
  /dev/input/event2    Amit's Input Provider              rel=0 btn_left=0 abs_mt=0 key=1

Selection used by libforce_cursor.so:
  mouse        -> /dev/input/event1 (Microsoft Microsoft Trackball Explorer®)
  touchscreen  -> NOT FOUND
  keyboard     -> /dev/input/event2 (Amit's Input Provider)
```

With the touch controller loaded you should see an extra multi-touch node (`abs_mt=1`) as
`event0`. Use `auto` on the first line of `device.txt`. Log lines to look for:

| Log line | Meaning |
|---|---|
| `[INIT] Auto-detected mouse: /dev/input/eventN (...)` | `auto` found it |
| `[INIT] WARNING: /dev/input/eventN ('...') is not a mouse, falling back to auto-detection` | stale path in `device.txt` |
| `[INIT] No mouse found by auto-detection` | no relative pointer with `BTN_LEFT` is connected |
| `[TOUCHSCREEN] No multi-touch device found ... zoom gestures disabled` | touch controller not loaded |
| `[KEYBOARD] Device 'Amit's Input Provider' not found` | MidiLoop not running, key mappings skipped |

## Zoom does nothing

The zoom is injected into the real multi-touch screen. If the log says `zoom gestures disabled`,
the touch controller did not load at boot; reboot (power off completely, see
[TOUCH_NOT_LOADING.md](TOUCH_NOT_LOADING.md)). If a multi-touch node exists and zoom still fails,
send the output of `probe_inputs` and the `[WHEEL]` lines from the journal.

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

## It is still loaded after DISABLE (version before 2.1.0)

Older `manage.sh` removed only the run script. Remove the entry by hand and restart:

```sh
sed -i 's|/media/662522/AddOns/mouseCursor/libforce_cursor.so||' /dev/shm/.LD_PRELOAD
systemctl restart acvs
```

## Restart or inspect the application

```sh
systemctl restart acvs          # or: respawn
systemctl status acvs --no-pager
```
