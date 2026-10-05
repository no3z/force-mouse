# How force-mouse fits into MockbaMod

MockbaMod starts everything from `boot.sh` on the SD card. Knowing when each
piece runs explains how to turn things on, off and keep them automatic.

## What runs when

| When | What MockbaMod runs | Used by |
|---|---|---|
| Once per power-up, before MPC | `/media/662522/autoexec.sh` (if it exists) | **touchFix** |
| Every time MPC starts (also when you create a new project) | every `/media/662522/AddOns/*.sh` ("run scripts") | **mouse** |
| Right before MPC | `LD_PRELOAD` is set to the libraries the run scripts listed in `/dev/shm/.LD_PRELOAD` | **mouse** |

Both are automatic: nothing has to be started by hand after a reboot.

## Mouse: a normal MockbaMod add-on

It lives in `AddOns/mouseCursor/` like the other add-ons (MidiLoop, mockbaMagic), with a `manage.sh` and a
`VERSION`. There are three equivalent ways to turn it on or off:

- **`adm`**, MockbaMod's add-on manager (run `adm` in a terminal on the Force): choose `mouseCursor`, then
  Enable or Disable. It lists add-ons by finding `AddOns/*/manage.sh`, so a folder copied into `AddOns/`
  shows up by itself.
- **`manage.sh`** directly: `sh /media/662522/AddOns/mouseCursor/manage.sh ENABLE` (or `DISABLE`).
- **By hand:** copy `AddOns/mouseCursor/run_mouseCursor.sh` to `AddOns/run_mouseCursor.sh`. That copy (plus
  restarting the application) is all `ENABLE` does; deleting it, and taking the library out of
  `/dev/shm/.LD_PRELOAD`, is `DISABLE`.

`ENABLE` and `DISABLE` restart the Force application, because the library is injected when MPC starts.
After that it is automatic: `boot.sh` runs the run script at every start, which adds the library to
`LD_PRELOAD`. Its settings are in `AddOns/mouseCursor/device.txt` ([configuration](CONFIGURATION.md)).

## touchFix: the `autoexec.sh` hook

`autoexec.sh` in the SD card root is MockbaMod's documented hook for "run my commands at power-up".
touchFix uses nothing else: `autoexec.sh` is three lines that call `touchfix.sh`. It is not an add-on, so it
does not appear in `adm`, and there is nothing to enable except having the file: **copied = on, deleted =
off.** Details: [TOUCHFIX.md](TOUCHFIX.md).

## Automating the installation itself

| What | How |
|---|---|
| Everything from a PC, one command | `tools/install.sh <force-ip> mouse touchfix` (password asked once) |
| Everything from the Force, no PC | the `curl` + `unzip` block in the [README](../README.md#install) |
| Building the release | pushing a `v*` tag runs the GitHub Actions workflow, which builds from that commit and publishes the zip |
| Keeping it | the files live on the SD card (root and `AddOns/`); if you rebuild or replace the SD card, copy them again |

## What this repository does not touch

- Nothing inside `MockbaMod/` (no file added, edited or removed).
- `boot.sh`, `env.sh` and the other add-ons.
- An `autoexec.sh` you already have: the installer leaves it alone and tells you the one line to add.
- The touch controller's firmware: touchFix only drives its reset line.

The only things added are `AddOns/mouseCursor/`, `AddOns/run_mouseCursor.sh` (written by the mouse
`ENABLE`), `autoexec.sh`, `touchfix.sh` and `touchfix.log`, plus the runtime files MPC itself keeps in
`/dev/shm` (RAM, gone after a reboot).
