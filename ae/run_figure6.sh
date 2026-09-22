#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_ROOT="${OUTPUT_ROOT:-${ROOT}/results/figure6}"
SELECTED="${WORKLOADS:-uniform,s0.5,s1.5,s3}"
COLLECT_STATS="${COLLECT_FDP_STATS:-1}"

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

SAMPLER_PID=""
cleanup() {
  if test -n "${SAMPLER_PID}"; then
    kill "${SAMPLER_PID}" 2>/dev/null || true
    wait "${SAMPLER_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

test -n "${KEY_DATASET_DIR:-}" || fail "KEY_DATASET_DIR is not set."
test -n "${PHASE1_KEY_FILE:-}" || fail "PHASE1_KEY_FILE is not set."
mkdir -p "${OUTPUT_ROOT}"

run_mode() {
  local workload="$1"
  local mode="$2"
  local output="${OUTPUT_ROOT}/${workload}/${mode}"
  local sampler=""
  mkdir -p "${output}"

  if test "${COLLECT_STATS}" = "1"; then
    "${ROOT}/ae/sample_fdp_stats.sh" "${output}/fdp_stats.log" &
    sampler="$!"
    SAMPLER_PID="${sampler}"
  fi

  set +e
  OUTPUT_DIR="${output}" "${ROOT}/ae/run_two_phase.sh" "${mode}"
  local status="$?"
  set -e
  if test -n "${sampler}"; then
    kill "${sampler}" 2>/dev/null || true
    wait "${sampler}" 2>/dev/null || true
    SAMPLER_PID=""
    {
      printf 'SAMPLE timestamp=%s\n' "$(date +%s.%N)"
      sudo -n nvme fdp stats "${FDP_DEVICE}" -e 1
    } >>"${output}/fdp_stats.log" 2>&1
    grep -q "Media Bytes with Metadata Written" "${output}/fdp_stats.log" ||
      fail "FDP statistics were not collected for ${mode}/${workload}."
  fi
  test "${status}" -eq 0 || fail "${mode}/${workload} failed."
}

IFS=',' read -r -a workloads <<<"${SELECTED}"
for workload in "${workloads[@]}"; do
  test -n "${FILES[${workload}]:-}" || fail "Unknown workload: ${workload}"
  export PHASE2_KEY_FILE="${KEY_DATASET_DIR}/${FILES[${workload}]}"
  export PHASE2_DISTRIBUTION="${DISTRIBUTIONS[${workload}]}"
  test -f "${PHASE2_KEY_FILE}" || fail "Missing workload: ${PHASE2_KEY_FILE}"

  run_mode "${workload}" baseline
  threshold_file="${OUTPUT_ROOT}/${workload}/p25.txt"
  python3 "${ROOT}/scripts/extract_p25_thresholds.py" \
    --log "${OUTPUT_ROOT}/${workload}/baseline/rocksdb.LOG" \
    --percentile 25 \
    --output "${threshold_file}"
  export ROCKSDB_TOO_FAR_THRESHOLDS_SEC
  ROCKSDB_TOO_FAR_THRESHOLDS_SEC="$(cat "${threshold_file}")"

  run_mode "${workload}" model
  run_mode "${workload}" nofdp
done

python3 "${ROOT}/scripts/results_to_csv.py" \
  --results-root "${OUTPUT_ROOT}" \
  --workloads "${SELECTED}" \
  --output "${OUTPUT_ROOT}/figure6a.csv"
python3 "${ROOT}/scripts/plot_precollected.py" \
  --kind throughput \
  --data "${OUTPUT_ROOT}/figure6a.csv" \
  --output "${OUTPUT_ROOT}/figure6a"

if test "${COLLECT_STATS}" = "1"; then
  case ",${SELECTED}," in
    *,uniform,*)
      python3 "${ROOT}/scripts/results_to_wa_csv.py" \
        --workload-dir "${OUTPUT_ROOT}/uniform" \
        --phase1-keys "${PHASE1_KEYS:-50000000}" \
        --phase2-keys "${PHASE2_KEYS:-100000000}" \
        --output "${OUTPUT_ROOT}/figure6b.csv"
      python3 "${ROOT}/scripts/plot_precollected.py" \
        --kind wa \
        --data "${OUTPUT_ROOT}/figure6b.csv" \
        --output "${OUTPUT_ROOT}/figure6b"
      ;;
  esac
  case ",${SELECTED}," in
    *,s3,*)
      python3 "${ROOT}/scripts/results_to_wa_csv.py" \
        --workload-dir "${OUTPUT_ROOT}/s3" \
        --phase1-keys "${PHASE1_KEYS:-50000000}" \
        --phase2-keys "${PHASE2_KEYS:-100000000}" \
        --output "${OUTPUT_ROOT}/figure6c.csv"
      python3 "${ROOT}/scripts/plot_precollected.py" \
        --kind wa \
        --data "${OUTPUT_ROOT}/figure6c.csv" \
        --output "${OUTPUT_ROOT}/figure6c"
      ;;
  esac
fi

printf 'Figure 6 sweep completed: %s\n' "${OUTPUT_ROOT}"
