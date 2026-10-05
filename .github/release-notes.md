USB mouse support for the Akai Force running MockbaMod: hardware cursor, clicks and drags, wheel as
pinch-to-zoom, and mouse buttons mapped to keys or MIDI CC.

**Tested on** MPC 3.9.1.2 (MockbaMod 4.51, kernel 6.18.26-az01-rt4). Not tested on other firmware.

## Install without Docker

```bash
unzip force-mouse-*-armv7.zip
tar -cf - mouseCursor | ssh root@<force-ip> 'tar -xf - -C /media/662522/AddOns && sh /media/662522/AddOns/mouseCursor/manage.sh ENABLE'
```

This restarts the Force application (unsaved project changes are lost). To upgrade a running install
use `tools/install.sh` from the repository instead, or disable it first with
`sh /media/662522/AddOns/mouseCursor/manage.sh DISABLE`. Check the download with
`sha256sum -c --ignore-missing SHA256SUMS`.

Full documentation: https://github.com/no3z/force-mouse#documentation

Built by GitHub Actions from the tagged commit with `tools/build.sh`.
Copyright (c) 2026 no3productionz, MIT License. Third-party notices: `NOTICE.md` (inside the zip).
