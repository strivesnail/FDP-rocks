#!/usr/bin/env bash
set -euo pipefail

DEVICE="${FDP_DEVICE:-}"
MOUNT_POINT="${FDP_MOUNT_POINT:-}"
DB_DIR="${FDP_DB_DIR:-}"
MIN_FREE_GIB="${FDP_MIN_FREE_GIB:-50}"

fail() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

first_line() {
  IFS= read -r line || true
  printf '%s\n' "${line:-}"
}

base_device() {
  local current
  local parent
  current="$(readlink -f "$1")"
  while true; do
    parent="$(lsblk -ndo PKNAME "${current}" 2>/dev/null | first_line || true)"
    test -n "${parent}" || break
    current="/dev/${parent}"
  done
  printf '%s\n' "${current}"
}

test -n "${DEVICE}" || fail "FDP_DEVICE is not set."
test -n "${MOUNT_POINT}" || fail "FDP_MOUNT_POINT is not set."
test -n "${DB_DIR}" || fail "FDP_DB_DIR is not set."
DEVICE="$(readlink -f "${DEVICE}")"
test -b "${DEVICE}" || fail "Not a block device: ${DEVICE}"
test -d "${MOUNT_POINT}" || fail "Mount point does not exist: ${MOUNT_POINT}"
test -d "${DB_DIR}" || fail "Database directory does not exist: ${DB_DIR}"
MOUNT_POINT="$(readlink -f "${MOUNT_POINT}")"
DB_DIR="$(readlink -f "${DB_DIR}")"

case "${DB_DIR}" in
  /|/home|"${MOUNT_POINT}") fail "Unsafe database directory: ${DB_DIR}" ;;
  "${MOUNT_POINT}"/*) ;;
  *) fail "Database directory must be inside the FDP mount point." ;;
esac

ROOT_DEVICE="$(findmnt -n -o SOURCE --target /)"
MOUNT_DEVICE="$(findmnt -n -o SOURCE --target "${MOUNT_POINT}")"
test -n "${MOUNT_DEVICE}" || fail "Mount point is not mounted."
ROOT_BASE="$(base_device "${ROOT_DEVICE}")"
TARGET_BASE="$(base_device "${DEVICE}")"
MOUNT_BASE="$(base_device "${MOUNT_DEVICE}")"
test "${TARGET_BASE}" != "${ROOT_BASE}" ||
  fail "The selected device shares the root filesystem device: ${ROOT_BASE}"
test "${TARGET_BASE}" = "${MOUNT_BASE}" ||
  fail "FDP_DEVICE does not back FDP_MOUNT_POINT: ${DEVICE} vs ${MOUNT_DEVICE}"
command -v nvme >/dev/null || fail "nvme-cli is not installed."
[[ "${MIN_FREE_GIB}" =~ ^[0-9]+([.][0-9]+)?$ ]] ||
  fail "FDP_MIN_FREE_GIB must be numeric."

AVAILABLE_BYTES="$(df --output=avail -B1 "${MOUNT_POINT}" | awk 'NR == 2 {print $1}')"
REQUIRED_BYTES="$(awk -v gib="${MIN_FREE_GIB}" 'BEGIN {printf "%.0f", gib * 1024 * 1024 * 1024}')"
test "${AVAILABLE_BYTES}" -ge "${REQUIRED_BYTES}" ||
  fail "Insufficient free space: require ${MIN_FREE_GIB} GiB on ${MOUNT_POINT}."

if find "${DB_DIR}" -mindepth 1 -maxdepth 1 -print -quit | grep -q .; then
  test "${CONFIRM_ERASE_DB:-}" = "YES" ||
    fail "Database directory is not empty; set CONFIRM_ERASE_DB=YES to permit cleanup."
fi

printf 'Device: %s\n' "${DEVICE}"
printf 'Mount point: %s\n' "${MOUNT_POINT}"
printf 'Database directory: %s\n' "${DB_DIR}"
printf 'Root device: %s\n' "${ROOT_BASE}"
printf 'Required free space: %s GiB\n' "${MIN_FREE_GIB}"
df -h "${MOUNT_POINT}"
printf 'Preflight checks passed. No data were modified.\n'
