# Adaptive Gear

FDP-Rocks can select a compaction budget online instead of using one fixed
budget. A gear is a `K/N` budget: the picker may accept up to `K` of the first
`N` custom compaction candidates before falling back to the regular picker.

The evaluated three-gear policy probes `0/10`, `5/10`, and `10/10`. Each probe
slot is measured, the winning gear is used for an exploit interval, and the
process repeats. Probe shrinking can skip consistently unhelpful gears.

## Run

Build FDP-Rocks, configure the FDP device and paper workloads, then run:

```bash
source ae/config.env
CONFIRM_ERASE_DB=YES ./ae/run_gear.sh s0.5
```

Accepted workloads are `uniform`, `s0.5`, `s1.5`, and `s3`. If
`ROCKSDB_TOO_FAR_THRESHOLDS_SEC` is unset, the script first runs a matching
baseline and extracts p25 thresholds. Set it only when reusing thresholds from
a valid matching baseline.

The default policy is:

```bash
export ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_KN=0/10,5/10,10/10
export ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_SLOT_SEC=10
export ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_EXPLOIT_SEC=60
export ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_SHRINK=1
export ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_METRIC=bps
```

`bps` chooses the gear with the highest user-write throughput. The optional
`wa` metric minimizes application WA multiplied by device WA and requires a
readable FDP-counter source:

```bash
export ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_PROBE_METRIC=wa
export ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_FDP_NVME=/dev/nvme0
```

The NVMe character device must correspond to the configured namespace.
Alternatively, set `ROCKSDB_TOO_FAR_BUDGET_ADAPTIVE_FDP_STATS_FILE` to a file
that is continuously refreshed with `nvme fdp stats` output.

## Outputs

Results are stored under `results/gear/<workload>/`. The gear run includes:

- benchmark output for both phases;
- the RocksDB event log;
- sampled FDP counters;
- `gear_timeline.csv`, containing each completed probe round, winning `K/N`,
  selector score, and interpolated key-write position.

`ae/run_two_phase.sh gear` provides the lower-level entry point. Adaptive gear
and a fixed `ROCKSDB_TOO_FAR_BUDGET` are mutually exclusive.
