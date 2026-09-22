#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROCKSDB_DIR="${ROCKSDB_DIR:-${SCRIPT_DIR}/../../third_party/rocksdb}"
SOURCE="${SCRIPT_DIR}/rocksdb_bench.cpp"
OUTPUT="${SCRIPT_DIR}/rocksdb_bench"
CXX="${CXX:-g++}"

test -f "${SOURCE}" || { printf 'Missing source: %s\n' "${SOURCE}" >&2; exit 1; }
test -d "${ROCKSDB_DIR}/include" || {
  printf 'Invalid ROCKSDB_DIR: %s\n' "${ROCKSDB_DIR}" >&2
  exit 1
}

if test -f "${ROCKSDB_DIR}/build/librocksdb.a"; then
  ROCKSDB_LIBRARY="${ROCKSDB_DIR}/build/librocksdb.a"
elif test -f "${ROCKSDB_DIR}/librocksdb.a"; then
  ROCKSDB_LIBRARY="${ROCKSDB_DIR}/librocksdb.a"
else
  printf 'Build librocksdb.a before compiling the benchmark.\n' >&2
  exit 1
fi

GFLAGS_CFLAGS="$(pkg-config --cflags gflags 2>/dev/null || true)"
GFLAGS_LIBS="$(pkg-config --libs gflags 2>/dev/null || printf '%s' '-lgflags')"
TBB_CFLAGS="$(pkg-config --cflags tbb 2>/dev/null || true)"
TBB_LIBS="$(pkg-config --libs tbb 2>/dev/null || printf '%s' '-ltbb')"
URING_LIBS="$(pkg-config --libs liburing 2>/dev/null || printf '%s' '-luring')"
PYTHON_LIBS="$(python3-config --ldflags --embed 2>/dev/null || python3-config --ldflags)"

"${CXX}" -std=gnu++20 -O2 -g -DNDEBUG -DROCKSDB_USE_RTTI \
  -I"${SCRIPT_DIR}" -I"${ROCKSDB_DIR}/include" -I"${ROCKSDB_DIR}" \
  ${GFLAGS_CFLAGS} ${TBB_CFLAGS} \
  "${SOURCE}" -o "${OUTPUT}" \
  "${ROCKSDB_LIBRARY}" ${GFLAGS_LIBS} ${TBB_LIBS} ${URING_LIBS} ${PYTHON_LIBS} \
  -lpthread -ldl -lz -lbz2 -lsnappy -lzstd -llz4 -lrt \
  -Wl,--allow-multiple-definition

printf 'Built %s\n' "${OUTPUT}"
