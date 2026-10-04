#!/usr/bin/env python3
"""Check exact Android overlays and sanitize their real bottom touch routing."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from run_summary_tests import function

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
from bootstrap import strict_apply


def main():
    paths = ("3ds_port/src/3ds_video.c", "3ds_port/src/3ds_bottom_ui.c")
    with tempfile.TemporaryDirectory(prefix="emerald-bottom-content-") as folder:
        work = Path(folder)
        for name in paths:
            target = work / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((ROOT / "origin" / name).read_bytes())
        for patch in sorted((ROOT / "patches/android").glob("*.patch")):
            strict_apply(work, patch, ["--include=" + name for name in paths])
        video, bottom = ((work / name).read_text() for name in paths)
        eye = function(video, "static void RenderEye(C3D_RenderTarget *target, uint32_t clear, float parallax)")
        assert "CtrHost_SetBottomMenuContent(AndroidBottomMenu_Content(sCentredScreen))" in eye
        # The logical blit remains1:1. Only the final presenter changes sampling.
        assert "C2D_DrawImageAt((C2D_Image){&sSurface, &middle}, 0, 0, 0, NULL, 1, 1);" in eye
        present = function(video, "void CtrVideo_Present(void)")
        assert present.count("CtrHost_SetBottomMenuContent(CTR_HOST_BOTTOM_ORIGINAL)") == 2
        modes = re.search(r"enum\s*\{\s*MODE_OFF,.*?\};", bottom, re.S)[0]
        state = re.search(r"static struct\s*\{\s*bool8 active, dragged;.*?\} sTouch;", bottom, re.S)[0]
        (work / "bottom_state.inc").write_text(modes + "\n" + state + "\n")
        (work / "bottom_mode.inc").write_text(function(bottom, "static u8 CurrentMode(void)"))
        (work / "bottom_touch.inc").write_text(function(bottom, "static u8 ProcessTouch(u8 mode)"))
        binary = work / "bottom-content"
        subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g",
                        "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I" + str(work), "-I" + str(ROOT / "android/native/include"),
                        "-I" + str(ROOT / "android/gpu/include"), "-I" + str(ROOT / "android/host/include"),
                        "-I" + str(ROOT / "origin/3ds_port/include"),
                        str(ROOT / "android/native/test/test_bottom_menu_touch.c"),
                        str(ROOT / "android/gpu/src/bottom_content.c"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=15,
                       env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
