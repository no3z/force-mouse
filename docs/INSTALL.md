# Build and install

There are two components, installed independently:

- **mouse**: the add-on `AddOns/mouseCursor/` (needs a restart of the Force application to start)
- **touchfix**: `touchfix.sh` and `autoexec.sh` on the SD card root (acts at the next power-up)

How they plug into MockbaMod and what is automatic: [MOCKBAMOD.md](MOCKBAMOD.md).

## Easiest: from the Force itself (no PC tools, no Docker)

Needs a terminal on the Force (SSH as root) and internet access from the Force. The Force has `curl` and `unzip`.

```sh
cd /tmp && curl -L -o fm.zip https://github.com/no3z/force-mouse/releases/latest/download/force-mouse-armv7.zip
unzip -oq fm.zip
# touchFix
cp touchFix/touchfix.sh /media/662522/ && [ -f /media/662522/autoexec.sh ] || cp touchFix/autoexec.sh /media/662522/
# mouse (restarts the Force application)
cp -r mouseCursor /media/662522/AddOns/ && sh /media/662522/AddOns/mouseCursor/manage.sh ENABLE
```

Run only the part you want. If you already have an `autoexec.sh`, the touchFix line leaves it alone; add
`sh "$(dirname "$0")/touchfix.sh"` to it yourself. This is for a first install; to upgrade the mouse add-on use
the next section, because the library is mapped into MPC while it runs and must not be overwritten in place.

## From a computer, without a terminal on the Force

Download and unzip the Release. Then copy to the SD card:

- touchFix: `touchFix/touchfix.sh` and `touchFix/autoexec.sh` to the SD card root (beside `boot.sh`).
- mouse: the `mouseCursor` folder to `AddOns/`, then enable it (see [MOCKBAMOD.md](MOCKBAMOD.md#mouse-a-normal-mockbamod-add-on)).

## From a computer with the repository

Requirements: Linux or macOS with Docker (the toolchain lives in an image), `zip` and `ssh`.

```bash
git clone https://github.com/no3z/force-mouse.git && cd force-mouse
tools/build.sh
tools/install.sh <force-ip> mouse touchfix      # either word alone installs just that one
```

`install.sh` asks for the SSH password **once** (all its steps share one connection) and never stores it. To force
password authentication when your ssh agent offers many keys:
`FORCE_SSH_OPTS="-o PreferredAuthentications=password" tools/install.sh <force-ip> mouse`.

What it does:

- **mouse:** copies the add-on to `AddOns/mouseCursor/`, keeps your `device.txt` (the new default is saved as
  `device.txt.default`) and the previous library as `libforce_cursor.so.prev`, replaces the library by rename (never
  in place), and runs `manage.sh ENABLE`, which restarts the Force application. **Unsaved project changes are
  lost.** `-y` skips the question; `--no-restart` installs without restarting, and the add-on loads the next
  time MPC starts.
- **touchfix:** puts `touchfix.sh` on the SD card root, and creates `autoexec.sh` only if there is none. If you
  have one, it is left alone and the installer prints the line to add.

## The build

`tools/build.sh` builds `force-mouse-builder:bookworm` from `tools/Dockerfile` on first use (Debian bookworm,
`gcc-arm-linux-gnueabihf`, `libdrm-dev:armhf`, `libasound2-dev:armhf`), compiles for ARMv7 hard-float
(`-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard`), and checks the result: ELF32 ARM, needed libraries
`libasound.so.2`, `libdrm.so.2` and `libc.so.6`, and no glibc symbol newer than 2.36 (the Force runs 2.39). It
stages `dist/mouseCursor/`, `dist/touchFix/`, `dist/force-mouse-<version>-armv7.zip` and `dist/SHA256SUMS`.
Cold, the toolchain image takes about half a minute to build; after that a build takes about 2 seconds.

Why bookworm: `debian:bullseye` returns 404 for its armhf security packages now. The kernel version does not
matter: the library only uses stable userspace interfaces. The version number is the `VERSION` file at the
root; one number for the whole package.

Manual build, without the script:

```bash
arm-linux-gnueabihf-gcc -shared -fPIC -O2 -Wall -Wextra \
  -I/usr/include/libdrm -I/usr/include/arm-linux-gnueabihf \
  -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard \
  -o libforce_cursor.so src/force_cursor.c -ldl -lpthread -lasound -ldrm
```

## Releases

Pushing a tag that matches `VERSION` (for `3.1.0`, the tag `v3.1.0`) runs `.github/workflows/release.yml`:
it builds on GitHub from that commit and publishes the zip (also as `force-mouse-armv7.zip`, a name without the
version for the "latest" link) and `SHA256SUMS` as a Release. No personal token is involved.
Check a download with `sha256sum -c --ignore-missing SHA256SUMS`.

## Verify

Mouse:

```sh
cat /dev/shm/.LD_PRELOAD                              # lists .../mouseCursor/libforce_cursor.so
grep -c libforce_cursor /proc/$(pidof MPC)/maps       # > 0: loaded in MPC
journalctl -u acvs --no-pager | grep -E "\[BOOT\]|\[INIT\]|\[GUARD\]"
awk '/^plane\[35\]/{f=1} /^plane\[38\]/{f=0} f' /sys/kernel/debug/dri/*/state 2>/dev/null | grep -E 'crtc=|fb=|crtc-pos'
```

`crtc=crtc-0` with a 64x64 `AR24` framebuffer means the cursor is on; `crtc-pos` follows the mouse. The journal
unit is `acvs`. Debugfs is `dri/0` or `dri/1` depending on the boot.

touchFix: `cat /media/662522/touchfix.log` (one line per boot, from the first power-up after installing).

Tools in `/media/662522/AddOns/mouseCursor/tools/`:

```sh
probe_inputs                      # which event node is used for the mouse and for key injection
drm_planes                        # CRTC and planes: types, formats, state
drm_screenshot /tmp/screen.ppm    # what the Force is showing (landscape PPM; -r keeps the panel orientation)
evdump /dev/input/eventN 10       # print a device's events for 10 s (evtest is not installed)
fake_mouse                        # virtual mouse driven by a FIFO, for testing without hardware
```

## Turn off, uninstall, roll back

- **touchFix:** delete `autoexec.sh` from the SD card root. Remove `touchfix.sh` and `touchfix.log` too if you
  want it gone.
- **mouse:** `sh /media/662522/AddOns/mouseCursor/manage.sh DISABLE` (or Disable in `adm`). It removes the run
  script, the RAM configuration and the library from `/dev/shm/.LD_PRELOAD`, then restarts the Force application.
  Delete `AddOns/mouseCursor` afterwards to remove the files. To roll back a bad build, copy
  `libforce_cursor.so.prev` over `libforce_cursor.so` and restart the application.
- **The Force shows nothing after installing the mouse add-on:** SSH in (sshd does not depend on MPC), run the
  `DISABLE` command above, then `systemctl reset-failed acvs; systemctl restart acvs`.
