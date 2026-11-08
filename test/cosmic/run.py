#!/usr/bin/env python3
"""Portable protocol/runtime tests; uses a system C/C++ compiler, no ESP32 needed."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
subprocess.run(["python3", str(root / "test/cosmic/test_fan.py")], check=True)
with tempfile.TemporaryDirectory(prefix="cosmic-tests-") as temp:
    build = pathlib.Path(temp)
    def run(args):
        subprocess.run([str(x) for x in args], check=True, cwd=root)
    run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Isrc", "test/cosmic/test_protocol.cpp", "-o", build / "protocol"])
    run([build / "protocol"])
    run(["g++", "-std=c++17", "-Isrc", "-Ilib/CosmicLG/src", "test/cosmic/test_remotes.cpp", "-o", build / "remotes"])
    run([build / "remotes"])
    run(["gcc", "-O2", "-c", "lib/CosmicGB/src/minigb_apu.c", "-o", build / "apu.o"])
    run(["g++", "-std=c++17", "-O2", "-Isrc", "-Ilib/CosmicGB/src", "test/cosmic/test_gameboy.cpp", build / "apu.o", "-lm", "-o", build / "gameboy"])
    run([build / "gameboy"])
    objects = []
    for source in sorted((root / "lib/Lua/src").glob("*.c")):
        if source.name in {"lua.c", "luac.c", "linit.c", "liolib.c", "loslib.c", "loadlib.c", "ldblib.c", "lcorolib.c"}:
            continue
        obj = build / (source.stem + ".o")
        run(["gcc", "-O2", "-DLUA_32BITS", "-c", source, "-o", obj])
        objects.append(obj)
    run(["g++", "-std=c++17", "-Wall", "-Wextra", "-DLUA_32BITS", "-DCOSMIC_LUA_STANDALONE", "-Isrc", "-Ilib/Lua/src", "src/cosmic_lua.cpp", "test/cosmic/test_lua.cpp", *objects, "-lm", "-o", build / "lua-tests"])
    run([build / "lua-tests", *sorted((root / "sdcard/apps").glob("*/main.lua"))])
