#!/usr/bin/env python3
"""Exercise the production AAudio recovery worker with controlled platform I/O."""
import os
from pathlib import Path
import subprocess
import tempfile

SHIM = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="emerald-aaudio-test-") as directory:
    work = Path(directory)
    flags = [os.environ.get("CC", "cc"), "-D_GNU_SOURCE", "-std=c11", "-O1", "-g",
             "-Wall", "-Wextra", "-Werror", "-pthread", "-fsanitize=address,undefined",
             "-fno-omit-frame-pointer", "-I" + str(SHIM / "test/host"),
             "-I" + str(SHIM / "include"), "-I" + str(SHIM / "src")]
    obj = work / "backend.o"
    subprocess.run(flags + ["-Dpthread_cond_timedwait=TestAudioTimedWait", "-c",
                            str(SHIM / "src/ndsp_aaudio.c"), "-o", str(obj)], check=True)
    binary = work / "test_aaudio"
    subprocess.run(flags + [str(obj), str(SHIM / "test/test_aaudio_recovery.c"),
                            "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=15,
                   env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
