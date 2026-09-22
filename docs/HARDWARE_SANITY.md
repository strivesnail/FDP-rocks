# Hardware Sanity Validation

The artifact entry points were validated on the author FDP system on
September 22, 2026. These reduced runs test execution and safety rather than
reproduce paper-scale measurements.

## Environment

- FDP namespace: `/dev/nvme0n1`
- Device model: FEMU Cylon NVMe Flexible Data Placement
- Filesystem: ext4 mounted with `discard`
- Existing 58 GiB prefill retained
- Compaction priority: BCS
- Value size: 800 bytes
- Worker threads: 1

## Fixed-Mode Validation

Baseline, FDP-Rocks model, and NoFDP modes each completed:

- Phase 1: 200,000 uniform writes
- Phase 2: 500,000 uniform writes

All phases produced flushes and L0/L1 compactions. No write, capacity, or
completion errors were reported. The model log confirmed that the wrapper
passed the required fixed budget:

```text
too_far budget (global K/N, all levels): 100/100 source=env raw=100/100
```

Synthetic lifetime thresholds were used because this scale does not produce
enough L1-L5 lifetime samples for a meaningful p25 estimate.

## Adaptive-Gear Validation

The gear path completed:

- Phase 1: 500,000 uniform writes
- Phase 2: 8,000,000 uniform writes
- Probe gears: `0/10`, `5/10`, `10/10`
- Probe slot: 1 second
- Exploit interval: 2 seconds

The run reached L1-L4 compactions, completed eight probe/exploit rounds, chose
all three configured gears, sampled FDP counters throughout the run, and
generated `gear_timeline.csv`. No write or capacity errors were reported.

The short probe and exploit intervals are test-only settings. Paper-scale runs
use the values documented in `ADAPTIVE_GEAR.md`.
