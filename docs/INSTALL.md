# Installation

The reference environment uses Ubuntu-compatible package names:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake git pkg-config python3 python3-dev python3-venv \
  libgflags-dev libtbb-dev liburing-dev zlib1g-dev libbz2-dev \
  libsnappy-dev libzstd-dev liblz4-dev nvme-cli
```

Install plotting dependencies:

```bash
./ae/install_plot_deps.sh
source .venv/bin/activate
```

Fetch and build the FDP-Rocks source:

```bash
./ae/fetch_sources.sh
./ae/build.sh
```

The kernel and FDP device setup are required only for full hardware
experiments. They are not required for plotting or the benchmark smoke test.

## Containerized Smoke Test

Docker provides the OS-level dependencies for the hardware-independent path:

```bash
./ae/container_smoke.sh
```

The container intentionally does not expose block devices or privileged NVMe
commands. Full FDP experiments must run on a Linux host with an FDP namespace.
