#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${ROOT}/results/smoke"

python3 -c "import matplotlib, numpy"
python3 "${ROOT}/scripts/validate_data.py"
mkdir -p "${OUT}"
python3 "${ROOT}/scripts/generate_example_workloads.py" \
  --output-dir "${OUT}/example-workloads"

python3 "${ROOT}/scripts/plot_precollected.py" \
  --kind throughput \
  --data "${ROOT}/data/precollected/figure6a.csv" \
  --output "${OUT}/figure6a"

for panel in figure6b figure6c; do
  python3 "${ROOT}/scripts/plot_precollected.py" \
    --kind wa \
    --data "${ROOT}/data/precollected/${panel}.csv" \
    --output "${OUT}/${panel}"
done

for panel in figure6a figure6b figure6c; do
  test -s "${OUT}/${panel}.pdf"
  test -s "${OUT}/${panel}.png"
done

cat >"${OUT}/gear-progress.log" <<'EOF'
[write-progress] timestamp=0 completed_keys=50000000 disk_usage=N/A
[write-progress] timestamp=9999999999 completed_keys=150000000 disk_usage=N/A
EOF
cat >"${OUT}/gear-rocksdb.LOG" <<'EOF'
2026/09/21-20:00:00.000000 [TooFarBudgetAdaptive] round=1 winning_global_slot=1 K/N=5/10 win_bytes_per_sec=123456.0 exploit_sec=60 probe_slot_sec=10 scores_bps=[1,2,3]
EOF
python3 "${ROOT}/scripts/extract_gear_timeline.py" \
  --progress-log "${OUT}/gear-progress.log" \
  --rocksdb-log "${OUT}/gear-rocksdb.LOG" \
  --output "${OUT}/gear-timeline.csv"
grep -q '^1,1,5,10,' "${OUT}/gear-timeline.csv"

"${ROOT}/ae/reproduce_precollected.sh"

printf 'Smoke test passed. Results: %s\n' "${OUT}"
