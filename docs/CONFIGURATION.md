# Configuration: device.txt

The addon reads `/dev/shm/.mouseCursor`, a plain text **file** (not a directory) that
`run_mouseCursor.sh` copies from `AddOns/mouseCursor/device.txt` on every start. Edit the file on
the SD card and restart the Force application; changes made in RAM are lost at the next start.

```
LINE 1   mouse device: "auto" or a path such as /dev/input/event2
LINE 2   speed multiplier, 0.1 to 5.0 (default 1.0 if missing or out of range)
LINE 3+  optional button mappings, "BTN_NAME=KEY_NAME", "BTN_NAME=0xHEX" or "BTN_NAME=MIDI_CC_n"
         empty lines and lines starting with # are ignored; at most 16 mappings
```

## Line 1: the mouse device

`auto` (recommended) picks the first `/dev/input/eventN` that has `REL_X`, `REL_Y` and `BTN_LEFT`,
ignoring the virtual devices on the Force (`Amit's Input Provider`, `Virtual Mouse Touch`).

A fixed path still works, but the event numbers depend on which devices exist at boot. With the
touchscreen present the Force has `event0` touch, `event1` gpio-keys, `event2` mouse, `event3`
Amit's Input Provider. When the touch controller does not load, they become `event0` gpio-keys,
`event1` mouse, `event2` Amit's Input Provider, so `/dev/input/event2` would be the keyboard
provider. Since 2.1.0 a configured path that is not a mouse is replaced by auto-detection and a
warning is logged. A stable alternative to `auto` is the symlink under `/dev/input/by-id/` or
`/dev/input/by-path/`.

## Line 2: speed

Multiplies the raw relative motion. 1.0 is the mouse's own resolution; the original examples use 3.
Lower it for a high-DPI mouse.

## Button mappings

| Button | Typical meaning |
|---|---|
| `BTN_LEFT` | primary click (touch) |
| `BTN_RIGHT` | secondary click |
| `BTN_MIDDLE` | wheel click |
| `BTN_SIDE`, `BTN_EXTRA` | thumb buttons (often "back" / "forward") |
| `BTN_FORWARD`, `BTN_BACK` | navigation buttons, may overlap SIDE/EXTRA depending on the mouse |
| `BTN_TASK` | rare |

A button without a mapping that is `BTN_LEFT`, `BTN_RIGHT` or `BTN_MIDDLE` is sent as a touch at
the cursor. Find your mouse's codes with:

```bash
scp tools/test_mouse_buttons.sh root@<force-ip>:/tmp/ && ssh root@<force-ip> 'bash /tmp/test_mouse_buttons.sh'
```

(it takes the device as an optional argument; by default it uses `/dev/input/event2`, so pass the
right one, see `probe_inputs`).

### Keyboard keys

`BTN_SIDE=KEY_SPACE`. Names: `KEY_ESC SPACE ENTER TAB BACKSPACE LEFTSHIFT RIGHTSHIFT LEFTCTRL
RIGHTCTRL LEFTALT RIGHTALT UP DOWN LEFT RIGHT PAGEUP PAGEDOWN HOME END DELETE INSERT F1..F12 MENU`.
A number also works: `0xf729` or decimal. Keys are written to the device named
`Amit's Input Provider` (created by MidiLoop), which MPC already listens to. If that device does
not exist the mapping is skipped and the log says so.

### MIDI CC

`BTN_RIGHT=MIDI_CC_108`. A Control Change (value 127, on press only) is sent through an ALSA
sequencer port named `MouseButtonMIDI` to client **129 port 0**, which on the tested Force is the
first MidiLoop client (ALSA clients 129-132 are all called `Mockba`). MidiLoop then runs whatever
`midiloop.config` assigns to that CC. If another MIDI device changes the ALSA client numbering,
the CCs go to the wrong client.

CC numbers below come from the original documentation and depend on your `midiloop.config`:

| Group | CCs |
|---|---|
| Transport | 70 play, 71 stop, 72 record, 79 stop-rewind |
| Views | 108 menu, 109 mixer, 110 editor, 111 matrix, 112 clip, 103 editor |
| Scenes | 21-28 launch scene 1-8, 75 previous, 76 next |
| Pages | 41-48 scene pages 1-8, 33-40 track pages 1-8 |
| Tempo | 106 up, 107 down |
| Navigation | 93-96 arrows (left, right, up, down), 89-92 page navigation |
| Utilities | 98 undo, 99 redo, 102 metronome |

## Examples

Basic:

```
auto
3
```

Transport on the side buttons, scenes on forward/back, undo on right click:

```
auto
2.5
BTN_SIDE=MIDI_CC_70
BTN_EXTRA=MIDI_CC_71
BTN_FORWARD=MIDI_CC_76
BTN_BACK=MIDI_CC_75
BTN_RIGHT=MIDI_CC_98
```

## Mouse wheel

Wheel up injects a two-finger zoom-in, wheel down a zoom-out, at the cursor position, into the
**real multi-touch screen** (it needs one; see [ARCHITECTURE.md](ARCHITECTURE.md)). There is no
configuration for it.
