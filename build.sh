#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")" && pwd)"
cmake -S "$root" -B "$root/build" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$root/build" --parallel
