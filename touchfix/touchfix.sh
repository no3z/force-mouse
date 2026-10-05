#!/bin/sh
# touchfix.sh - bring the Akai Force touchscreen back when it was not detected at boot.
#
# MockbaMod runs autoexec.sh from the SD card root at power-up, before MPC starts. Keep this file
# next to it and let autoexec.sh call it. Nothing else in MockbaMod is touched.
#
# What it does, in order:
#   1. Touch controller present: write "OK" to touchfix.log and stop.
#   2. Missing: pulse the controller's reset line (TOUCH_RST). Only that line; it never writes to
#      the controller.
#   3. The controller answers again: reboot once, so the kernel binds it.
#   4. Still missing after that reboot: log it and carry on booting. It never reboots twice in a row.
#
# touchfix.log (same folder) has one line per boot. To turn this off, delete autoexec.sh.
# Run it by hand with:  sh /media/662522/touchfix.sh
# For tests only: TOUCHFIX_TEST=1 acts as if the touch were missing and does not reboot.

SD="$(cd "$(dirname "$0")" && pwd)"
LOG="$SD/touchfix.log"
BUS=4

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >>"$LOG"
    if [ $(($(wc -l <"$LOG"))) -gt 300 ]; then # keep the file small
        tail -n 200 "$LOG" >"$LOG.tmp" && mv "$LOG.tmp" "$LOG"
    fi
}

touch_dir() {
    for d in /sys/bus/i2c/devices/$BUS-0026 /sys/bus/i2c/devices/$BUS-0041; do
        [ -e "$d" ] && { echo "$d"; return 0; }
    done
    return 1
}

# does the controller answer on its normal I2C address (0x26 ILI2117, 0x41 ILI2116)?
answers() {
    python3 - "$BUS" <<'PY'
import fcntl, os, sys
fd = os.open("/dev/i2c-" + sys.argv[1], os.O_RDWR)
for a in (0x26, 0x41):
    try:
        fcntl.ioctl(fd, 0x0703, a)  # I2C_SLAVE
        os.read(fd, 1)
        sys.exit(0)
    except OSError:
        pass
sys.exit(1)
PY
}

# true when this boot follows the reboot this script triggered
after_reboot=0
case "$(tail -n 1 "$LOG" 2>/dev/null)" in *"rebooting once"*) after_reboot=1 ;; esac

if [ "${TOUCHFIX_TEST:-0}" != 1 ] && dir="$(touch_dir)"; then
    info="$(cat "$dir/name" 2>/dev/null) fw $(cat "$dir/fw_version" 2>/dev/null)"
    if [ "$after_reboot" = 1 ]; then
        log "OK    touch detected ($info) after the automatic reset and reboot: recovered"
    else
        log "OK    touch detected ($info)"
    fi
    exit 0
fi

if [ "$after_reboot" = 1 ]; then
    log "BAD   touch still missing after the automatic reset and reboot; carrying on without it"
    exit 1
fi

log "BAD   touch not detected; resetting the controller"

# TOUCH_RST is pin 5 of the gpio7 bank; its sysfs number is the bank's base plus 5
base=""
for c in /sys/class/gpio/gpiochip*; do
    [ "$(cat "$c/label" 2>/dev/null)" = "gpio7" ] && base="$(cat "$c/base")"
done
if [ -z "$base" ] || ! command -v python3 >/dev/null 2>&1; then
    log "FAIL  cannot reset: gpio7 bank or python3 not found; carrying on"
    exit 1
fi
rst=$((base + 5))
if ! echo "$rst" >/sys/class/gpio/export 2>/dev/null; then
    log "FAIL  cannot take the reset line (gpio $rst is in use); carrying on"
    exit 1
fi
echo low >"/sys/class/gpio/gpio$rst/direction"
sleep 0.02
echo high >"/sys/class/gpio/gpio$rst/direction"
recovered=0
i=0
while [ "$i" -lt 8 ]; do
    sleep 0.5
    if answers; then recovered=1; break; fi
    i=$((i + 1))
done
echo "$rst" >/sys/class/gpio/unexport 2>/dev/null

if [ "$recovered" != 1 ]; then
    log "FAIL  the controller did not answer after the reset; carrying on"
    exit 1
fi

log "RESET the controller answers again; rebooting once so the kernel binds it"
sync
if [ "${TOUCHFIX_TEST:-0}" = 1 ]; then
    log "TEST  reboot skipped"
    exit 0
fi
systemctl reboot 2>/dev/null || reboot
sleep 120 # keep boot.sh from starting MPC while the reboot is under way
