# Benchmark Driver

`rocksdb_bench.cpp` is the single-worker write benchmark used by the artifact.
The packaged copy removes development-only comments, machine-specific paths,
and generated binaries.

Build FDP-Rocks first, then run:

```bash
ROCKSDB_DIR="$PWD/third_party/rocksdb" ./src/benchmark/compile.sh
```

Every run must provide explicit `--db1_path`, `--key_order_dir`, and
`--key_file_path` arguments. The driver prints machine-readable progress lines:

```text
[write-progress] timestamp=... completed_keys=... disk_usage=...
```
