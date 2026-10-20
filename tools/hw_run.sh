#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# One hands-free run on a real console: wiiload the DOL with a beacon argument, collect its logs and shots,
# and succeed only when the app said goodbye on its way back to the Homebrew Channel.
# Run it through ~/Documents/Dev/wii-hw/hw.sh so beads take turns on the consoles.
#   hw_run.sh <console-ip> <dol> <tour> <out-dir> [extra wiiload arguments...]
set -u
ip=$1 dol=$2 tour=$3 out=$4
shift 4
port=47110
host=$(ip -4 route get "$ip" | sed -n 's/.* src \([0-9.]*\).*/\1/p')
python3 "$(dirname "$0")/beacon.py" --port "$port" --out "$out" --timeout 300 &
listener=$!
sleep 1
if ! WIILOAD="tcp:$ip" "${DEVKITPRO:-/opt/devkitpro}/tools/bin/wiiload" "$dol" "beacon=$host:$port" "tour=$tour" "$@"; then
    kill "$listener"
    wait "$listener"
    exit 2
fi
wait "$listener"
