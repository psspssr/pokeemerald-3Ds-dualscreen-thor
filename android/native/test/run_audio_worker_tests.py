#!/usr/bin/env python3
"""Run upstream's sound-worker handoff on the production Android thread/lock shim."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SHIM = ROOT / "android/shim"
PORT = ROOT / "origin/3ds_port"


def function(source, signature):
    match = re.search(re.escape(signature) + r"\s*\{", source)
    assert match, signature
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main():
    source = (PORT / "src/3ds_audio.c").read_text()
    declarations = []
    for name in ("sWorker", "sKick, sIdle", "sWorkerQuit", "sReady", "sSamples"):
        match = re.search(r"^static [^;\n]*\b" + re.escape(name) + r";", source, re.M)
        assert match, name
        declarations.append(match[0])
    actual = "\n".join(declarations) + "\n"
    actual += source[source.index("#define CTR_AUDIO_SYSCORE_PERCENT"):]
    actual += "\n" + function(source, "void CtrAudio_Shutdown(void)")
    with tempfile.TemporaryDirectory(prefix="emerald-audio-worker-") as directory:
        work = Path(directory)
        (work / "audio_worker_actual.inc").write_text(actual)
        binary = work / "audio-worker"
        subprocess.run([
            os.environ.get("CC", "cc"), "-D_GNU_SOURCE", "-std=gnu11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-pthread", "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer", "-ffunction-sections", "-fdata-sections",
            "-I" + str(work), "-I" + str(SHIM / "include"),
            "-I" + str(SHIM / "test/host"), "-I" + str(SHIM / "src"),
            "-I" + str(ROOT / "android/host/include"), "-I" + str(PORT / "include"),
            str(ROOT / "android/native/test/test_audio_worker.c"),
            *(str(SHIM / "src" / (name + ".c")) for name in ("thread", "sync", "svc", "apt")),
            "-Wl,--wrap=threadCreate", "-Wl,--wrap=svcGetSystemTick", "-Wl,--gc-sections",
            "-o", str(binary)
        ], check=True)
        # The real engine is one-shot per process. Each case uses a fresh set
        # of upstream static state, as a newly launched Android process does.
        for case in ("handoff", "shutdown", "fallback"):
            subprocess.run([str(binary), case], check=True, timeout=10,
                           env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
