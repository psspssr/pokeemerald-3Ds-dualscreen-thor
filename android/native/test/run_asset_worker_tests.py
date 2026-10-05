#!/usr/bin/env python3
"""Exercise real asset warmup and platform shutdown on Android's thread shim."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from run_audio_worker_tests import function
from run_starter_tests import declaration

ROOT = Path(__file__).resolve().parents[3]
TREE = Path(os.environ.get("EMERALD_TEST_TREE", ROOT / "build/upstream")).resolve()
sys.path.insert(0, str(ROOT / "tools"))
from bootstrap import PatchError, strict_apply, patch_files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", action="store_true")
    parser.add_argument("--case", choices=("all", "loose", "pack", "finished", "failure"), default="all")
    args = parser.parse_args()
    patch = ROOT / "patches/android/094-join-asset-warmup.patch"
    with tempfile.TemporaryDirectory(prefix="emerald-asset-worker-") as directory:
        work = Path(directory)
        for rel in patch_files(patch):
            p = work / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes((TREE / rel).read_bytes())
        try:
            strict_apply(work, patch, ["--reverse"])
        except PatchError:
            strict_apply(work, patch)
            strict_apply(work, patch, ["--reverse"])
        if not args.before:
            strict_apply(work, patch)
        assets = (work / "3ds_port/src/3ds_assets.c").read_text()
        platform = (work / "3ds_port/src/3ds_platform.c").read_text()
        helpers = declaration(assets, "struct AssetMapEntry\n") + declaration(assets, "struct AssetPayload\n")
        (work / "asset_types.inc").write_text(helpers)
        wrappers = "\n".join(function(platform, signature) for signature in (
            "void CtrLock_Init(CtrLock *lock)", "void CtrLock_Lock(CtrLock *lock)",
            "void CtrLock_Unlock(CtrLock *lock)",
            "bool CtrPlatform_StartThread(void (*entry)(void *), void *arg, unsigned stack, int core)"))
        if not args.before:
            wrappers += "\n" + function(platform, "CtrThread CtrPlatform_StartJoinableThread(void (*entry)(void *), void *arg, unsigned stack, int core)")
            wrappers += "\n" + function(platform, "void CtrPlatform_JoinThread(CtrThread thread)")
        (work / "asset_thread_helpers.inc").write_text(wrappers)
        (work / "asset_worker.inc").write_text(assets[assets.index("#define WARM_BUDGET_SHARE"):assets.index("void CtrAssets_SetBudget")])
        (work / "asset_shutdown.inc").write_text(function(platform, "void CtrPlatform_Shutdown(void)"))
        binary = work / "asset-worker"
        subprocess.run([os.environ.get("CC", "cc"), "-D_GNU_SOURCE", "-std=gnu11", "-O1", "-g",
                        "-Wall", "-Wextra", "-Werror", "-pthread", "-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer", "-ffunction-sections", "-fdata-sections",
                        "-DTEST_FIXED=" + str(int(not args.before)), "-I" + str(work),
                        "-I" + str(work / "3ds_port/include"), "-I" + str(ROOT / "android/shim/include"),
                        "-I" + str(ROOT / "android/shim/test/host"), "-I" + str(ROOT / "android/shim/src"),
                        "-I" + str(ROOT / "android/host/include"),
                        "-I" + str(TREE / "3ds_port/include"),
                        str(ROOT / "android/native/test/test_asset_worker.c"),
                        *(str(ROOT / "android/shim/src" / (name + ".c")) for name in ("thread", "sync", "svc")),
                        "-Wl,--wrap=threadCreate", "-Wl,--gc-sections", "-o", str(binary)], check=True)
        cases = ("loose", "pack", "finished", "failure") if args.case == "all" else (args.case,)
        for case in cases:
            subprocess.run([str(binary), case], check=True, timeout=10,
                           env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
