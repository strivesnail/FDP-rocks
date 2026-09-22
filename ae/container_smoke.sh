#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${ARTIFACT_IMAGE:-fdp-rocks-artifact:atc26}"

docker build -t "${IMAGE}" "${ROOT}"
docker run --rm "${IMAGE}"
