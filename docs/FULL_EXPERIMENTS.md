# Full Experiment Instructions

Read `SAFETY.md` before continuing. These instructions assume a dedicated FDP
SSD and a verified build of FDP-Rocks.

## Prepare the Configuration

```bash
cp ae/config.example.env ae/config.env
$EDITOR ae/config.env
source ae/config.env
./ae/preflight.sh
./ae/collect_environment.sh results/environment.txt
```

Set `FDP_DEVICE`, `FDP_MOUNT_POINT`, `FDP_DB_DIR`, `KEY_DATASET_DIR`, and
`PHASE1_KEY_FILE` to real paths. The database directory must be inside the FDP
mount point. The preflight command does not modify data.

The statistics sampler uses the following read-only command:

```bash
sudo -n nvme fdp stats "$FDP_DEVICE" -e 1
```

Configure narrowly scoped sudo access for that exact command if unattended
sampling is required. Do not grant unrestricted passwordless sudo.

## Experiment Configuration

The Figure 6(a-c) measurements use:

- compaction priority: compensated size (BCS);
- value size: 800 bytes;
- Phase 1: 50 million uniformly distributed writes;
- Phase 2: 100 million writes;
- prefill: 58 GiB;
- one benchmark worker;
- FDP-Rocks lifetime threshold: p25 from the matching baseline log.

Figure 6(a) uses uniform and three Zipfian traces. Figure 6(b) uses the uniform
trace. Figure 6(c) uses the highly skewed trace.

## Workload Inputs

The five paper workload files and their SHA256 hashes are listed in
`data/workloads.sha256`. Place them in `KEY_DATASET_DIR`, then run:

```bash
./ae/verify_workloads.sh
```

Small example files for format and extension testing are included under
`data/examples/`. Regenerate them with:

```bash
python3 scripts/generate_example_workloads.py
```

The full paper files total approximately 3.4 GiB and are not included in the
compact Available + Functional bundle. Full hardware runs require checked
copies whose hashes match `data/workloads.sha256`.

## Prefill

The prefill program creates a new file using direct writes and no write-lifetime
hint. It refuses to overwrite an existing file.

```bash
CONFIRM_PREFILL=YES ./ae/prefill.sh
```

Delete the prefill file manually before repeating this step. Do not remove it
between the baseline, model, and NoFDP runs in one sweep.

## One-Command Figure 6 Sweep

After building the source and validating workloads:

```bash
CONFIRM_ERASE_DB=YES ./ae/run_figure6.sh
```

The command runs baseline, extracts matching p25 thresholds, runs FDP-Rocks and
NoFDP, samples FDP counters, converts logs to CSV, and creates Figure 6(a-c).
Results are written to `results/figure6/`.

To run a subset:

```bash
WORKLOADS=uniform,s3 CONFIRM_ERASE_DB=YES ./ae/run_figure6.sh
```

Accepted workload names are `uniform`, `s0.5`, `s1.5`, and `s3`.

## Individual Two-Phase Run

Set `PHASE2_KEY_FILE` and `PHASE2_DISTRIBUTION`, then choose one mode:

```bash
CONFIRM_ERASE_DB=YES OUTPUT_DIR=results/manual/baseline \
  ./ae/run_two_phase.sh baseline
CONFIRM_ERASE_DB=YES \
  OUTPUT_DIR=results/manual/model \
  ROCKSDB_TOO_FAR_THRESHOLDS_SEC=10.0,20.0,30.0,40.0,50.0 \
  ./ae/run_two_phase.sh model
CONFIRM_ERASE_DB=YES \
  OUTPUT_DIR=results/manual/gear \
  ROCKSDB_TOO_FAR_THRESHOLDS_SEC=10.0,20.0,30.0,40.0,50.0 \
  ./ae/run_two_phase.sh gear
CONFIRM_ERASE_DB=YES OUTPUT_DIR=results/manual/nofdp \
  ./ae/run_two_phase.sh nofdp
```

The model thresholds above are syntax examples only. Paper runs must extract
p25 values from the matching baseline `rocksdb.LOG`. Model mode uses the
fixed `100/100` budget from the reported p25 experiment. Gear mode replaces
that fixed budget with online probing; see `ADAPTIVE_GEAR.md`.

## Internal Sequence

1. Verify the device and mount point with `ae/preflight.sh`.
2. Prefill the dedicated device.
3. Run the RocksDB-FDP baseline.
4. Extract p25 lifetime thresholds from the baseline Phase 2 log.
5. Run FDP-Rocks with the extracted thresholds.
6. For Figure 6(a), also run RocksDB-NoFDP.
7. Convert logs to CSV and render the figures.
8. Check every run for a completed Phase 2 and for storage or write errors.

## Completion Criteria

A run is valid only when:

- the expected number of writes completed successfully;
- the process exited successfully;
- the log contains no `No space left on device`, `Write ERROR`, or
  `Write FAILED`;
- device-statistics sampling covers the complete run when DLWA is reported.

Do not treat wrapper-script completion as sufficient. The benchmark output must
also satisfy these checks.

## Resource Expectations

- Plot smoke test: under five minutes, under 1 GiB RAM, under 100 MiB output.
- Source build: approximately 10–30 minutes, up to 8 GiB RAM and 5 GiB disk.
- Example benchmark: under one minute and under 1 GiB temporary disk.
- One paper two-phase run: tens of minutes to several hours and up to 70 GiB
  transient database space after the 58 GiB prefill.
- Complete 12-run Figure 6 sweep: several hours to more than one day depending
  on SSD and compaction behavior.

For artifact evaluation without FDP hardware, use the smoke test and packaged
measurements described in the root README.

Expected non-fatal output includes RocksDB compaction statistics, progress-bar
control characters in captured logs, and `N/A` disk usage when the benchmark
path is unavailable to `statvfs`. Any write failure, capacity error, missing
completion marker, or missing FDP counter is fatal.
