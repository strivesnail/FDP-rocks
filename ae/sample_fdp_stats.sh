#!/usr/bin/env bash
set -euo pipefail

OUTPUT="${1:-}"
INTERVAL="${FDP_STATS_INTERVAL_SEC:-10}"

test -n "${OUTPUT}" || {
  printf 'Usage: %s OUTPUT_LOG\n' "$0" >&2
  exit 1
}
test -n "${FDP_DEVICE:-}" || {
  printf 'FDP_DEVICE is not set.\n' >&2
  exit 1
}

while true; do
  printf 'SAMPLE timestamp=%s\n' "$(date +%s.%N)"
  sudo -n nvme fdp stats "${FDP_DEVICE}" -e 1
  sleep "${INTERVAL}"
done >>"${OUTPUT}" 2>&1
