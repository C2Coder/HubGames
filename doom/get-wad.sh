#!/bin/bash
# Downloads the free shareware doom1.wad (v1.9) and checks it.
set -euo pipefail
cd "$(dirname "$0")"
curl -fsSL -o doom1.wad https://raw.githubusercontent.com/Akbar30Bill/DOOM_wads/master/doom1.wad
python3 - <<'PYTHON'
import hashlib
from pathlib import Path
if hashlib.sha1(Path('doom1.wad').read_bytes()).hexdigest() != '5b2e249b9c5133ec987b3ea77596381dc0d6bc1d':
    raise SystemExit('WAD checksum mismatch; do not flash this file')
print('doom1.wad: checksum OK')
PYTHON
