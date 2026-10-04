#!/usr/bin/env python3
"""Check actual upstream FPS defaults and persistence across fresh processes."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "origin/3ds_port"


def main():
    with tempfile.TemporaryDirectory(prefix="emerald-fps-settings-") as directory:
        work = Path(directory)
        fixture = work / "test.c"
        fixture.write_text(r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "3ds_settings.c"
static const char *settingsPath;
FILE *CtrFs_OpenData(const char *name, const char *mode) {
    assert(strcmp(name, "settings.txt") == 0);
    return fopen(settingsPath, mode);
}
void CtrLog_Write(CtrLogCategory category, const char *format, ...) {
    (void)category; (void)format;
}
int main(int argc, char **argv) {
    assert(argc == 4);
    settingsPath = argv[1];
    assert(!CtrSettings_ShowFps()); /* An unconfigured process starts off. */
    CtrSettings_Load();
    assert(CtrSettings_ShowFps() == (argv[2][0] == '1'));
    CtrSettings_SetShowFps(argv[3][0] == '1');
    CtrSettings_Shutdown();
    return 0;
}
''')
        binary = work / "fps-settings"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g",
                        "-Wall", "-Wextra", "-Werror", "-pthread",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I" + str(SOURCE / "tests/settings_host"),
                        "-I" + str(SOURCE / "include"), "-I" + str(SOURCE / "src"),
                        str(fixture), "-o", str(binary)], check=True)
        for kind in ("fresh", "legacy"):
            settings = work / (kind + ".txt")
            if kind == "legacy":
                settings.write_text("voxel=1\nvoxel_pitch=40\nvoxel_zoom=100\nvoxel_blur=0\n")
            # Every check is a fresh process: no test mutation of cached flags.
            for before, after in ((0, 1), (1, 0), (0, 0)):
                subprocess.run([str(binary), str(settings), str(before), str(after)],
                               check=True, timeout=10,
                               env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
                assert f"fps={after}\n" in settings.read_text()
        print("PASS FPS defaults off for fresh/legacy settings; explicit on/off persists across restart")


if __name__ == "__main__":
    main()
