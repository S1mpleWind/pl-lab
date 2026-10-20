#!/usr/bin/env bash
set -euo pipefail
bash setup.sh
bash build.sh
ctest --test-dir build --output-on-failure
export PYTHONPATH="/opt/compilerlab${PYTHONPATH:+:$PYTHONPATH}"
python3 -m grader validate .
# No official algorithm problems have been published. This suite only exercises
# the declared feature interface and must not be reported as course acceptance.
python3 - <<'PY'
from pathlib import Path
Path('/tmp/features-only.yaml').write_text('revision: features-only\nproblems: []\n')
PY
python3 -m grader grade --submission . --suite /tmp/features-only.yaml --output /tmp/lab-grader-report
python3 tests/parallel_benchmark.py --compiler build/labc
