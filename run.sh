#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 1 || ! -f "$1" ]]; then
    echo 'usage: bash run.sh ARTIFACT' >&2
    exit 2
fi
artifact="$1"
if [[ "$artifact" != /* ]]; then artifact="./$artifact"; fi
exec "$artifact"
