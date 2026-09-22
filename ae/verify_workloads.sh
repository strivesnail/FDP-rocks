#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATASET_DIR="${KEY_DATASET_DIR:-}"

test -n "${DATASET_DIR}" || {
  printf 'Set KEY_DATASET_DIR to the directory containing paper workloads.\n' >&2
  exit 1
}
test -d "${DATASET_DIR}" || {
  printf 'Dataset directory does not exist: %s\n' "${DATASET_DIR}" >&2
  exit 1
}

(
  cd "${DATASET_DIR}"
  sha256sum --check "${ROOT}/data/workloads.sha256"
)
