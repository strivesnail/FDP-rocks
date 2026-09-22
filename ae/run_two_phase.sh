#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCHMARK="${BENCHMARK:-${ROOT}/src/benchmark/rocksdb_bench}"
MODE="${1:-}"
OUTPUT_DIR="${OUTPUT_DIR:-${ROOT}/results/full}"

fail() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

case "${MODE}" in
  baseline)
    CUSTOM_COMPACTION=0
    HASH_HANDLE=0
    TRIVIAL_REWRITE=0
    FIXED_BUDGET=""
    ADAPTIVE_GEAR=0
    ;;
  model)
    CUSTOM_COMPACTION=1
    HASH_HANDLE=0
    TRIVIAL_REWRITE=1
    FIXED_BUDGET="${ROCKSDB_TOO_FAR_BUDGET:-100/100}"
    ADAPTIVE_GEAR=0
    test -n "${ROCKSDB_TOO_FAR_THRESHOLDS_SEC:-}" ||
      fail "ROCKSDB_TOO_FAR_THRESHOLDS_SEC is required for model mode."
    ;;
  gear)
    CUSTOM_COMPACTION=1
    HASH_HANDLE=0
    TRIVIAL_REWRITE=1
    FIXED_BUDGET=""
    ADAPTIVE_GEAR=1
    test -n "${ROCKSDB_TOO_FAR_THRESHOLDS_SEC:-}" ||
      fail "ROCKSDB_TOO_FAR_THRESHOLDS_SEC is required for gear mode."
    ;;
  nofdp)
    CUSTOM_COMPACTION=0
    HASH_HANDLE=2
    TRIVIAL_REWRITE=0
    FIXED_BUDGET=""
    ADAPTIVE_GEAR=0
    ;;
  *)
    fail "Usage: $0 baseline|model|gear|nofdp"
    ;;
esac

test -x "${BENCHMARK}" || fail "Benchmark is not built: ${BENCHMARK}"
test -f "${PHASE1_KEY_FILE:-}" || fail "PHASE1_KEY_FILE is missing."
test -f "${PHASE2_KEY_FILE:-}" || fail "PHASE2_KEY_FILE is missing."
test "${CONFIRM_ERASE_DB:-}" = "YES" ||
  fail "Set CONFIRM_ERASE_DB=YES after verifying FDP_DB_DIR."

"${ROOT}/ae/preflight.sh"
mkdir -p "${OUTPUT_DIR}"
rm -rf "${FDP_DB_DIR:?}/"*

run_phase() {
  local phase="$1"
  local count="$2"
  local distribution="$3"
  local key_file="$4"
  local log="${OUTPUT_DIR}/${MODE}_${phase}.log"
  local base_keys=0
  if test "${phase}" = "phase2"; then
    base_keys="${PHASE1_KEYS:-50000000}"
  fi

  env \
    ROCKSDB_ENABLE_PHASE2=1 \
    ROCKSDB_PHASE2_USE_BUFFER=0 \
    ROCKSDB_PHASE2_REWRITE=0 \
    ROCKSDB_COLLECT_FEATURES=0 \
    ROCKSDB_ML_PREDICT=0 \
    ROCKSDB_ENABLE_CUSTOM_COMPACTION="${CUSTOM_COMPACTION}" \
    ROCKSDB_HASH_HANDLE="${HASH_HANDLE}" \
    ROCKSDB_PHASE2_REGISTER_METADATA=1 \
    ROCKSDB_TRIVIAL_MOVE_REWRITE="${TRIVIAL_REWRITE}" \
    ROCKSDB_COMPACTION_PRI="${COMPACTION_PRIORITY:-BCS}" \
    ROCKSDB_TOO_FAR_THRESHOLDS_SEC="${ROCKSDB_TOO_FAR_THRESHOLDS_SEC:-}" \
    ROCKSDB_TOO_FAR_BUDGET="${FIXED_BUDGET}" \
    ROCKSDB_TOO_FAR_BUDGET_SCHEDULE="" \
    ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE="${ADAPTIVE_GEAR}" \
    ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_KN="${ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_KN:-0/10,5/10,10/10}" \
    ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_SLOT_SEC="${ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_SLOT_SEC:-10}" \
    ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_EXPLOIT_SEC="${ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_EXPLOIT_SEC:-60}" \
    ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_MIN_BYTES="${ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_MIN_BYTES:-0}" \
    ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_SHRINK="${ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_SHRINK:-1}" \
    ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_ADVANCE="${ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_ADVANCE:-wall}" \
    ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_SLOT_COMPACTIONS="${ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_SLOT_COMPACTIONS:-30}" \
    ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_METRIC="${ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_METRIC:-bps}" \
    ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_FDP_STATS_FILE="${ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_FDP_STATS_FILE:-}" \
    ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_FDP_NVME="${ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_FDP_NVME:-}" \
    ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_FDP_FROM_DB_PATH="${ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_FDP_FROM_DB_PATH:-0}" \
    ROCKSDB_BASE_KEYS="${base_keys}" \
    "${BENCHMARK}" \
    --worker_threads="${WORKER_THREADS:-1}" \
    --db1_path="${FDP_DB_DIR}" \
    --num="${count}" \
    --value_size="${VALUE_SIZE:-800}" \
    --batch=10000 \
    --key_distribution="${distribution}" \
    --key_sort=random \
    --key_order_dir="${KEY_DATASET_DIR}" \
    --key_file_path="${key_file}" 2>&1 | tee "${log}"

  grep -q "Total ${count} writes completed" "${log}" ||
    fail "${phase} did not complete ${count} writes."
  if grep -Eq "No space left|Write ERROR|Write FAILED" "${log}"; then
    fail "${phase} contains a write or capacity error."
  fi
}

run_phase phase1 "${PHASE1_KEYS:-50000000}" uniform "${PHASE1_KEY_FILE}"
run_phase phase2 "${PHASE2_KEYS:-100000000}" \
  "${PHASE2_DISTRIBUTION:-uniform}" "${PHASE2_KEY_FILE}"

if test -r "${FDP_DB_DIR}/LOG"; then
  cp "${FDP_DB_DIR}/LOG" "${OUTPUT_DIR}/rocksdb.LOG"
else
  fail "RocksDB LOG was not produced in ${FDP_DB_DIR}."
fi

printf 'Completed %s two-phase run. Logs: %s\n' "${MODE}" "${OUTPUT_DIR}"
