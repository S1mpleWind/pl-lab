#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
docker_command="${DOCKER:-docker}"
image="${LAB_TEST_IMAGE:-compilerlab-frontend:test}"

# The development build stays local. This script only performs clean image tests.
"$docker_command" info >/dev/null
if ! "$docker_command" image inspect "$image" >/dev/null 2>&1; then
    tar -C "$root/materials" -cf - . | "$docker_command" build -t "$image" -
fi

# Stream sources, never host binaries, to an independent container filesystem.
tar -C "$root" \
    --exclude='./.git' --exclude='./build' --exclude='./out' \
    --exclude='./reports' --exclude='./.deps' --exclude='__pycache__' \
    -cf - . |
    "$docker_command" run --rm -i --user 0 -e LAB_SUBMISSION_TESTS="${LAB_SUBMISSION_TESTS:-0}" --entrypoint bash "$image" -c '
        set -euo pipefail
        mkdir -p /workspace
        cd /workspace
        tar -xf -
        if [[ "$LAB_SUBMISSION_TESTS" == 1 ]]; then
            bash tests/submission-in-image.sh
        else
            bash setup.sh
            bash build.sh
            ctest --test-dir build --output-on-failure
        fi
    '
