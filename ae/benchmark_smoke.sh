#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCHMARK="${ROOT}/src/benchmark/rocksdb_bench"

test -x "${BENCHMARK}" || {
  printf 'Build the benchmark with src/benchmark/compile.sh first.\n' >&2
  exit 1
}

TEMP_ROOT="$(mktemp -d)"
trap 'rm -rf "${TEMP_ROOT}"' EXIT
mkdir -p "${TEMP_ROOT}/db" "${TEMP_ROOT}/keys"

env \
  ROCKSDB_ENABLE_PHASE2=1 \
  ROCKSDB_HASH_HANDLE=2 \
  ROCKSDB_ENABLE_CUSTOM_COMPACTION=0 \
  ROCKSDB_TRIVIAL_MOVE_REWRITE=0 \
  ROCKSDB_PHASE2_REGISTER_METADATA=1 \
  ROCKSDB_COMPACTION_PRI=BCS \
  "${BENCHMARK}" \
  --worker_threads=1 \
  --db1_path="${TEMP_ROOT}/db" \
  --num=10000 \
  --value_size=128 \
  --batch=1000 \
  --key_distribution=uniform \
  --key_sort=random \
  --key_order_dir="${TEMP_ROOT}/keys"

printf 'Benchmark smoke test passed.\n'
