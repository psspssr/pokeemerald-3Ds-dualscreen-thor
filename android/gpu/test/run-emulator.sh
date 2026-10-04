#!/usr/bin/env bash
# Run the real GLES backend's pixel assertions on a connected Android device.
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$repo_root"
ndk_dir="${ANDROID_NDK_HOME:-${NDK:-$repo_root/build/tooling/android-ndk-r27c}}"
adb_bin="${ADB:-${ANDROID_HOME:-/root/android-sdk}/platform-tools/adb}"
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
"$compiler" -std=gnu11 -O2 -Wall -Wextra -Werror -D__3DS__ -DCTR_GPU_TEST \
    -Iandroid/gpu/include -Iandroid/shim/include -Iandroid/host/include \
    android/gpu/test/offscreen.c android/gpu/src/*.c android/gpu/src/maths/*.c \
    -Wl,--wrap=glUniform4fv -Wl,--wrap=glBindTexture -Wl,--wrap=GX_BindQueue \
    -Wl,--wrap=glDisableVertexAttribArray -Wl,--wrap=glVertexAttribPointer \
    -lEGL -lGLESv3 -landroid -llog -lm -o "$out/gpu-offscreen-test"
"${adb_cmd[@]}" push "$out/gpu-offscreen-test" "$out/voxel.shbin" /data/local/tmp/
"${adb_cmd[@]}" shell /data/local/tmp/gpu-offscreen-test /data/local/tmp/voxel.shbin "${@:2}" | tee "$out/result.log"
