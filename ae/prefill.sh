#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIZE="${FDP_PREFILL_SIZE:-58G}"
OUTPUT="${FDP_PREFILL_FILE:-${FDP_MOUNT_POINT:-}/fdp_prefill.bin}"
POST_FREE_GIB="${FDP_POST_PREFILL_FREE_GIB:-50}"
SOURCE="${ROOT}/src/prefill/fill.cpp"
BINARY="${ROOT}/src/prefill/fill"

fail() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

test "${CONFIRM_PREFILL:-}" = "YES" ||
  fail "Set CONFIRM_PREFILL=YES after verifying the dedicated FDP device."
test -n "${FDP_MOUNT_POINT:-}" || fail "FDP_MOUNT_POINT is not set."
test -n "${OUTPUT}" || fail "FDP_PREFILL_FILE is empty."
OUTPUT="$(readlink -m "${OUTPUT}")"
MOUNT_POINT="$(readlink -f "${FDP_MOUNT_POINT}")"
case "${OUTPUT}" in
  "${MOUNT_POINT}"/*) ;;
  *) fail "FDP_PREFILL_FILE must be inside FDP_MOUNT_POINT." ;;
esac
test ! -e "${OUTPUT}" || fail "Prefill file already exists: ${OUTPUT}"

FDP_MIN_FREE_GIB=0 "${ROOT}/ae/preflight.sh"
PREFILL_BYTES="$(numfmt --from=iec "${SIZE}")"
FREE_BYTES="$(df --output=avail -B1 "${MOUNT_POINT}" | awk 'NR == 2 {print $1}')"
POST_FREE_BYTES="$(awk -v gib="${POST_FREE_GIB}" 'BEGIN {printf "%.0f", gib * 1024 * 1024 * 1024}')"
test "${FREE_BYTES}" -ge "$((PREFILL_BYTES + POST_FREE_BYTES))" ||
  fail "Prefill would leave less than ${POST_FREE_GIB} GiB free."

if test ! -x "${BINARY}" || test "${SOURCE}" -nt "${BINARY}"; then
  "${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra "${SOURCE}" -o "${BINARY}"
fi

"${BINARY}" "${SIZE}" "${OUTPUT}"
printf 'Prefill completed: %s\n' "${OUTPUT}"
