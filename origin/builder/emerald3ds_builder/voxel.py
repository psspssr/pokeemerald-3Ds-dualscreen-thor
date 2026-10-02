"""Run the port's voxel generators on the tree rebuilt from the ROM.

The generator scripts ship with the release (payload/voxelgen/, the same files
as 3ds_port/scripts in the source tree). They are run one by one, each in its
own process so no state carries over between them, with the rebuilt tree as
their repository root. Their outputs must match the CRCs the release was built
with; anything else would be a different game data set than the executable
expects.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import zlib
from pathlib import Path

from .errors import BuilderError

# (script, arguments, output path relative to the tree) in the order the
# generators depend on each other.
STEPS = [
    ("gen_voxel_regions.py", [], "3ds_port/romfs/voxel/regions.bin"),
    ("gen_voxel_sign_masks.py", [], "3ds_port/romfs/voxel/signposts.bin"),
    ("gen_voxel_relief.py", ["--output", "3ds_port/romfs/voxel/relief.bin"], "3ds_port/romfs/voxel/relief.bin"),
    ("gen_voxel_buildings.py", ["--output", "3ds_port/romfs/voxel/buildings.bin"],
     "3ds_port/romfs/voxel/buildings.bin"),
    # not voxel data, but made the same way: the intro's leaves scene, widened
    ("gen_intro_margins.py", ["--output", "3ds_port/romfs/stage/leaves.bin"], "3ds_port/romfs/stage/leaves.bin"),
]


def script_command(script: Path, args: list[str]) -> list[str]:
    """How to run one bundled script: through this very executable when frozen."""
    if getattr(sys, "frozen", False):
        exe = Path(sys.executable)
        console = exe.with_name("emerald3ds-builder-cli" + exe.suffix)
        return [str(console if console.exists() else exe), "--run-script", str(script)] + args
    return [sys.executable, "-B", "-m", "emerald3ds_builder", "--run-script", str(script)] + args


def run_generators(tree: Path, voxelgen: Path, expected: list[dict], progress=None) -> dict[str, bytes]:
    if not voxelgen.is_dir():
        raise BuilderError("The release is incomplete: payload/voxelgen is missing.")
    port = tree / "3ds_port"
    shutil.copytree(voxelgen, port, dirs_exist_ok=True)
    (port / "romfs" / "voxel").mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    here = Path(__file__).resolve().parent.parent
    env["PYTHONPATH"] = str(here) + os.pathsep + env.get("PYTHONPATH", "")
    steps = [s for s in STEPS if (port / "scripts" / s[0]).exists()]
    for i, (script, args, _) in enumerate(steps):
        cmd = script_command(port / "scripts" / script, [a if not a.startswith("3ds_port/") else str(tree / a)
                                                         for a in args])
        result = subprocess.run(cmd, cwd=str(port), env=env, capture_output=True, text=True,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        if result.returncode != 0:
            raise BuilderError("A voxel data generator failed (%s)." % script,
                               (result.stderr or result.stdout)[-1500:])
        if progress:
            progress((i + 1) / len(steps))
    outputs: dict[str, bytes] = {}
    for item in expected:
        rel = item["path"]
        path = port / "romfs" / rel
        if not path.exists():
            raise BuilderError("A voxel data file was not produced: %s" % rel)
        data = path.read_bytes()
        if len(data) != item["size"] or zlib.crc32(data) & 0xFFFFFFFF != item["crc"]:
            raise BuilderError("A voxel data file does not match this release: %s" % rel,
                               "The generated file differs from the one the game was built with.")
        outputs[rel] = data
    return outputs
