#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT="${ROOT}/results/reproduced-data"
TEMP="$(mktemp -d)"
trap 'rm -rf "${TEMP}"' EXIT

(cd "${ROOT}" && sha256sum --check data/raw/SHA256SUMS)
mkdir -p "${TEMP}/b" "${TEMP}/c" "${OUTPUT}"
tar -xzf "${ROOT}/data/raw/figure6b_logs.tar.gz" -C "${TEMP}/b"
tar -xzf "${ROOT}/data/raw/figure6c_logs.tar.gz" -C "${TEMP}/c"

python3 "${ROOT}/scripts/legacy_logs_to_wa_csv.py" \
  --baseline-tee "${TEMP}/b/fig6b_paper58g_baseline_uniform.log" \
  --baseline-stats "${TEMP}/b/fig6b_paper58g_baseline_uniform_fdp_stats.log" \
  --model-initial-tee "${TEMP}/b/fig6b_paper58g_modelbase_uniform_retry.log" \
  --model-resume-tees \
    "${TEMP}/b/fig6b_paper58g_modelbase_uniform_resume.log" \
    "${TEMP}/b/fig6b_paper58g_modelbase_uniform_resume2.log" \
    "${TEMP}/b/fig6b_paper58g_modelbase_uniform_resume3.log" \
  --model-stats \
    "${TEMP}/b/fig6b_paper58g_modelbase_uniform_retry_fdp_stats.log" \
    "${TEMP}/b/fig6b_paper58g_modelbase_uniform_resume_fdp_stats.log" \
    "${TEMP}/b/fig6b_paper58g_modelbase_uniform_resume2_fdp_stats.log" \
    "${TEMP}/b/fig6b_paper58g_modelbase_uniform_resume3_fdp_stats.log" \
  --output "${OUTPUT}/figure6b.csv"

python3 "${ROOT}/scripts/legacy_logs_to_wa_csv.py" \
  --baseline-tee "${TEMP}/c/fig6c_paper58g_baseline_s250.log" \
  --baseline-stats "${TEMP}/c/fig6c_paper58g_baseline_s250_fdp_stats.log" \
  --model-initial-tee "${TEMP}/c/fig6c_paper58g_modelbase_s250.log" \
  --model-stats "${TEMP}/c/fig6c_paper58g_modelbase_s250_fdp_stats.log" \
  --output "${OUTPUT}/figure6c.csv"

for panel in figure6b figure6c; do
  python3 "${ROOT}/scripts/compare_csv.py" \
    "${ROOT}/data/precollected/${panel}.csv" \
    "${OUTPUT}/${panel}.csv"
  python3 "${ROOT}/scripts/plot_precollected.py" \
    --kind wa \
    --data "${OUTPUT}/${panel}.csv" \
    --output "${OUTPUT}/${panel}"
done

printf 'Raw-log reproduction passed. Results: %s\n' "${OUTPUT}"
