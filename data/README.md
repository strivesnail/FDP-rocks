# Pre-collected Data

The CSV files in `precollected/` are compact representations of measurements
from the authors’ FDP test system.

## Figure 6(a)

`figure6a.csv` contains the final Phase 2 throughput for four workload
skewness settings and three system configurations. Values are reported in
thousands of operations per second. The values are the published chart
constants preserved by the authors' original rendering script:

- RocksDB-NoFDP: 31.5, 33.3, 48.8, 66.6 Kops/s;
- RocksDB-FDP: 34.1, 35.5, 51.9, 70.8 Kops/s;
- FDP-Rocks: 44.8, 46.1, 63.7, 78.9 Kops/s.

The original Figure 6(a) tee logs were not retained. New hardware runs use
`scripts/results_to_csv.py` to derive the same schema directly from logs. This
limitation is disclosed so processed chart values are not mistaken for raw
measurements.

## Figures 6(b) and 6(c)

The write-amplification CSV files contain samples at five-million-write
intervals. ALWA is derived from RocksDB flush, compaction, and user-ingest
counters. DLWA is derived from NVMe FDP host-bytes-written and
media-bytes-written counters.

Figure 6(b) FDP-Rocks encountered temporary filesystem-capacity stalls under
the 58 GiB prefill. The same database was reopened and the remaining key-order
suffixes were applied. The final suffix used paced writes to allow compaction
to release transient space. Device counters remained cumulative, while
application counters were stitched across process boundaries. This run is
provided as measured diagnostic data and is not represented as an
uninterrupted execution.

Figure 6(c) completed as an uninterrupted run for both compared systems.

The corresponding raw logs are in `raw/figure6b_logs.tar.gz` and
`raw/figure6c_logs.tar.gz`; `raw/SHA256SUMS` records their checksums.
`ae/reproduce_precollected.sh` extracts those archives, recreates the CSV
files, checks every numeric value, and regenerates the plots.

The archived logs preserve the original console output verbatim, including
non-English labels emitted by the historical runner. They are immutable
measurement evidence, not maintained source code. All current source,
automation, parsers, and documentation are English-only.

The plotting smoke test validates file structure and regenerates plots. It
does not alter or normalize the measured values.

## Workload Inputs

`workloads.sha256` records the exact five binary inputs used by the Figure 6
runs. Each file is a stream of little-endian unsigned 64-bit keys. Full files
total approximately 3.4 GiB and are not included in the compact Available +
Functional bundle. They are needed only for optional paper-scale FDP hardware
runs.

`examples/` contains deterministic small inputs for format validation and
extension tests. They are examples rather than substitutes for the checked
paper workloads.
