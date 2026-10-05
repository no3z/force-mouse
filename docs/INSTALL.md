# Build and install

> Check [COMPATIBILITY.md](COMPATIBILITY.md) first: on Force firmware 3.9.1.2 the addon loads but
> its hooks never fire.

## Requirements

- PC: Linux or macOS with Docker (the cross toolchain lives in an image; nothing is installed on
  the host), `zip`, `ssh`.
- Force: MockbaMod on the SD card (`/media/662522` with `boot.sh` and `AddOns/`), SSH as root.

## Build

```bash
tools/build.sh
```

The first run builds `force-mouse-builder:bookworm` from `tools/Dockerfile` (Debian bookworm,
`gcc-arm-linux-gnueabihf`, `libdrm-dev:armhf`, `libasound2-dev:armhf`). Then it compiles and checks:

- `build/libforce_cursor.so` and `build/probe_inputs`: ELF32 ARM, hard-float
  (`-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard`), stripped
- needed libraries: `libasound.so.2` and `libc.so.6` (MPC already has `libdrm` and `libasound`
  loaded)
- the highest glibc symbol must be <= 2.36 (the Force runs 2.39); the build fails otherwise

and stages `dist/mouseCursor/`, `dist/force-mouse-<version>-armv7.zip` and `dist/SHA256SUMS`.

Why bookworm: the earlier notes used `debian:bullseye`, whose armhf security packages now return
404 and break `apt`. Bookworm's glibc (2.36) is older than the Force's (2.39), so the binaries load.

The kernel version does not matter for the build: the library only uses stable userspace ABIs
(DRM, evdev, uinput, ALSA sequencer). It was verified on `6.18.26-az01-2026-04-30-rt4`.

Manual build, without the script:

```bash
arm-linux-gnueabihf-gcc -shared -fPIC -O2 -Wall -Wextra \
  -I/usr/include/libdrm -I/usr/include/arm-linux-gnueabihf \
  -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard \
  -o libforce_cursor.so src/force_cursor.c -ldl -lpthread -lasound
```

## Install with the script

```bash
tools/install.sh <force-ip>            # asks before restarting the Force application
tools/install.sh <force-ip> -y         # no question
tools/install.sh <force-ip> --no-restart
```

ssh asks for the password. To force password authentication when your ssh agent offers many
keys: `FORCE_SSH_OPTS="-o PreferredAuthentications=password" tools/install.sh <force-ip>`.
Nothing in this repository stores or accepts a password.

What it does on the Force:

1. Copies the files to `/media/662522/AddOns/mouseCursor/`.
2. Keeps the previous library as `libforce_cursor.so.prev` and keeps your existing `device.txt`
   (the new default is saved as `device.txt.default`).
3. Runs `manage.sh ENABLE`: copies `run_mouseCursor.sh` to `AddOns/` and `device.txt` to
   `/dev/shm/.mouseCursor`, then calls `respawn`, which restarts the MPC application.
   **Unsaved project changes are lost.**

With `--no-restart` the run script is executed once so `/dev/shm/.LD_PRELOAD` is updated, and the
addon loads the next time MPC starts.

## Install by hand

```bash
tar -C dist -cf - mouseCursor | ssh root@<force-ip> 'tar -xf - -C /media/662522/AddOns'
ssh root@<force-ip> 'sh /media/662522/AddOns/mouseCursor/manage.sh ENABLE'
```

The SD card is FAT/exFAT: files show as executable through the mount options, `chmod` has no
lasting effect.

## How MockbaMod loads it

`boot.sh` (run by `az01-launch-MPC` under `acvs.service`) executes every `AddOns/*.sh` before it
starts MPC. `run_mouseCursor.sh` prepends `libforce_cursor.so` to `/dev/shm/.LD_PRELOAD` and copies
`device.txt` to `/dev/shm/.mouseCursor`; `boot.sh` then exports `LD_PRELOAD` from that file and
launches `/usr/bin/MPC`. `/dev/shm` is RAM, so this is rebuilt on every start.

## Verify

```bash
ssh root@<force-ip> '
  cat /dev/shm/.LD_PRELOAD                              # lists .../mouseCursor/libforce_cursor.so
  cat /dev/shm/.mouseCursor | head -n 3                 # the active configuration
  grep -c libforce_cursor /proc/$(pidof MPC)/maps       # > 0: loaded in MPC
  journalctl -u acvs --no-pager | grep -E "Mouse Cursor|\[INIT\]|\[TOUCHSCREEN\]|CURSOR_PATCH"
  grep -c "Virtual Mouse Touch" /proc/bus/input/devices # 1 once the input thread started
'
```

The journal unit is `acvs` (`systemctl status acvs`). The `inmusic-mpc` name used by some other
addons' `manage.sh` does not exist on firmware 3.9.1.2.

`dist/mouseCursor/probe_inputs` can be run on the Force at any time to see which event node would
be used for the mouse, the touchscreen and key injection.

## Disable, uninstall, roll back

```bash
ssh root@<force-ip> 'sh /media/662522/AddOns/mouseCursor/manage.sh DISABLE'
```

`DISABLE` removes the run script, the RAM configuration and the entry in `/dev/shm/.LD_PRELOAD`,
then restarts the application (before 2.1.0 the preload entry stayed until a reboot). To remove
the files too, delete `/media/662522/AddOns/mouseCursor` afterwards. To roll back a bad build,
copy `libforce_cursor.so.prev` over `libforce_cursor.so` and restart the application.
