#!/usr/bin/env python3
"""Run the production system shim against deterministic host I/O adapters."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SHIM = ROOT / "android" / "shim"
sources = [SHIM / "src" / (name + ".c") for name in
           ("apt", "exit", "fs", "hid", "mem", "ndsp", "svc", "sync", "thread")]
wraps = [line.strip() for line in (SHIM / "wrap.txt").read_text().splitlines()
         if line.strip() and line.strip() not in ("exit", "atexit")]
with tempfile.TemporaryDirectory(prefix="emerald-shim-build-") as tmp:
    executable = Path(tmp) / "test_shim"
    command = [os.environ.get("CC", "cc"), "-D_GNU_SOURCE", "-std=gnu11", "-O1", "-g",
               "-Wall", "-Wextra", "-Werror", "-pthread", "-fsanitize=address,undefined",
               "-fno-omit-frame-pointer", "-I" + str(SHIM / "include"),
               "-I" + str(SHIM / "test" / "host"), "-I" + str(SHIM / "src"),
               "-I" + str(ROOT / "android" / "host" / "include"),
               str(SHIM / "test" / "test_shim.c"), *(str(path) for path in sources),
               *("-Wl,--wrap=" + name for name in wraps), "-lm", "-o", str(executable)]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True, timeout=15)
