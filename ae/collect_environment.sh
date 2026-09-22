#!/usr/bin/env bash
set -euo pipefail

OUTPUT="${1:-environment.txt}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

first_line() {
  IFS= read -r line || true
  printf '%s\n' "${line:-}"
}

{
  printf 'Collected (UTC): %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf '\n[Operating system]\n'
  if test -r /etc/os-release; then
    cat /etc/os-release
  fi
  uname -a

  printf '\n[CPU]\n'
  lscpu | awk -F: '
    /Architecture|CPU\\(s\\)|Model name|Socket\\(s\\)|Core\\(s\\) per socket|Thread\\(s\\) per core/ {
      gsub(/^[ \t]+|[ \t]+$/, "", $2)
      print $1 ": " $2
    }'

  printf '\n[Memory]\n'
  free -h

  printf '\n[Toolchain]\n'
  "${CXX:-g++}" --version | first_line
  cmake --version | first_line
  python3 --version
  nvme version

  printf '\n[Source revisions]\n'
  git -C "${ROOT}" rev-parse HEAD 2>/dev/null || printf 'Artifact revision unavailable.\n'
  if test -d "${ROOT}/third_party/rocksdb/.git"; then
    git -C "${ROOT}/third_party/rocksdb" rev-parse HEAD
  else
    printf 'FDP-Rocks source not fetched.\n'
  fi

  printf '\n[Filesystem]\n'
  if test -n "${FDP_MOUNT_POINT:-}"; then
    findmnt -o SOURCE,TARGET,FSTYPE,OPTIONS --target "${FDP_MOUNT_POINT}"
    df -h "${FDP_MOUNT_POINT}"
  else
    printf 'FDP_MOUNT_POINT is not set.\n'
  fi

  printf '\n[NVMe controller, serial number omitted]\n'
  if test -n "${FDP_DEVICE:-}"; then
    nvme id-ctrl "${FDP_DEVICE}" -o json |
      python3 -c 'import json,sys; d=json.load(sys.stdin); print(json.dumps({k:d.get(k) for k in ("mn","fr","vid","ssvid")}, indent=2))'
    nvme fdp configs "${FDP_DEVICE}" -e 1 2>&1 || true
  else
    printf 'FDP_DEVICE is not set.\n'
  fi
} >"${OUTPUT}"

printf 'Wrote environment report: %s\n' "${OUTPUT}"
