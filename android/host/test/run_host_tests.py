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
    # Rename only the host's reference to origin's main, so this second test
    # can start the real detached game thread with a controlled game fixture.
    flags = command[:command.index(str(HOST / "src/ctr_host.c"))]
    obj = Path(directory) / "host_mystery.o"
    subprocess.run(flags + ["-Dmain=TestGameMain", "-c", str(HOST / "src/ctr_host.c"),
                            "-o", str(obj)], check=True)
    mystery = Path(directory) / "test_mystery_host"
    subprocess.run(flags + [str(obj), str(HOST / "test/test_mystery_host.c"),
                            "-o", str(mystery)], check=True)
    subprocess.run([str(mystery)], check=True, timeout=15)
    # The production APT listener ordering releases GPU windows before the
    # host acknowledges pause. Exercise both main and secondary window slots.
    root = HOST.parents[1]
    lifecycle = Path(directory) / "test_window_lifecycle"
    lifecycle_flags = [flags[0], "-I" + str(root / "android/shim/test/host"), *flags[1:]]
    subprocess.run(lifecycle_flags + ["-I" + str(root / "android/shim/include"),
                            str(obj), str(root / "android/shim/src/apt.c"),
                            str(HOST / "test/test_window_lifecycle.c"),
                            "-o", str(lifecycle)], check=True)
    for window in ("0", "1"):
        for phase in ("before", "during", "settled"):
            subprocess.run([str(lifecycle), phase, window], check=True, timeout=5)
    diagnostics = Path(directory) / "test_diagnostics"
    subprocess.run(flags + [str(HOST / "src/diagnostics.c"), str(HOST / "test/test_diagnostics.c"),
                            "-o", str(diagnostics)], check=True)
    subprocess.run([str(diagnostics)], check=True, timeout=15)
