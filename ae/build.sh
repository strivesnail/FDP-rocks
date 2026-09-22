#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROCKSDB_DIR="${ROCKSDB_DIR:-${ROOT}/third_party/rocksdb}"
JOBS="${JOBS:-$(nproc)}"

test -f "${ROCKSDB_DIR}/src.mk" || {
  printf 'Missing FDP-Rocks source. Run ae/fetch_sources.sh first.\n' >&2
  exit 1
}

PYTHON_CFLAGS="$(python3-config --cflags)"
PYTHON_LDFLAGS="$(python3-config --ldflags --embed 2>/dev/null || python3-config --ldflags)"

make -C "${ROCKSDB_DIR}" -j"${JOBS}" clean
make -C "${ROCKSDB_DIR}" -j"${JOBS}" \
  USE_RTTI=1 \
  ROCKSDB_PLUGINS="" \
  ROCKSDB_ML_PREDICT_PYTHON=1 \
  EXTRA_CXXFLAGS="-DROCKSDB_ML_PREDICT_PYTHON ${PYTHON_CFLAGS}" \
  EXTRA_LDFLAGS="${PYTHON_LDFLAGS}" \
  static_lib

ROCKSDB_DIR="${ROCKSDB_DIR}" "${ROOT}/src/benchmark/compile.sh"
printf 'FDP-Rocks and the benchmark driver were built successfully.\n'
