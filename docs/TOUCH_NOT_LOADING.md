# The touchscreen sometimes does not load at boot

This is the reason a mouse is needed at all on some Forces, and the reason the input event numbers
change between boots. Findings below come from one affected boot (2026-10-05) of a Force on MPC
3.9.1.2, kernel `6.18.26-az01-2026-04-30-rt4`. **The root cause is not proven**; evidence and
hypotheses are kept apart.

## Evidence

**The touch controller never gets a driver.** The boot service that handles touch firmware
logged this, before MPC was even started:

```
Oct 05 17:05:29 touch-fw-upd version 1.4
Oct 05 17:05:29 touch-fw-upd: no supported touch panel installed
```

`touch-fw-update.service` is a oneshot ordered before `acvs.service` (`After=touch-fw-update.service`
in the unit). `touch-fw-upd` looks for the panel at `/sys/bus/i2c/devices/4-0026` (Ilitek ILI2117)
or `4-0041` (ILI2116). Neither exists. Bus 4 is `ff160000.i2c`.

**All candidate nodes are disabled in the live device tree.** `/sys/firmware/devicetree/base/i2c@ff160000/`
contains `ili2116@41`, `ili2117@26` and `st1727@55`, each with `status = "disabled"`
(product code `ADA2`). Presumably something earlier in the boot enables the one matching the
detected panel; that was not verified. The kernel has the drivers (`ili210x_i2c`, `ili2116`,
`ili2117`, `ili2131`, `st1232-ts` in `/sys/bus/i2c/drivers`).

**The controller does not answer at its expected addresses.** A read-only probe of `/dev/i2c-4`
(one byte read per address, the same thing `i2cdetect` does):

```
0x26 no ack     0x41 no ack     0x55 no ack
ACK addresses on i2c-4: ['0x62']
```

Something does answer at `0x62`, which is not one of the expected addresses. It may be the touch
IC in an unexpected mode (a bootloader or recovery mode, for example); that is a guess.

**Resulting inputs.** Only `gpio-keys`, the USB mouse and `Amit's Input Provider` exist, so every
event number is one lower than on a good boot. A fixed `/dev/input/event2` for the mouse then points
at the keyboard provider.

**Restarting MPC cannot help.** Detection happens before MPC starts, and `systemctl restart acvs`
does not rerun `touch-fw-update.service`.

Not measured: how often it happens (the journal only keeps the current boot) and what a good boot
looks like on the same unit.

**What the addon does about it.** Since 3.0.0 the addon sends clicks, drags and the zoom gesture
through its own virtual touch screen, so a Force whose physical touch controller did not load can
still be driven with a mouse. It does not fix the controller: see below.

## Hypotheses, most likely first

1. **Contact on the display/touch flat cable (FFC) or connector.** The classic cause of a touch
   panel that is there on some boots and absent on others.
2. **The touch IC left in a bad state by an unclean power-off**, then not answering the normal
   address until it is fully power cycled. The same boot logged `Volume was not properly unmounted`
   for three USB volumes and an ext4 error count on `loop0`, which points at power being cut
   without a clean shutdown at some time. Circumstantial only.
3. **Marginal power at boot** (reset or supply timing of the touch IC).

## What to try

- Power off from the menu, unplug the power supply for 30 s or more, then start again. A soft
  reboot or restarting the application does not reset the panel.
- Shut the Force down from the menu instead of cutting power.
- Try another power supply.
- If it keeps happening, reseat the flat cable or contact support (hardware fault).

## How to check a boot yourself

```sh
journalctl -b -u touch-fw-update --no-pager        # "no supported touch panel installed" = bad boot
ls /sys/bus/i2c/devices/                           # 4-0026 or 4-0041 present = good boot
grep -E '^(N|H):' /proc/bus/input/devices          # a touch device should be listed
/media/662522/AddOns/mouseCursor/tools/probe_inputs   # abs_mt=1 on a node other than 'Virtual Mouse Touch' = physical touchscreen present
```

The raw I2C probe (read-only; bus 4 had only the touch controller on the tested unit):

```sh
python3 - <<'EOF'
import os, fcntl
fd = os.open("/dev/i2c-4", os.O_RDWR)
found = []
for a in range(0x08, 0x78):
    try:
        fcntl.ioctl(fd, 0x0703, a)   # I2C_SLAVE
        os.read(fd, 1)
        found.append(hex(a))
    except OSError:
        pass
print("ACK addresses on i2c-4:", found or "NONE")
EOF
```

Comparing the output of a good boot and a bad boot of the same unit would separate a hardware
fault from a boot-timing problem; contributions welcome.
