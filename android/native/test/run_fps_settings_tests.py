#!/usr/bin/env python3
"""Check Android FPS/voxel-blur defaults and persistence across fresh processes."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "origin/3ds_port"
sys.path.insert(0, str(ROOT / "tools"))
from bootstrap import strict_apply


def main():
    with tempfile.TemporaryDirectory(prefix="emerald-fps-settings-") as directory:
        work = Path(directory)
        settings_source = work / "3ds_port/src/3ds_settings.c"
        settings_source.parent.mkdir(parents=True)
        settings_source.write_bytes((SOURCE / "src/3ds_settings.c").read_bytes())
        strict_apply(work, ROOT / "patches/android/074-sharp-voxel-defaults.patch")
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
    assert(argc == 6);
    settingsPath = argv[1];
    assert(!CtrSettings_ShowFps()); /* An unconfigured process starts off. */
    assert(!CtrSettings_VoxelBlur());
    CtrSettings_Load();
    assert(CtrSettings_ShowFps() == (argv[2][0] == '1'));
    assert(CtrSettings_VoxelBlur() == (argv[4][0] == '1'));
    CtrSettings_SetShowFps(argv[3][0] == '1');
    CtrSettings_SetVoxelBlur(argv[5][0] == '1');
    CtrSettings_Shutdown();
    return 0;
}
''')
        binary = work / "fps-settings"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g",
                        "-Wall", "-Wextra", "-Werror", "-pthread",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I" + str(SOURCE / "tests/settings_host"),
                        "-I" + str(SOURCE / "include"), "-I" + str(settings_source.parent),
                        str(fixture), "-o", str(binary)], check=True)
        for kind in ("fresh", "legacy", "explicit-blur-on", "explicit-blur-off"):
            settings = work / (kind + ".txt")
            if kind == "legacy":
                settings.write_text("voxel=1\nvoxel_pitch=40\nvoxel_zoom=100\n")
            elif kind.startswith("explicit-blur"):
                settings.write_text("voxel=1\nvoxel_blur=" + ("1" if kind.endswith("on") else "0") + "\n")
            blur = int(kind.endswith("on"))
            # Every check is a fresh process: no test mutation of cached flags.
            for before, after in ((0, 1), (1, 0), (0, 0)):
                next_blur = 1 - blur
                subprocess.run([str(binary), str(settings), str(before), str(after), str(blur), str(next_blur)],
                               check=True, timeout=10,
                               env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
                assert f"fps={after}\n" in settings.read_text()
                assert f"voxel_blur={next_blur}\n" in settings.read_text()
                blur = next_blur
        print("PASS FPS and voxel blur default off; fresh/legacy and explicit choices persist across restart")


if __name__ == "__main__":
    main()
