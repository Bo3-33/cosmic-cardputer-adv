#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Create a passphrase-encrypted Cardputer vault using the reference Proton CLI crypto.

Requires git and Bun >=1.1. No background service; never invokes sign-in until run.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

REVISION = "4e796c4f1e8ba19e3098e591fff5833e4d9c3ef5"
URL = "https://github.com/brandonkramer/proton-authenticator-cli.git"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sd", type=Path, help="Mounted SD card directory")
    parser.add_argument("--output", type=Path, help="Output vault file, instead of --sd")
    parser.add_argument("--cli-dir", type=Path, help="Use an existing checkout at the pinned revision")
    args = parser.parse_args()
    if bool(args.sd) == bool(args.output):
        parser.error("choose either --sd MOUNT_PATH or --output VAULT_FILE")
    bun = shutil.which("bun")
    if not bun:
        sys.exit("Install Bun from https://bun.com/docs/installation, then run this command again.")
    if not shutil.which("git"):
        sys.exit("Install git, then run this command again.")
    destination = args.output or args.sd / "authenticator/vault.cav"
    if args.sd and (not args.sd.is_dir() or not os.path.ismount(args.sd)):
        sys.exit("The --sd path must be the mounted SD card. Mount it first, or use --output.")
    if destination.exists():
        sys.exit("The destination already contains a vault. Use --output with a new filename to re-pair.")
    cache = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "cosmic-authenticator"
    checkout = args.cli_dir or cache / "proton-cli"
    if not checkout.exists():
        checkout.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "clone", URL, str(checkout)], check=True)
        subprocess.run(["git", "checkout", "--detach", REVISION], cwd=checkout, check=True)
    actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=checkout, text=True).strip()
    if actual != REVISION:
        sys.exit("The reference checkout has a different revision. Use the pinned revision or a fresh cache.")
    subprocess.run([bun, "install", "--frozen-lockfile"], cwd=checkout, check=True)
    helper = checkout / "cosmic-pair.ts"
    shutil.copyfile(Path(__file__).with_suffix(".ts"), helper)
    # Input and output remain attached to the user's terminal; passwords are masked.
    subprocess.run([bun, "run", str(helper.resolve()), str(destination.resolve())], cwd=checkout, check=True)

if __name__ == "__main__":
    try:
        main()
    except (subprocess.CalledProcessError, OSError) as e:
        sys.exit(f"Pairing could not finish ({type(e).__name__}). Your existing vault was not replaced.")
