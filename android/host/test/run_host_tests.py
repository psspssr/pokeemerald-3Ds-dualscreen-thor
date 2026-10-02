#!/usr/bin/env python3
"""Exercise production host configuration and prompt synchronization on POSIX."""
import os
from pathlib import Path
import subprocess
import tempfile

HOST = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="emerald-host-test-") as directory:
    binary = Path(directory) / "test_host"
    command = [os.environ.get("CC", "cc"), "-D_GNU_SOURCE", "-std=c11", "-O1", "-g",
               "-Wall", "-Wextra", "-Werror", "-pthread", "-fsanitize=address,undefined",
               "-fno-omit-frame-pointer", "-I" + str(HOST / "test/stubs"),
               "-I" + str(HOST / "include"), str(HOST / "src/ctr_host.c"),
               str(HOST / "test/test_host.c"), "-o", str(binary)]
    subprocess.run(command, check=True)
    # Individual condition waits below are bounded too. This outer deadline
    # catches a deadlock before any test callback has had a chance to signal.
    subprocess.run([str(binary)], check=True, timeout=15)
