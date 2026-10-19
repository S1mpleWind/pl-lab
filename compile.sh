#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")" && pwd)"
if [[ $# -lt 2 || -z "${!#}" ]]; then
    echo 'usage: bash compile.sh SOURCE [SOURCE ...] OUTPUT' >&2
    exit 2
fi
artifact="${!#}"
sources=("${@:1:$#-1}")
# Validate every alias before removing an old artifact.
python3 - "$artifact" "${sources[@]}" <<'PY'
import os
import sys
output, *sources = sys.argv[1:]
for source in sources:
    if not os.path.isfile(source):
        print('source file not found: ' + source, file=sys.stderr)
        sys.exit(2)
    if os.path.realpath(output) == os.path.realpath(source) or (os.path.exists(output) and os.path.samefile(output, source)):
        print('output must not overwrite a source file', file=sys.stderr)
        sys.exit(2)
PY
rm -f -- "$artifact"
exec "$root/build/labc" --compile -o "$artifact" -- "${sources[@]}"
