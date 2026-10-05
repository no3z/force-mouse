touchFix - gets the Akai Force touchscreen back when it was not detected at boot

Sometimes the Force boots without a touchscreen: the touch controller does not answer at power-up,
so the kernel never uses it. touchFix notices that, resets the controller and reboots ONCE.

INSTALL (no terminal needed)
  Copy touchfix.sh and autoexec.sh to the root of the Force's SD card (next to boot.sh).
  If you already have an autoexec.sh, do not replace it: copy only touchfix.sh and add this line
  to your autoexec.sh:    sh "$(dirname "$0")/touchfix.sh"

TURN OFF
  Delete autoexec.sh (or the line you added). Nothing else was changed.

WHAT IT RECORDS
  touchfix.log in the same folder, one line per boot:
    OK    touch detected          RESET  controller answered after the reset, rebooting once
    BAD   touch not detected      FAIL   the controller did not answer after the reset

SAFETY
  It only drives the controller's reset line; it never writes to the controller. It never reboots
  twice in a row, so a Force whose touch cannot be recovered boots normally.

Documentation: https://github.com/no3z/force-mouse
