#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
cargo test
cargo run --release -- --self-test
echo "RBL Studio native headless checks: PASS"
