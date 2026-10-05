#!/usr/bin/env bash
# Install dist/mouseCursor on an Akai Force running MockbaMod, then enable it.
#
#   tools/install.sh <force-ip> [-y] [--no-restart]
#
#   -y            do not ask before restarting the Force application
#   --no-restart  copy the files and write the preload entry, but do not restart
#                 (the addon is picked up the next time MPC starts)
#
# Authentication is whatever ssh does (password prompt, agent, key). No credential is
# stored or accepted by this script. Run tools/build.sh first.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOST=""
ASSUME_YES=0
RESTART=1
for arg in "$@"; do
    case "$arg" in
        -y) ASSUME_YES=1 ;;
        --no-restart) RESTART=0 ;;
        -*) echo "unknown option: $arg" >&2; exit 2 ;;
        *) HOST="$arg" ;;
    esac
done
[ -n "$HOST" ] || { sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

STAGE="$ROOT/dist/mouseCursor"
[ -f "$STAGE/libforce_cursor.so" ] || { echo "dist/mouseCursor not found, run tools/build.sh first" >&2; exit 1; }

SD=/media/662522
# FORCE_SSH_OPTS adds ssh options, e.g. FORCE_SSH_OPTS="-o PreferredAuthentications=password"
read -r -a EXTRA_SSH_OPTS <<<"${FORCE_SSH_OPTS:-}"
# One connection is shared by every ssh call below, so the password is asked only once.
CONTROL="$(mktemp -u "${TMPDIR:-/tmp}/force-mouse-ssh.XXXXXX")"
SSH_OPTS=(-o ConnectTimeout=8 -o ControlMaster=auto -o "ControlPath=$CONTROL" -o ControlPersist=60
    ${EXTRA_SSH_OPTS[@]+"${EXTRA_SSH_OPTS[@]}"})
SSH=(ssh "${SSH_OPTS[@]}" "root@$HOST")
trap 'ssh "${SSH_OPTS[@]}" -O exit "root@$HOST" >/dev/null 2>&1 || true' EXIT

echo "Target: root@$HOST ($SD/AddOns/mouseCursor)"
"${SSH[@]}" "test -d $SD/MockbaMod && test -d $SD/AddOns" \
    || { echo "MockbaMod not found on the Force SD card ($SD)" >&2; exit 1; }

if [ "$RESTART" = 1 ] && [ "$ASSUME_YES" = 0 ]; then
    if [ -t 0 ]; then
        read -r -p "This restarts the Force application; unsaved project changes are lost. Continue? [y/N] " answer
        [ "$answer" = y ] || [ "$answer" = Y ] || { echo "aborted"; exit 1; }
    else
        echo "refusing to restart non-interactively without -y" >&2
        exit 1
    fi
fi

echo "Copying files..."
tar -C "$ROOT/dist" -cf - mouseCursor | "${SSH[@]}" 'rm -rf /tmp/force-mouse-stage && mkdir -p /tmp/force-mouse-stage && tar -xf - -C /tmp/force-mouse-stage'

"${SSH[@]}" "sh -s" <<EOF
set -e
SD=$SD
D=\$SD/AddOns/mouseCursor
mkdir -p "\$D"
# keep the previous library for a quick rollback and the user's configuration
[ -f "\$D/libforce_cursor.so" ] && cp -f "\$D/libforce_cursor.so" "\$D/libforce_cursor.so.prev"
[ -f "\$D/device.txt" ] && cp -f "\$D/device.txt" /tmp/mouseCursor.device.keep
# MPC has the library mapped while it runs: never rewrite it in place (that can crash MPC),
# copy under another name and rename, which leaves the mapped file untouched.
mv /tmp/force-mouse-stage/mouseCursor/libforce_cursor.so /tmp/force-mouse-stage/libforce_cursor.so.new
cp -rf /tmp/force-mouse-stage/mouseCursor/* "\$D/"
cp -f /tmp/force-mouse-stage/libforce_cursor.so.new "\$D/libforce_cursor.so.new"
mv -f "\$D/libforce_cursor.so.new" "\$D/libforce_cursor.so"
if [ -f /tmp/mouseCursor.device.keep ]; then
    cp -f "\$D/device.txt" "\$D/device.txt.default"
    cp -f /tmp/mouseCursor.device.keep "\$D/device.txt"
    rm -f /tmp/mouseCursor.device.keep
    echo "kept existing device.txt (new default saved as device.txt.default)"
fi
chmod +x "\$D/manage.sh" "\$D/run_mouseCursor.sh" "\$D"/tools/* 2>/dev/null || true
rm -rf /tmp/force-mouse-stage
sync
echo "installed in \$D"
EOF

if [ "$RESTART" = 1 ]; then
    echo "Enabling and restarting the Force application..."
    "${SSH[@]}" "sh $SD/AddOns/mouseCursor/manage.sh ENABLE" || true
else
    echo "Enabling without restart..."
    "${SSH[@]}" "sh -s" <<EOF
set -e
mmPath=\$(cat /dev/shm/.mmPath)
cp -f "\$mmPath/AddOns/mouseCursor/run_mouseCursor.sh" "\$mmPath/AddOns/run_mouseCursor.sh"
cp -f "\$mmPath/AddOns/mouseCursor/device.txt" /dev/shm/.mouseCursor
sh "\$mmPath/AddOns/run_mouseCursor.sh"
echo "enabled; restart the application (or reboot) to load it"
EOF
fi
echo "Done. Check the log with:  ssh root@$HOST 'journalctl -u acvs --no-pager | grep -E \"Mouse Cursor|\\[INIT\\]|\\[TOUCHSCREEN\\]\"'"
