#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKLOAD="${1:-}"
OUTPUT_ROOT="${OUTPUT_ROOT:-${ROOT}/results/gear}"

declare -A FILES=(
  [uniform]="keys_num_100000000_threads_1_dist_uniform_shuf_tid_0.bin"
  [s0.5]="keys_num_100000000_threads_1_dist_zipfian_s055_shuf_tid_0.bin"
  [s1.5]="keys_num_100000000_threads_1_dist_zipfian_s100_shuf_tid_0.bin"
  [s3]="keys_num_100000000_threads_1_dist_zipfian_s250_shuf_tid_0.bin"
)
declare -A DISTRIBUTIONS=(
  [uniform]="uniform"
  [s0.5]="zipfian"
  [s1.5]="zipfian"
  [s3]="zipfian"
)

fail() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

test -n "${WORKLOAD}" && test -n "${FILES[${WORKLOAD}]:-}" ||
  fail "Usage: $0 uniform|s0.5|s1.5|s3"
test -n "${KEY_DATASET_DIR:-}" || fail "KEY_DATASET_DIR is not set."

export PHASE2_KEY_FILE="${PHASE2_KEY_FILE:-${KEY_DATASET_DIR}/${FILES[${WORKLOAD}]}}"
export PHASE2_DISTRIBUTION="${PHASE2_DISTRIBUTION:-${DISTRIBUTIONS[${WORKLOAD}]}}"
test -f "${PHASE2_KEY_FILE}" || fail "Missing workload: ${PHASE2_KEY_FILE}"

BASELINE_DIR="${OUTPUT_ROOT}/${WORKLOAD}/baseline"
GEAR_DIR="${OUTPUT_ROOT}/${WORKLOAD}/gear"
mkdir -p "${BASELINE_DIR}" "${GEAR_DIR}"

if test -z "${ROCKSDB_TOO_FAR_THRESHOLDS_SEC:-}"; then
  OUTPUT_DIR="${BASELINE_DIR}" "${ROOT}/ae/run_two_phase.sh" baseline
  python3 "${ROOT}/scripts/extract_p25_thresholds.py" \
    --log "${BASELINE_DIR}/rocksdb.LOG" \
    --percentile 25 \
    --output "${OUTPUT_ROOT}/${WORKLOAD}/p25.txt"
  export ROCKSDB_TOO_FAR_THRESHOLDS_SEC
  ROCKSDB_TOO_FAR_THRESHOLDS_SEC="$(
    cat "${OUTPUT_ROOT}/${WORKLOAD}/p25.txt"
  )"
fi

SAMPLER_PID=""
cleanup() {
  if test -n "${SAMPLER_PID}"; then
    kill "${SAMPLER_PID}" 2>/dev/null || true
    wait "${SAMPLER_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

if test "${COLLECT_FDP_STATS:-1}" = "1"; then
  "${ROOT}/ae/sample_fdp_stats.sh" "${GEAR_DIR}/fdp_stats.log" &
  SAMPLER_PID="$!"
fi
set +e
OUTPUT_DIR="${GEAR_DIR}" "${ROOT}/ae/run_two_phase.sh" gear
status="$?"
set -e
cleanup
SAMPLER_PID=""
test "${status}" -eq 0 || fail "Gear run failed."

python3 "${ROOT}/scripts/extract_gear_timeline.py" \
  --progress-log "${GEAR_DIR}/gear_phase2.log" \
  --rocksdb-log "${GEAR_DIR}/rocksdb.LOG" \
  --output "${GEAR_DIR}/gear_timeline.csv"

printf 'Adaptive gear run completed: %s\n' "${GEAR_DIR}"
