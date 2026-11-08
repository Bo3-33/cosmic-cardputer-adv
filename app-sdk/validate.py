#!/usr/bin/env python3
"""Validate a COSMIC app directory and optionally package it as a .capp.zip."""
import argparse
import json
import pathlib
import re
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument("directory", type=pathlib.Path)
parser.add_argument("--zip", type=pathlib.Path, dest="archive")
args = parser.parse_args()
manifest = args.directory / "app.json"
source = args.directory / "main.lua"
try:
    assert manifest.stat().st_size <= 2048, "app.json exceeds 2048 bytes"
    data = json.loads(manifest.read_text())
    assert isinstance(data, dict), "manifest must be an object"
    assert data.get("format") == "cosmic.app", "format must be cosmic.app"
    assert type(data.get("version")) is int and data["version"] == 1, "version must be integer 1"
    assert type(data.get("api")) is int and data["api"] == 1, "api must be integer 1"
    assert re.fullmatch(r"[a-z0-9-]{1,32}", data.get("id", "")), "invalid app id"
    assert args.directory.name == data["id"], "directory name must equal app id"
    assert isinstance(data.get("name"), str) and 1 <= len(data["name"].encode()) <= 24, "name must be 1..24 UTF-8 bytes"
    assert data.get("entry") == "main.lua", "entry must be main.lua"
    assert source.is_file() and 0 < source.stat().st_size <= 24576, "main.lua must be 1..24576 bytes"
    source.read_text(encoding="utf-8")
except (OSError, ValueError, AssertionError, TypeError) as exc:
    raise SystemExit(f"Invalid package: {exc}")
if args.archive:
    with zipfile.ZipFile(args.archive, "w", zipfile.ZIP_DEFLATED) as z:
        for name in ("app.json", "main.lua"):
            z.write(args.directory / name, f"{data['id']}/{name}")
print(f"Valid COSMIC app: {data['id']} ({data['name']})")
