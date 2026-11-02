#!/usr/bin/env python3
"""Sanitize the real bounded NPC allocator and camera spawn/remove sequence.

Only sprite drawing/allocation and map/script-query adapters are mocked. The
object pool, template initialization, flags, retention, view scoring/eviction
and camera shifts are the generated game code.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

from run_summary_tests import function

ROOT = Path(__file__).resolve().parents[3]
TREE = Path(os.environ.get("EMERALD_TEST_TREE", ROOT / "build/upstream")).resolve()


def main():
    with tempfile.TemporaryDirectory(prefix="emerald-object-spawn-") as folder:
        work = Path(folder)
        source = (TREE / "src/event_object_movement.c").read_text()
        signature = "static bool8 GetAvailableObjectEventId(u16 localId, u8 mapNum, u8 mapGroup, u8 *objectEventId)"
        # This definition has documentation between its signature and brace.
        source = re.sub(re.escape(signature) + r"\n(?://[^\n]*\n)+\{", signature + "\n{", source)
        definitions = "#define sObjEventId data[0]\n"
        for name in ("OBJECT_EVENT_VIEW_MARGIN", "OBJECT_EVENT_EVICT_GAP"):
            definitions += re.search(r"^#define " + name + r" .*$", source, re.M)[0] + "\n"
        for name in ("sMovementTypeHasRange", "gInitialMovementTypeFacingDirections"):
            definitions += re.search(r"(?:static )?const (?:bool8|u8) " + name + r"\[.*?\] = \{.*?\n\};", source, re.S)[0] + "\n"
        signatures = (
            "static void ClearObjectEvent(struct ObjectEvent *objectEvent)", signature,
            "static u8 InitObjectEventStateFromTemplate(const struct ObjectEventTemplate *template, u8 mapNum, u8 mapGroup)",
            "static void SetObjectEventDynamicGraphicsId(struct ObjectEvent *objectEvent)",
            "void SetObjectEventDirection(struct ObjectEvent *objectEvent, u8 direction)",
            "static u8 TrySetupObjectEventSprite(const struct ObjectEventTemplate *objectEventTemplate, struct SpriteTemplate *spriteTemplate, u8 mapNum, u8 mapGroup, s16 cameraX, s16 cameraY)",
            "static u8 TrySpawnObjectEventTemplate(const struct ObjectEventTemplate *objectEventTemplate, u8 mapNum, u8 mapGroup, s16 cameraX, s16 cameraY)",
            "u8 GetObjectEventIdByLocalIdAndMap(u8 localId, u8 mapNum, u8 mapGroupId)",
            "static u8 GetObjectEventIdByLocalIdAndMapInternal(u8 localId, u8 mapNum, u8 mapGroupId)",
            "static u8 GetObjectEventIdByLocalId(u8 localId)",
            "static const struct ObjectEventTemplate *FindObjectEventTemplateByLocalId(u8 localId, const struct ObjectEventTemplate *templates, u8 count)",
            "static void GetObjectEventWindow(s16 *left, s16 *right, s16 *top, s16 *bottom)",
            "static float ObjectEventViewScore(s16 x, s16 y)",
            "static bool8 IsLinkPlayerObjectEvent(u8 objectEventId)",
            "static u8 GetSpawnableObjectEventCount(void)",
            "static bool8 IsInObjectEventWindow(s16 x, s16 y, s16 left, s16 right, s16 top, s16 bottom)",
            "static bool8 CanEvictObjectEvent(u8 objectEventId, s16 left, s16 right, s16 top, s16 bottom)",
            "static bool8 HasFreeObjectEventSlot(void)",
            "static bool8 EvictFurthestObjectEvent(float score, s16 left, s16 right, s16 top, s16 bottom)",
            "void TrySpawnObjectEvents(s16 cameraX, s16 cameraY)",
            "bool8 FreeObjectEventSlotNearPlayer(void)",
            "void RemoveObjectEventsOutsideView(void)",
            "static void RemoveObjectEventIfOutsideView(struct ObjectEvent *objectEvent)",
            "static void RemoveObjectEvent(struct ObjectEvent *objectEvent)",
            "static void RemoveObjectEventInternal(struct ObjectEvent *objectEvent)",
            "void UpdateObjectEventsForViewChange(void)",
            "void UpdateObjectEventCoordsForCameraUpdate(void)",
            "void UpdateObjectEventsForCameraUpdate(s16 x, s16 y)",
        )
        definitions += "\n".join(sig + ";" for sig in signatures)
        (work / "objects_defs.inc").write_text(definitions)
        (work / "objects_helpers.inc").write_text("\n".join(function(source, sig) for sig in signatures))
        camera = (TREE / "src/field_camera.c").read_text()
        assert "CameraMove(deltaX, deltaY);\n        UpdateObjectEventsForCameraUpdate(deltaX, deltaY);" in camera
        for voxel in (1, 0):
            binary = work / ("voxel-enabled" if voxel else "classic-only")
            subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g",
                            "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-ignored-qualifiers",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            "-DPORTABLE", "-DPLATFORM_3DS", "-DCTR_FIELD_VIEW_HOST",
                            "-DCTR_VOXEL_ENABLED=" + str(voxel), "-I" + str(work),
                            "-I" + str(TREE / "3ds_port/include"), "-iquote" + str(TREE / "include"),
                            str(ROOT / "android/native/test/test_object_spawn.c"),
                            str(TREE / "3ds_port/src/3ds_field_view.c"), "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10,
                           env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
