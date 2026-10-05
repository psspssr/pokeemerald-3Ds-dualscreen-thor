#!/usr/bin/env python3
"""Sanitize the real bounded NPC allocator and camera spawn/remove sequence.

Only sprite drawing/allocation and map/script-query adapters are mocked. The
object pool, template initialization, flags, retention and camera shifts are
the generated game code. --before demonstrates the one-camera-update delay.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from run_summary_tests import function

ROOT = Path(__file__).resolve().parents[3]
TREE = Path(os.environ.get("EMERALD_TEST_TREE", ROOT / "build/upstream")).resolve()
sys.path.insert(0, str(ROOT / "tools"))
from bootstrap import PatchError, strict_apply


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", action="store_true")
    args = parser.parse_args()
    patch = ROOT / "patches/android/102-object-spawn-order.patch"
    with tempfile.TemporaryDirectory(prefix="emerald-object-spawn-") as folder:
        work = Path(folder)
        target = work / "src/event_object_movement.c"
        target.parent.mkdir()
        target.write_bytes((TREE / "src/event_object_movement.c").read_bytes())
        if patch.exists():
            try:
                strict_apply(work, patch, ["--reverse"])
            except PatchError:
                strict_apply(work, patch)
                strict_apply(work, patch, ["--reverse"])
            if not args.before:
                strict_apply(work, patch)
        elif not args.before:
            raise RuntimeError("object spawn overlay is missing")
        source = target.read_text()
        signature = "static bool8 GetAvailableObjectEventId(u16 localId, u8 mapNum, u8 mapGroup, u8 *objectEventId)"
        # This definition has documentation between its signature and brace.
        source = re.sub(re.escape(signature) + r"\n(?://[^\n]*\n)+\{", signature + "\n{", source)
        definitions = "#define sObjEventId data[0]\n"
        for name in ("sMovementTypeHasRange", "gInitialMovementTypeFacingDirections"):
            definitions += re.search(r"(?:static )?const (?:bool8|u8) " + name + r"\[.*?\] = \{.*?\n\};", source, re.S)[0] + "\n"
        signatures = (
            "static void ClearObjectEvent(struct ObjectEvent *objectEvent)", signature,
            "static u8 InitObjectEventStateFromTemplate(const struct ObjectEventTemplate *template, u8 mapNum, u8 mapGroup)",
            "static void SetObjectEventDynamicGraphicsId(struct ObjectEvent *objectEvent)",
            "void SetObjectEventDirection(struct ObjectEvent *objectEvent, u8 direction)",
            "static u8 TrySetupObjectEventSprite(const struct ObjectEventTemplate *objectEventTemplate, struct SpriteTemplate *spriteTemplate, u8 mapNum, u8 mapGroup, s16 cameraX, s16 cameraY)",
            "static u8 TrySpawnObjectEventTemplate(const struct ObjectEventTemplate *objectEventTemplate, u8 mapNum, u8 mapGroup, s16 cameraX, s16 cameraY)",
            "void TrySpawnObjectEvents(s16 cameraX, s16 cameraY)",
            "void RemoveObjectEventsOutsideView(void)",
            "static void RemoveObjectEventIfOutsideView(struct ObjectEvent *objectEvent)",
            "static void RemoveObjectEvent(struct ObjectEvent *objectEvent)",
            "static void RemoveObjectEventInternal(struct ObjectEvent *objectEvent)",
            "void UpdateObjectEventCoordsForCameraUpdate(void)",
            "void UpdateObjectEventsForCameraUpdate(s16 x, s16 y)",
        )
        definitions += "\n".join(sig + ";" for sig in signatures)
        (work / "objects_defs.inc").write_text(definitions)
        (work / "objects_helpers.inc").write_text("\n".join(function(source, sig) for sig in signatures))
        # Both live rendering choices use this camera call before rendering;
        # this fix deliberately has no voxel-mode or distance policy branch.
        camera = (TREE / "src/field_camera.c").read_text()
        assert "CameraMove(deltaX, deltaY);\n        UpdateObjectEventsForCameraUpdate(deltaX, deltaY);" in camera
        for voxel in (1, 0):
            binary = work / ("voxel-enabled" if voxel else "classic-only")
            subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g",
                            "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-ignored-qualifiers",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            "-DPORTABLE", "-DPLATFORM_3DS", "-DCTR_VOXEL_ENABLED=" + str(voxel),
                            "-I" + str(work), "-iquote" + str(TREE / "include"),
                            str(ROOT / "android/native/test/test_object_spawn.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10,
                           env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
