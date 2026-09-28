#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/third_party/rocksdb"
REPO_URL="https://github.com/strivesnail/rocksdb.git"
REVISION="9043994d90c0c1e5321eb312bf239e3625f6d6d5"

mkdir -p "$(dirname "${DEST}")"

if test -d "${DEST}/.git"; then
  git -C "${DEST}" remote set-url origin "${REPO_URL}"
  git -C "${DEST}" fetch --tags origin
elif test -e "${DEST}"; then
  printf 'ERROR: %s exists but is not a git clone. Remove it and rerun.\n' "${DEST}" >&2
  exit 1
else
  git clone "${REPO_URL}" "${DEST}"
fi

git -C "${DEST}" checkout --detach "${REVISION}"
printf 'Checked out FDP-Rocks revision %s\n' "${REVISION}"
