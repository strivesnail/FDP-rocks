#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/third_party/rocksdb"
REVISION="f3fc05908517e4b3346fb690d3a97d69877fec50"

if test -e "${DEST}"; then
  printf 'ERROR: destination already exists: %s\n' "${DEST}" >&2
  exit 1
fi

mkdir -p "$(dirname "${DEST}")"
git clone https://github.com/strivesnail/rocksdb.git "${DEST}"
git -C "${DEST}" checkout --detach "${REVISION}"
printf 'Checked out FDP-Rocks revision %s\n' "${REVISION}"
