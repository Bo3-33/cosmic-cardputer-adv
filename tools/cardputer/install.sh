#!/bin/sh
# Install only the desktop command; never connect to or flash a device here.
set -eu
source_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
install_dir=${CARDPUTER_INSTALL_HOME:-"$HOME/.local/lib/cosmic-cardputer"}
bin_dir=${CARDPUTER_BIN_HOME:-"$HOME/.local/bin"}
python3 -m venv "$install_dir/venv"
"$install_dir/venv/bin/python" -m pip install -r "$source_dir/requirements.txt"
mkdir -p "$bin_dir"
cp "$source_dir/cardputer.py" "$source_dir/connect-cardputer.py" "$source_dir/flash_preserve.py" "$install_dir/"
"$install_dir/venv/bin/python" - "$install_dir" "$bin_dir" <<'PY'
from pathlib import Path
import shlex
import sys
install, binary = map(lambda p: Path(p).expanduser().resolve(), sys.argv[1:])
for command, script in [('cardputer', 'cardputer.py'), ('cardputer-connect', 'connect-cardputer.py')]:
    wrapper = binary / command
    wrapper.write_text('#!/bin/sh\nexec ' + shlex.quote(str(install / 'venv/bin/python')) + ' ' + shlex.quote(str(install / script)) + ' "$@"\n')
    wrapper.chmod(0o755)
PY
printf '\nInstalled: %s/cardputer\n' "$bin_dir"
printf 'Ensure %s is on PATH. Then run: cardputer --all --fix-permissions\n' "$bin_dir"
