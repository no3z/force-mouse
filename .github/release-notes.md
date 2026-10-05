Two independent fixes for the Akai Force running MockbaMod. Use either one, or both.

- **Mouse support**: hardware cursor, clicks and drags, wheel as pinch-to-zoom, mouse buttons to keys or MIDI CC.
  A MockbaMod add-on (`mouseCursor/`).
- **Touchscreen recovery (touchFix)**: if the Force boots without its touchscreen, resets the touch controller
  and reboots once. Two shell scripts (`touchFix/`) that go to the SD card root.

**Tested on** MPC 3.9.1.2 (MockbaMod 4.51, kernel 6.18.26-az01-rt4). Not tested on other firmware.

## Install from the Force itself (SSH as root, internet access, no Docker)

```sh
cd /tmp && curl -L -o fm.zip https://github.com/no3z/force-mouse/releases/latest/download/force-mouse-armv7.zip
unzip -oq fm.zip
# touchFix: two files on the SD card root (keeps an autoexec.sh you already have)
cp touchFix/touchfix.sh /media/662522/ && [ -f /media/662522/autoexec.sh ] || cp touchFix/autoexec.sh /media/662522/
# mouse: copy the add-on and enable it (restarts the Force application: unsaved project changes are lost)
cp -r mouseCursor /media/662522/AddOns/ && sh /media/662522/AddOns/mouseCursor/manage.sh ENABLE
```

Or copy `touchFix/touchfix.sh` and `touchFix/autoexec.sh` to the SD card root and `mouseCursor/` to `AddOns/` from
your computer. To upgrade a running mouse add-on use `tools/install.sh` from the repository. Check the download
with `sha256sum -c --ignore-missing SHA256SUMS`.

Documentation: https://github.com/no3z/force-mouse#documentation

Built by GitHub Actions from the tagged commit with `tools/build.sh`.
Copyright (c) 2026 no3productionz, MIT License. Third-party notices: `NOTICE.md` (inside the zip).
