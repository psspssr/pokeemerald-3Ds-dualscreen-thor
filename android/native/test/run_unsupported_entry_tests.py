#!/usr/bin/env python3
"""Exercise the actual title-to-Berry callback, with Android and original routing."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", action="store_true", help="prove the unfixed Android dead end")
    parser.add_argument("--source", type=Path)
    args = parser.parse_args()
    source = args.source or Path(os.environ.get("EMERALD_TEST_TREE", ROOT / "build/upstream")) / "src/title_screen.c"
    patch = ROOT / "patches/android/090-unsupported-berry-transfer.patch"
    with tempfile.TemporaryDirectory(prefix="emerald-unsupported-entry-") as directory:
        stage = Path(directory)
        copy = stage / "src/title_screen.c"
        copy.parent.mkdir()
        copy.write_bytes(source.read_bytes())
        patched = "No GBA link transport exists here." in copy.read_text()
        if patched:
            subprocess.run(["git", "apply", "--reverse", "--check", str(patch)], cwd=stage, check=True)
            if args.before:
                subprocess.run(["git", "apply", "--reverse", str(patch)], cwd=stage, check=True)
        elif not args.before:
            subprocess.run(["git", "apply", str(patch)], cwd=stage, check=True)
        text = copy.read_text()
        match = re.search(r"static void CB2_GoToBerryFixScreen\(void\)\s*\{", text)
        assert match
        start, end, depth = match.start(), match.end(), 1
        while depth:
            depth += (text[end] == "{") - (text[end] == "}")
            end += 1
        actual = text[start:end]
        harness = stage / "probe.c"
        harness.write_text("""
#include <assert.h>
#include <stdio.h>
static void (*callback)(void);
static unsigned fade, stops, menuFrames, unsupportedFrames;
int UpdatePaletteFade(void) { return fade; }
void m4aMPlayAllStop(void) { ++stops; }
void CB2_InitMainMenu(void) { ++menuFrames; }
void CB2_InitBerryFixProgram(void) { ++unsupportedFrames; }
void SetMainCallback2(void (*next)(void)) { callback = next; }
""" + actual + """
int main(void) {
    fade = 1;
    callback = CB2_GoToBerryFixScreen;
    callback();
    assert(callback == CB2_GoToBerryFixScreen && stops == 0);
    fade = 0;
    callback();
    assert(stops == 1);
#ifdef __ANDROID__
    assert(callback == CB2_InitMainMenu);
    for (unsigned frame = 0; frame < 60; ++frame) callback();
    assert(menuFrames == 60 && unsupportedFrames == 0);
#else
    assert(callback == CB2_InitBerryFixProgram);
    callback();
    assert(unsupportedFrames == 1 && menuFrames == 0);
#endif
    puts("PASS actual Berry transition waits for fade; Android returns to main menu, original route preserved");
}
""")
        for android in (True, False):
            executable = stage / ("android-entry" if android else "original-entry")
            command = [os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g",
                       "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                       "-fno-omit-frame-pointer"]
            if android:
                command.append("-D__ANDROID__")
            subprocess.run(command + [str(harness), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True, timeout=10,
                           env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
