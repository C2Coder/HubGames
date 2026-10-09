#!/bin/bash
# Writes doom1.wad to the doomwad partition (once, the app flash doesn't touch it).
# Usage: doom/flash-wad.sh [port], run inside an ESP-IDF shell (. export.sh)
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f doom/doom1.wad ] || doom/get-wad.sh
parttool.py --port "${1:-/dev/ttyACM0}" --partition-table-file partitions.csv write_partition --partition-name doomwad --input doom/doom1.wad
