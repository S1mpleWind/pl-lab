#!/usr/bin/env bash
set -euo pipefail
missing=0
for tool in g++ cmake flex bison m4 python3 clang; do
    if ! command -v "$tool" >/dev/null 2>&1; then missing=1; fi
done
if [[ "$missing" -eq 0 ]]; then exit 0; fi
if [[ "$(id -u)" -ne 0 ]]; then
    echo 'Install build-essential cmake flex bison m4 python3 clang with your local package manager.' >&2
    echo 'setup.sh installs dependencies as root inside the course test image.' >&2
    exit 2
fi
apt-get -o Acquire::Retries=3 update
DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=3 install -y --no-install-recommends \
    build-essential cmake flex bison m4 python3 clang
