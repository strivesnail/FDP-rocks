# Hardware and Software Environment

## Author Environment

- Ubuntu 22.04.5 LTS
- Linux 6.10 FDP kernel, revision `5b8dbac4391db18ae72a4ba8dec207c202c27aa9`
- Intel Xeon E5-2695 v4 CPU
- 62 GiB RAM and 4 GiB swap
- device-reported model: `FEMU Cylon NVMe Flexible Data Placement`
- device-reported firmware: `1.0`
- 125 GiB ext4 filesystem mounted with `rw,relatime,discard`
- nvme-cli 2.5 and libnvme 1.5
- GCC 11.4.0 and CMake 3.22.1
- Python 3.10.12
- one RocksDB benchmark worker
- 800-byte values
- 50 million Phase 1 writes and 100 million Phase 2 writes
- 58 GiB device prefill for the reported Figure 6 measurements

The machine-generated report is preserved in `docs/author-environment.txt`.
It intentionally omits device serial numbers.

## Software Dependencies

Build dependencies include:

- GCC or Clang with C++20 support
- CMake and Make
- Python 3.10 or newer
- gflags
- TBB
- liburing
- zlib, bzip2, Snappy, Zstandard, and LZ4 development packages
- nvme-cli for FDP statistics

Plotting requires the packages pinned in `requirements.txt`.

## Device Access

The complete evaluation requires FDP support in the kernel, controller, and
namespace. A regular SSD can run the build and NoFDP smoke workload but cannot
produce meaningful FDP placement or DLWA results.

Pre-collected data are included so evaluators without FDP hardware can validate
the analysis and plotting path.
