Instructions for mouseCursor Addon
module Credits: @no3z (discord)
Enhanced with Pinch Gestures & Button Mappings - January 2026
------------------------------------------------------------
This AddOn allows you to use a mouse on the Akai Force.

FEATURES:
---------
1. Mouse cursor support with configurable speed
2. Mouse wheel to pinch-to-zoom gestures
3. Configurable mouse button to keyboard key mappings

CONFIGURATION (device.txt):
---------------------------
The device.txt file supports the following format:

Line 1: Mouse input device path (e.g., /dev/input/event2)
Line 2: Cursor speed multiplier (0.1 to 5.0)
Line 3+: Button mappings (optional)
  Format: BUTTON_NAME=KEY_NAME or 0xHEX=0xHEX
  Comments: Lines starting with #
  Empty lines are ignored

Example device.txt:
-------------------
/dev/input/event2
3
# Map side buttons to keyboard keys
BTN_SIDE=KEY_SPACE
BTN_EXTRA=KEY_TAB
BTN_FORWARD=0xf729
BTN_BACK=KEY_ESC

SUPPORTED BUTTON NAMES:
-----------------------
BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA,
BTN_FORWARD, BTN_BACK, BTN_TASK

SUPPORTED KEY NAMES:
--------------------
KEY_SPACE, KEY_TAB, KEY_ENTER, KEY_ESC, KEY_BACKSPACE,
KEY_LEFTSHIFT, KEY_RIGHTSHIFT, KEY_LEFTCTRL, KEY_RIGHTCTRL,
KEY_LEFTALT, KEY_RIGHTALT, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
KEY_PAGEUP, KEY_PAGEDOWN, KEY_HOME, KEY_END, KEY_DELETE, KEY_INSERT

You can also use hex codes (e.g., 0xf729 for special MENU button)

MOUSE WHEEL GESTURES:
---------------------
- Scroll up: Zoom in (pinch fingers apart)
- Scroll down: Zoom out (pinch fingers together)
- Works in MPC browser, sample editor, and other zoom-enabled views

FINDING YOUR MOUSE DEVICE:
---------------------------
Its possible that your Force might have different devices connected and
the actual input device might be different than /dev/input/event2
In that case you will have to find your device id and change event2 to its respective
event number say event4 or whatever.

You can get a list of the input devices using the following command
and you can use the event value from H: parameter:
  command: cat /proc/bus/input/devices

BACKWARD COMPATIBILITY:
-----------------------
The old 2-line format (device path + speed) is still supported.
Button mappings are optional.