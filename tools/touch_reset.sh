#!/bin/sh
# touch_reset.sh - bring back an Ilitek touch controller that did not answer at boot.
#
#   sh touch_reset.sh
#
# Run it on the Force, as root. If the kernel has no touch controller bound (neither 4-0026 nor
# 4-0041 under /sys/bus/i2c/devices), it pulses the controller's reset line (TOUCH_RST, GPIO7 pin 5)
# and then looks for the controller on its normal I2C address (0x26 ILI2117, 0x41 ILI2116).
# It only drives the reset line: it never writes to the controller and never touches TOUCH_INT.
#
# The kernel binds the touch driver only when U-Boot enables the device-tree node at boot, and this
# kernel cannot enable it later, so a reboot is still needed after a successful reset.
#
# Exit status: 0 touch already bound, or the controller answered after the reset; 1 it did not;
# 2 cannot run (no GPIO bank, line in use, no python3).
set -u

BUS=4
RST_PIN=5 # gpio7 bank, TOUCH_RST in the device tree
LOW_MS=20

bound() { [ -e "/sys/bus/i2c/devices/$BUS-0026" ] || [ -e "/sys/bus/i2c/devices/$BUS-0041" ]; }

if bound; then
    echo "touch controller already bound by the kernel, nothing to do"
    exit 0
fi

command -v python3 >/dev/null 2>&1 || { echo "python3 is required (I2C scan)"; exit 2; }

base=""
for c in /sys/class/gpio/gpiochip*; do
    [ "$(cat "$c/label" 2>/dev/null)" = "gpio7" ] && base="$(cat "$c/base")"
done
[ -n "$base" ] || { echo "GPIO bank gpio7 not found"; exit 2; }
rst=$((base + RST_PIN))

if ! echo "$rst" >/sys/class/gpio/export 2>/dev/null; then
    echo "cannot export GPIO $rst (in use by a driver?), not touching it"
    exit 2
fi
trap 'echo "$rst" >/sys/class/gpio/unexport 2>/dev/null' EXIT

scan() {
    python3 - "$BUS" <<'EOF'
import fcntl, os, sys
fd = os.open("/dev/i2c-" + sys.argv[1], os.O_RDWR)
found = []
for a in (0x26, 0x41, 0x62):
    try:
        fcntl.ioctl(fd, 0x0703, a)
        os.read(fd, 1)
        found.append(hex(a))
    except OSError:
        pass
print(" ".join(found))
EOF
}

echo "before: i2c-$BUS answers on: $(scan)"
echo low >"/sys/class/gpio/gpio$rst/direction"
sleep "0.0$((LOW_MS / 10))"
echo high >"/sys/class/gpio/gpio$rst/direction"

i=0
while [ "$i" -lt 8 ]; do
    sleep 0.5
    now="$(scan)"
    case " $now " in
    *" 0x26 "* | *" 0x41 "*)
        echo "after reset: i2c-$BUS answers on: $now"
        echo "the controller is back on its normal address; reboot so the kernel binds it"
        exit 0
        ;;
    esac
    i=$((i + 1))
done
echo "after reset: i2c-$BUS answers on: ${now:-nothing}"
echo "the controller did not come back on its normal address"
exit 1
