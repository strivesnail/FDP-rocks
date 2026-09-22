# Source Revisions

## FDP-Rocks

- Repository: `https://github.com/strivesnail/rocksdb.git`
- Branch: `artifact-clean`
- Revision: `f3fc05908517e4b3346fb690d3a97d69877fec50`
- Evaluated code base: `33016b94e4cfb73b5a56126b47c203a25c70d46a`
- Upstream base: `47344a0fe`
- Comparison:
  `https://github.com/strivesnail/rocksdb/compare/33016b94e...f3fc05908`

This fork contains the FDP placement manager, custom compaction picker,
adaptive-gear controller, trivial-move rewrite support, event logging, and
build integration.
The artifact-clean revision preserves the evaluated executable behavior while
removing legacy tests, generated files, non-English design notes, and redundant
comments. It was rebuilt and passed the packaged benchmark smoke test.

## Benchmark and FDP Utilities

- Repository: `https://github.com/zhongch4g/kernel-exp.git`
- Base revision: `d3599621a39371d7f1957296d78f303166d4a374`

The artifact package includes the benchmark files needed by the evaluation.
Generated binaries, training experiments, editor state, temporary logs, and
unrelated analysis scripts are intentionally excluded.

The packaged benchmark differs from the development-tree copy only where
needed for artifact use:

- machine-specific default paths were removed;
- progress output was changed to a machine-readable English format;
- disk usage now uses `statvfs` instead of a hard-coded device shell command;
- development-only and non-English comments were removed;
- the build script now resolves source and library paths from the artifact.

The prefill utility is a small, artifact-specific rewrite with explicit output,
overwrite protection, aligned direct I/O, and failure propagation.

## Model Interface

The reported p25 experiments do not call an external ML service. Baseline
RocksDB logs are used to derive five lifetime thresholds for levels L1 through
L5. The thresholds are passed through
`ROCKSDB_TOO_FAR_THRESHOLDS_SEC`.

Legacy model-training and ONNX experiments are outside the evaluated artifact.

The adaptive-gear path is implemented in the same source tree and does not call
an external model. It probes configured `K/N` compaction budgets and chooses a
gear from measured throughput or write amplification. Its public artifact entry
point is `ae/run_gear.sh`; configuration is documented in
`docs/ADAPTIVE_GEAR.md`.
