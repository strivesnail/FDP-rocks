# FDP-Rocks Artifact

This repository contains the artifact for **“FDP-Rocks: An Effective Use Case
of FDP in RocksDB”** (ACM ATC 2026).

The artifact targets the **Available** and **Functional** badges. It provides:

- source revisions and build instructions for FDP-Rocks;
- a short, hardware-independent smoke test;
- pre-collected measurements and plotting scripts;
- archived raw logs and data-conversion scripts for Figure 6(b-c);
- deterministic example workloads and checksums for the paper workloads;
- an adaptive-gear experiment entry point and timeline extractor;
- full experiment instructions for an FDP-capable NVMe SSD.

## Safety Warning

The full experiments erase, format, prefill, and heavily write an NVMe device.
Never run a hardware experiment against a device that contains useful data.
The smoke test does not access block devices and is safe on a regular machine.
See [docs/SAFETY.md](docs/SAFETY.md) before using the full experiment path.

## Getting Started

The following path validates the artifact without FDP hardware. It installs only
the plotting dependencies, checks the packaged data, and regenerates the
available Figure 6 panels.

```bash
git clone https://github.com/strivesnail/FDP-rocks.git
cd FDP-rocks

python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r requirements.txt
./ae/smoke_test.sh
```

Expected runtime is under five minutes. Generated files are written to
`results/smoke/`.

The same test can run in a container:

```bash
./ae/container_smoke.sh
```

To validate the compiled source path:

```bash
./ae/fetch_sources.sh
./ae/build.sh
./ae/benchmark_smoke.sh
```

The build may take several minutes. The benchmark smoke test writes 10,000
small records to a temporary directory and does not require FDP hardware.

To regenerate Figure 6(b-c) data from the packaged raw logs:

```bash
./ae/reproduce_precollected.sh
```

## Artifact Components

- `ae/`: installation, validation, and smoke-test entry points.
- `data/precollected/`: compact, pre-collected measurements used by the plots.
- `data/raw/`: archived benchmark and FDP-statistics logs.
- `data/examples/`: deterministic, small workload inputs for extension tests.
- `scripts/`: result validation and plotting code.
- `expected/`: reference plots from the authors’ FDP test system.
- `docs/`: hardware, safety, source, and full-experiment documentation.

## Paper-to-Artifact Mapping

- Figure 6(a): throughput under four workload skewness settings.
- Figure 6(b): ALWA and DLWA under uniform insertions.
- Figure 6(c): ALWA and DLWA under highly skewed insertions.

The smoke test regenerates these panels from the packaged measurements. Full
hardware runs require the environment described in
[docs/HARDWARE.md](docs/HARDWARE.md).

## Source Code

FDP-Rocks is based on RocksDB. Exact revisions and the benchmark source layout
are documented in [docs/SOURCES.md](docs/SOURCES.md). Upstream licenses are
preserved. System package and build instructions are in
[docs/INSTALL.md](docs/INSTALL.md).

Maintained source code, scripts, and documentation are English-only. Archived
raw logs remain byte-for-byte measurement evidence and may contain historical
non-English console labels.

## Full Experiments

Full experiments are intentionally separate from the smoke test because they
require a dedicated FDP SSD, root privileges for device setup, approximately
125 GiB of device capacity, and several hours per sweep. See
[docs/FULL_EXPERIMENTS.md](docs/FULL_EXPERIMENTS.md).

The complete Figure 6 workflow has one entry point after configuration:

```bash
source ae/config.env
./ae/verify_workloads.sh
CONFIRM_PREFILL=YES ./ae/prefill.sh
CONFIRM_ERASE_DB=YES ./ae/run_figure6.sh
```

The online adaptive-gear policy has a separate entry point:

```bash
CONFIRM_ERASE_DB=YES ./ae/run_gear.sh s0.5
```

See [docs/ADAPTIVE_GEAR.md](docs/ADAPTIVE_GEAR.md) for the policy, selector
metrics, configuration variables, and outputs.

Reduced end-to-end validation results for all modes are recorded in
[docs/HARDWARE_SANITY.md](docs/HARDWARE_SANITY.md).

## Expected Results

Exact values vary with SSD firmware, media state, kernel version, and background
compaction timing. The smoke test checks data integrity and plot generation; it
does not claim to reproduce device measurements on non-FDP hardware.

The exact author environment is recorded in
[docs/author-environment.txt](docs/author-environment.txt).

## Archival Status

GitHub is the development home page. The archival release uses Zenodo DOI
[`10.5281/zenodo.22885948`](https://doi.org/10.5281/zenodo.22885948).

The compact Available + Functional bundle contains all tracked source,
documentation, example inputs, pre-collected measurements, and archived raw
logs required by the hardware-independent validation path. The five
paper-scale workload binaries are excluded from that bundle; their exact
filenames and SHA256 checksums remain in `data/workloads.sha256`.

The paper-scale binaries are needed only for optional full FDP hardware runs.
After placing checked copies in `KEY_DATASET_DIR`, create an optional full
bundle with:

```bash
./ae/package_archive.sh
```

## License

Artifact scripts and documentation are released under Apache License 2.0.
RocksDB and other third-party components retain their original licenses. See
[NOTICE.md](NOTICE.md).
