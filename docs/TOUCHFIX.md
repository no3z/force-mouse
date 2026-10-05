# Touchscreen recovery (touchFix)

Sometimes the Force boots with no touchscreen. The touch controller did not answer at power-up, the
bootloader enabled no touch node, and the kernel never uses the touchscreen until the next boot.
touchFix notices that at boot and tries to fix it by itself.

It is two small shell scripts on the SD card root. It is not a MockbaMod add-on and changes nothing inside
MockbaMod: it uses `autoexec.sh`, the hook MockbaMod already runs at power-up ([how](MOCKBAMOD.md)).

## What it does

At every power-up, before MPC starts, `autoexec.sh` runs `touchfix.sh`:

1. **Touch controller present** (the kernel has bound it): write an `OK` line to `touchfix.log` and stop.
   This is what happens almost always, and it does almost no work.
2. **Missing:** pulse the controller's reset line (`TOUCH_RST`, GPIO7 pin 5) for 20 ms. Only that line.
   It never writes to the controller or its firmware.
3. **The controller answers again** on its I2C address (`0x26` for the ILI2117, `0x41` for the ILI2116):
   reboot **once**, so the bootloader detects it and the kernel binds it.
4. **Still missing after that reboot:** log it and carry on booting without the touchscreen. It never reboots
   twice in a row, so a Force whose touch cannot be recovered boots normally.

Why a reboot is needed: the kernel binds the touch driver only if the bootloader enables the device-tree node
at boot, and this kernel has no way to enable it later. The evidence and the measurements are in
[TOUCH_NOT_LOADING.md](TOUCH_NOT_LOADING.md).

## Install

Any one of these. They all end up with `touchfix.sh` and `autoexec.sh` in `/media/662522/` (the SD card root,
next to `boot.sh`).

- **By hand, no terminal:** unzip the Release on your computer and copy the two files from `touchFix/` to the
  root of the SD card.
- **From the Force itself:** the commands in the [README](../README.md#install).
- **From a PC with the repository:** `tools/build.sh`, then `tools/install.sh <force-ip> touchfix`.

If you **already have an `autoexec.sh`**, do not replace it. Copy only `touchfix.sh` and add this line to your
file (the installer does not edit it, it tells you):

```sh
sh "$(dirname "$0")/touchfix.sh"
```

It takes effect at the next power-up. Nothing needs restarting.

## Turn it off

Delete `autoexec.sh` (or the line you added). `touchfix.sh` can stay: it does nothing unless something runs it.
To remove everything: delete `touchfix.sh`, `autoexec.sh` and `touchfix.log` from the SD card root.

## The log

`touchfix.log`, in the same folder, one line per boot (it keeps the last 200-300):

```
2026-10-05 19:02:11 OK    touch detected (ili2117 fw 5.0)
2026-10-05 19:20:40 BAD   touch not detected; resetting the controller
2026-10-05 19:20:41 RESET the controller answers again; rebooting once so the kernel binds it
2026-10-05 19:21:35 OK    touch detected (ili2117 fw 5.0) after the automatic reset and reboot: recovered
```

| Word | Meaning |
|---|---|
| `OK` | The touch was detected. With "recovered" it was the boot after an automatic reset and reboot. |
| `BAD` | Not detected. Followed by what it tried, or by "still missing after the automatic reset and reboot". |
| `RESET` | The reset made the controller answer again; it reboots right after this line. |
| `FAIL` | The controller did not answer after the reset (or the reset line could not be taken). It carries on. |

This log is also the only record of how often the problem happens: the system journal only keeps the current boot.
The loop guard uses it too: a boot whose previous line is `RESET ... rebooting once` never reboots again.

## Run it by hand

```sh
sh /media/662522/touchfix.sh
```

Does exactly what happens at boot: nothing if the touch is bound, otherwise reset and reboot. The line it
writes is in `touchfix.log`.

## Limits

- It cannot help if the controller does not answer even after a reset; that is a hardware fault (the flat cable,
  or the touch module). Then `FAIL` lines will show it.
- The cause of the controller not answering at power-up is not known. The measurements are one observation, and a
  plain reboot without the reset was not tried as a control, so it is possible that the reboot alone is what helps.
  The log will tell: keep it for a few weeks.
- It reboots at most once per boot you start yourself. A Force that needs the reset at every boot reboots twice
  per power-up (the second boot finds the touch bound and does nothing).
- It needs `python3` (part of the Force's system) for the I2C check.
- Firmware: written and tested on the Force with MPC 3.9.1.2 and an ILI2117 panel. The ILI2116 address and the
  gpio7 reset pin come from the device tree of the same firmware; not tested on an ILI2116 unit.

## Testing it without a bad boot

For developers. `TOUCHFIX_TEST=1` makes the script act as if the touch were missing and skip the reboot. The
kernel driver holds the reset line, so unbind it first, and bind it again afterwards:

```sh
echo 4-0026 > /sys/bus/i2c/drivers/ili2117/unbind
TOUCHFIX_TEST=1 sh /media/662522/touchfix.sh        # real reset pulse, no reboot
echo 4-0026 > /sys/bus/i2c/drivers/ili2117/bind
cat /media/662522/touchfix.log
```
