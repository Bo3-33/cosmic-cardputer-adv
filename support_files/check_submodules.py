import subprocess
from pathlib import Path
import sys

try:
    Import("env")  # type: ignore
except NameError:
    env = None

if env is not None and env.get("PROJECT_DIR"):
    ROOT = Path(env.get("PROJECT_DIR"))
else:
    ROOT = Path(__file__).resolve().parent.parent

SUBMODULE_CMD = ["git", "submodule", "update", "--init", "--recursive"]


def run_command(cmd, cwd):
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Command failed: {' '.join(cmd)}")
        print(result.stdout)
        print(result.stderr)
        sys.exit(result.returncode)
    return result.stdout.strip()


print("Checking submodules...")
if not (ROOT / ".git").exists():
    # Release source archives include dependencies but deliberately omit Git metadata.
    required = [
        ROOT / "lib/Arduino_GFX/src/Arduino_GFX.h",
        ROOT / "lib/ArduinoJson/src/ArduinoJson.h",
        ROOT / "lib/DisplayDrivers/src/DisplayDrivers.h",
    ]
    if not all(path.is_file() for path in required):
        print("Source archive is missing vendored dependencies. Extract the complete cosmic-source.zip.")
        sys.exit(1)
    print("Using vendored dependencies from source archive.")
elif not (ROOT / "lib" / "Arduino_GFX").exists():
    print("Submodules not checked out. Initializing...")
    run_command(SUBMODULE_CMD, ROOT)
else:
    print("Initializing missing submodules and updating existing ones...")
    run_command(SUBMODULE_CMD, ROOT)
print("Submodules are ready.")
