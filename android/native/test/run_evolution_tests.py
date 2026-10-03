#!/usr/bin/env python3
"""Check actual evolution sparkle geometry under its real make target flags.

Run bootstrap first. --full-makefile permits an isolated before/after overlay
comparison without changing the generated game tree.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
TREE = Path(os.environ.get("EMERALD_TEST_TREE", ROOT / "build/upstream")).resolve()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--full-makefile", type=Path, default=TREE / "3ds_port/full.mk")
    args = parser.parse_args()
    source = (TREE / "src/evolution_graphics.c").read_text()
    helpers = source[source.index("#define sSpeed "):source.index("void LoadEvoSparkleSpriteAndPal(void)")]
    makefile = args.full_makefile.read_text()
    rules = makefile[makefile.index("CTR_GBA_CENTRED_SRCS :="):makefile.index("# Which centred screen")]
    with tempfile.TemporaryDirectory(prefix="emerald-evolution-sparkles-") as directory:
        work = Path(directory)
        (work / "sparkles.inc").write_text(helpers)
        probe = work / "probe.mk"
        probe.write_text(rules + "\nbuild/root/src/evolution_graphics.o:\n"
                         "\t@printf '%s\\n' '$(FULLCFLAGS)'\n")
        result = subprocess.run(["make", "--no-print-directory", "-s", "-B", "-f", str(probe),
                                 "ROOT=" + str(TREE), "build/root/src/evolution_graphics.o"],
                                cwd=TREE / "3ds_port", check=True, capture_output=True, text=True)
        target_flags = shlex.split(result.stdout.strip())
        binary = work / "sparkles"
        subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g",
                        "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-D_GNU_SOURCE", "-DPORTABLE", "-DMODERN=1", "-DPLATFORM_3DS",
                        *target_flags, "-iquote" + str(TREE / "include"),
                        "-iquote" + str(TREE / "3ds_port/compat"), "-iquote" + str(work),
                        str(ROOT / "android/native/test/test_evolution_graphics.c"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10,
                       env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
