#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT="${1:-${ROOT}/FDP-rocks-atc26-artifact.tar.gz}"
TEMP="$(mktemp -d)"
trap 'rm -rf "${TEMP}"' EXIT

test -n "${KEY_DATASET_DIR:-}" || {
  printf 'Set KEY_DATASET_DIR before packaging the archival artifact.\n' >&2
  exit 1
}
"${ROOT}/ae/verify_workloads.sh"
test -z "$(git -C "${ROOT}" status --short)" || {
  printf 'Commit all artifact changes before creating an archive.\n' >&2
  exit 1
}

mkdir -p "${TEMP}/FDP-rocks/data/workloads"
git -C "${ROOT}" archive HEAD | tar -x -C "${TEMP}/FDP-rocks"
while read -r checksum filename; do
  test -n "${checksum}" || continue
  cp "${KEY_DATASET_DIR}/${filename}" "${TEMP}/FDP-rocks/data/workloads/"
done <"${ROOT}/data/workloads.sha256"

tar -czf "${OUTPUT}" -C "${TEMP}" FDP-rocks
sha256sum "${OUTPUT}" >"${OUTPUT}.sha256"
printf 'Created archival package: %s\n' "${OUTPUT}"
