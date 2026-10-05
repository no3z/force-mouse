#!/usr/bin/env bash
# Install the add-ons built in dist/ on an Akai Force running MockbaMod, over ssh.
#
#   tools/install.sh <force-ip> [mouse] [touchfix] [-y] [--no-restart]
#
#   mouse         the mouse add-on (the default when no component is named)
#   touchfix      the touchscreen recovery: touchfix.sh and autoexec.sh on the SD card root
#   -y            do not ask before restarting the Force application (mouse only)
#   --no-restart  (mouse) install without restarting; it loads the next time MPC starts
#
# Authentication is whatever ssh does (password prompt, agent, key), asked once. No credential is
# stored or accepted by this script. Run tools/build.sh first.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOST=""
ASSUME_YES=0
RESTART=1
MOUSE=0
TOUCHFIX=0
for arg in "$@"; do
    case "$arg" in
        -y) ASSUME_YES=1 ;;
        --no-restart) RESTART=0 ;;
        mouse) MOUSE=1 ;;
        touchfix) TOUCHFIX=1 ;;
        -*) echo "unknown option: $arg" >&2; exit 2 ;;
        *) HOST="$arg" ;;
    esac
done
[ -n "$HOST" ] || { sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
[ "$MOUSE$TOUCHFIX" = 00 ] && MOUSE=1

DIRS=""
if [ "$MOUSE" = 1 ]; then
    [ -f "$ROOT/dist/mouseCursor/libforce_cursor.so" ] || { echo "dist/mouseCursor not found, run tools/build.sh first" >&2; exit 1; }
    DIRS="$DIRS mouseCursor"
fi
if [ "$TOUCHFIX" = 1 ]; then
    [ -f "$ROOT/dist/touchFix/touchfix.sh" ] || { echo "dist/touchFix not found, run tools/build.sh first" >&2; exit 1; }
    DIRS="$DIRS touchFix"
fi

SD=/media/662522
# FORCE_SSH_OPTS adds ssh options, e.g. FORCE_SSH_OPTS="-o PreferredAuthentications=password"
read -r -a EXTRA_SSH_OPTS <<<"${FORCE_SSH_OPTS:-}"
# One connection is shared by every ssh call below, so the password is asked only once.
CONTROL="$(mktemp -u "${TMPDIR:-/tmp}/force-mouse-ssh.XXXXXX")"
SSH_OPTS=(-o ConnectTimeout=8 -o ControlMaster=auto -o "ControlPath=$CONTROL" -o ControlPersist=60
    ${EXTRA_SSH_OPTS[@]+"${EXTRA_SSH_OPTS[@]}"})
SSH=(ssh "${SSH_OPTS[@]}" "root@$HOST")
trap 'ssh "${SSH_OPTS[@]}" -O exit "root@$HOST" >/dev/null 2>&1 || true' EXIT

echo "Target: root@$HOST (SD card $SD), installing:$DIRS"
"${SSH[@]}" "test -d $SD/MockbaMod && test -d $SD/AddOns" \
    || { echo "MockbaMod not found on the Force SD card ($SD)" >&2; exit 1; }

if [ "$MOUSE" = 1 ] && [ "$RESTART" = 1 ] && [ "$ASSUME_YES" = 0 ]; then
    if [ -t 0 ]; then
        read -r -p "The mouse add-on restarts the Force application; unsaved project changes are lost. Continue? [y/N] " answer
        [ "$answer" = y ] || [ "$answer" = Y ] || { echo "aborted"; exit 1; }
    else
        echo "refusing to restart non-interactively without -y" >&2
        exit 1
    fi
fi

echo "Copying files..."
# shellcheck disable=SC2086
tar -C "$ROOT/dist" -cf - $DIRS | "${SSH[@]}" 'rm -rf /tmp/force-mouse-stage && mkdir -p /tmp/force-mouse-stage && tar -xf - -C /tmp/force-mouse-stage'

"${SSH[@]}" "MOUSE=$MOUSE TOUCHFIX=$TOUCHFIX SD=$SD sh -s" <<'EOF'
set -e
S=/tmp/force-mouse-stage

if [ "$MOUSE" = 1 ]; then
    D=$SD/AddOns/mouseCursor
    mkdir -p "$D"
    # keep the previous library for a quick rollback and the user's configuration
    [ -f "$D/libforce_cursor.so" ] && cp -f "$D/libforce_cursor.so" "$D/libforce_cursor.so.prev"
    [ -f "$D/device.txt" ] && cp -f "$D/device.txt" /tmp/mouseCursor.device.keep
    # MPC has the library mapped while it runs: never rewrite it in place (that can crash MPC),
    # copy under another name and rename, which leaves the mapped file untouched.
    mv "$S/mouseCursor/libforce_cursor.so" "$S/libforce_cursor.so.new"
    cp -rf "$S/mouseCursor/"* "$D/"
    cp -f "$S/libforce_cursor.so.new" "$D/libforce_cursor.so.new"
    mv -f "$D/libforce_cursor.so.new" "$D/libforce_cursor.so"
    if [ -f /tmp/mouseCursor.device.keep ]; then
        cp -f "$D/device.txt" "$D/device.txt.default"
        cp -f /tmp/mouseCursor.device.keep "$D/device.txt"
        rm -f /tmp/mouseCursor.device.keep
        echo "mouse: kept your device.txt (the new default is device.txt.default)"
    fi
    chmod +x "$D/manage.sh" "$D/run_mouseCursor.sh" "$D"/tools/* 2>/dev/null || true
    echo "mouse: installed in $D"
fi

if [ "$TOUCHFIX" = 1 ]; then
    cp -f "$S/touchFix/touchfix.sh" "$SD/touchfix.sh"
    if [ ! -f "$SD/autoexec.sh" ]; then
        cp -f "$S/touchFix/autoexec.sh" "$SD/autoexec.sh"
        echo "touchfix: installed touchfix.sh and created autoexec.sh in $SD"
    elif grep -q "touchfix.sh" "$SD/autoexec.sh"; then
        echo "touchfix: updated touchfix.sh; your autoexec.sh already calls it"
    else
        echo "touchfix: installed touchfix.sh. You already have an autoexec.sh, which was left alone."
        echo "          Add this line to it to turn touchFix on:   sh \"\$(dirname \"\$0\")/touchfix.sh\""
    fi
fi
rm -rf "$S"
sync
EOF

if [ "$MOUSE" = 1 ]; then
    if [ "$RESTART" = 1 ]; then
        echo "Enabling the mouse add-on and restarting the Force application..."
        "${SSH[@]}" "sh $SD/AddOns/mouseCursor/manage.sh ENABLE" || true
    else
        echo "Enabling the mouse add-on without restarting..."
        "${SSH[@]}" "sh -s" <<EOF
set -e
mmPath=\$(cat /dev/shm/.mmPath)
cp -f "\$mmPath/AddOns/mouseCursor/run_mouseCursor.sh" "\$mmPath/AddOns/run_mouseCursor.sh"
cp -f "\$mmPath/AddOns/mouseCursor/device.txt" /dev/shm/.mouseCursor
sh "\$mmPath/AddOns/run_mouseCursor.sh"
echo "enabled; restart the application (or reboot) to load it"
EOF
    fi
fi
echo "Done."
[ "$MOUSE" = 1 ] && echo "Mouse log:     ssh root@$HOST 'journalctl -u acvs --no-pager | grep -E \"\\[BOOT\\]|\\[INIT\\]\"'"
[ "$TOUCHFIX" = 1 ] && echo "Touchfix log:  ssh root@$HOST 'cat $SD/touchfix.log'   (one line per boot, from the next power-up)"
exit 0
