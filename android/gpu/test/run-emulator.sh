#!/usr/bin/env bash
# Run the real GLES backend's pixel assertions on a connected Android device.
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$repo_root"
ndk_dir="${ANDROID_NDK_HOME:-${NDK:-$repo_root/build/tooling/android-ndk-r27c}}"
if [[ -n "${ADB:-}" ]]; then
    adb_bin="$(command -v "$ADB" || true)"
elif [[ -n "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}" ]]; then
    adb_bin="${ANDROID_HOME:-$ANDROID_SDK_ROOT}/platform-tools/adb"
else
    adb_bin="$(command -v adb || true)"
fi
if [[ -z "$adb_bin" || ! -x "$adb_bin" ]]; then
    echo "adb not found; set ADB, ANDROID_HOME or ANDROID_SDK_ROOT, or add adb to PATH." >&2
    exit 1
fi
serial="${1:-${ANDROID_SERIAL:-}}"
adb_cmd=("$adb_bin")
if [[ -n "$serial" ]]; then adb_cmd+=(-s "$serial"); fi
abi="$("${adb_cmd[@]}" shell getprop ro.product.cpu.abi | tr -d '\r')"
case "$abi" in
    x86_64) target=x86_64-linux-android28 ;;
    x86) target=i686-linux-android28 ;;
    arm64-v8a) target=aarch64-linux-android28 ;;
    armeabi-v7a) target=armv7a-linux-androideabi28 ;;
    *) echo "Unsupported or disconnected Android device: $abi" >&2; exit 1 ;;
esac
compiler="$ndk_dir/toolchains/llvm/prebuilt/linux-x86_64/bin/$target-clang"
out="$repo_root/build/gpu-tests"
mkdir -p "$out"
python3 tools/picasso2glsl.py origin/3ds_port/src/voxel/voxel.v.pica -o "$out/voxel.shbin"
python3 - "$out/upstream_voxel_diorama.inc" "$out/upstream_voxel_warmup.inc" "$out/upstream_battle_scene.inc" <<'PY'
from pathlib import Path
import os
import re
import sys
import tempfile
source = Path("origin/3ds_port/src/3ds_video.c").read_text()
first = source.index("#define DIORAMA_TOP")
last = source.index("/*\n * Bloom:", first)
Path(sys.argv[1]).write_text(source[first:last])
# Exercise the real render-dispatch branch, including the complete strict
# Android overlay order. The opt-in old image proves the pixel regression.
sys.path.insert(0, "tools")
from bootstrap import patch_files, strict_apply
relative = "3ds_port/src/3ds_video.c"
with tempfile.TemporaryDirectory(prefix="emerald-warmup-gles-") as directory:
    stage = Path(directory)
    path = stage / relative
    path.parent.mkdir(parents=True)
    path.write_text(source)
    for patch in sorted(Path("patches/android").glob("*.patch")):
        if os.environ.get("CTR_GPU_WARMUP_BEFORE") == "1" and patch.name.startswith("096-"):
            continue
        if os.environ.get("CTR_GPU_BATTLE_FILTER_BEFORE") == "1" and patch.name.startswith("098-"):
            continue
        if relative in patch_files(patch):
            strict_apply(stage, patch, ("--include=" + relative,))
    branch = re.search(r"    else if \(blank\)\n    \{(.*?)\n    \}", path.read_text(), re.S)
    if branch is None:
        raise SystemExit("upstream voxel warm-up branch changed")
    Path(sys.argv[2]).write_text(branch.group(1) + "\n")
    source = path.read_text()
    def function(signature, text=source):
        start = text.index(signature)
        return text[start:text.index("\n}\n", start) + 3]
    names = ("SCENE_ZOOM", "SCENE_W", "SCENE_H", "VIEW_LEFT", "VIEW_TOP", "VIEW_RIGHT", "VIEW_BOTTOM",
             "LAYER_IDLE_FRAMES", "LAYER_RETRY_FRAMES", "TRANSITION_ZOOM", "TRANSITION_X",
             "TRANSITION_HALF", "TRANSITION_HALF_X", "TRANSITION_HALF_TOP", "BATTLE_SHADOW_W",
             "BATTLE_SHADOW_H", "BATTLE_SHADOW_ALPHA", "BLOOM_W", "BLOOM_H", "BLOOM_THRESHOLD")
    body = "\n".join(re.search(r"^#define " + name + r"\s+.*$", source, re.M)[0] for name in names) + "\n"
    for signature in ("static void SceneRelease(void)", "static void SceneSetFilter(void)",
                      "static void ScenePrepare(void)", "static bool BattleStatLayer(void)",
                      "static void RenderBattleScene(uint32_t clear)"):
        if signature in source:
            body += function(signature)
    body += re.search(r"typedef struct\n\{\n    int dx, dy;.*?\} TransitionBand;", source, re.S)[0] + "\n"
    body += function("static TransitionBand TransitionBandAt(int g, unsigned targets)")
    body += function("static void TransitionPicture(void)")
    body += function("static bool BattleShadowTexture(void)")
    body += function("static void VoxelBloomPrepare(void)")
    body += function("static void VoxelBloomCompose(float strength)")
    body += function("uint32_t CtrVideo_Texel(unsigned x, unsigned y, unsigned width)",
                     Path("origin/3ds_port/src/3ds_video_decode.c").read_text())
    Path(sys.argv[3]).write_text(body)
PY
"$compiler" -std=gnu11 -O2 -Wall -Wextra -Werror -D__3DS__ -DCTR_GPU_TEST \
    -Iandroid/gpu/include -Iandroid/shim/include -Iandroid/host/include -Iorigin/3ds_port/include -I"$out" \
    android/gpu/test/offscreen.c android/gpu/src/*.c android/gpu/src/maths/*.c android/host/src/diagnostics.c \
    -Wl,--wrap=glUniform4fv -Wl,--wrap=glBindTexture -Wl,--wrap=GX_BindQueue \
    -Wl,--wrap=glDisableVertexAttribArray -Wl,--wrap=glVertexAttribPointer \
    -lEGL -lGLESv3 -landroid -llog -lm -o "$out/gpu-offscreen-test"
"${adb_cmd[@]}" push "$out/gpu-offscreen-test" "$out/voxel.shbin" /data/local/tmp/
"${adb_cmd[@]}" shell /data/local/tmp/gpu-offscreen-test /data/local/tmp/voxel.shbin "${@:2}" | tee "$out/result.log"
