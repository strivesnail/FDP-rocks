# Safety

## Destructive Operations

The full experiment path may:

- delete every file in the configured database directory;
- create a large prefill file;
- format or mount an NVMe namespace;
- issue administrative NVMe commands;
- consume nearly all available device capacity.

Use a dedicated test SSD. Verify the device path, mount point, and database
directory before every run. Do not use the operating-system disk.

## Privileges

Device setup and FDP statistics may require root privileges. Do not grant broad
passwordless sudo access. If automated sampling is required, allow only the
exact read-only `nvme fdp stats` command and remove the rule after the run.

## Required Guardrails

Full-run scripts must reject:

- an empty device or database path;
- `/`, `/home`, or another system directory as the database path;
- a mounted root filesystem as the target device;
- insufficient free space;
- an existing database unless destructive cleanup is explicitly confirmed.

The hardware-independent smoke test performs no privileged or destructive
operation.

`ae/preflight.sh` resolves block-device ancestry, rejects a device that shares
the root filesystem device, verifies that the configured mount is backed by the
selected device, requires the database path to be below that mount, checks a
configurable free-space threshold, and rejects a non-empty database directory
without explicit confirmation.

`ae/prefill.sh` requires a separate `CONFIRM_PREFILL=YES`, refuses to overwrite
an existing file, and checks that the requested prefill leaves the configured
free-space reserve.

`ae/sample_fdp_stats.sh` runs until its parent sweep stops it. If it is launched
manually, stop it with `Ctrl-C`.
