#!/bin/sh
# Builds a test libemerald.so (fake_game.c + the host sources) and a tiny RomFS
# into a nativeOut directory the app's Gradle build can consume:
#   android/host/test/build_fake.sh [OUT]   (default /tmp/emerald-fake-out)
#   cd android/app && ./gradlew assembleDebug -Pemerald.nativeOut=OUT
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
HOST=$(dirname "$HERE")
OUT=${1:-/tmp/emerald-fake-out}
SDK=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}
NDK=${ANDROID_NDK_HOME:-$SDK/ndk/27.2.12479018}
CC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"

for ABI in armeabi-v7a x86_64; do
case "$ABI" in
    armeabi-v7a) TARGET=armv7a-linux-androideabi28 ;;
    x86_64) TARGET=x86_64-linux-android28 ;;
esac
mkdir -p "$OUT/jniLibs/$ABI" "$OUT/assets/romfs/fake"
"$CC" --target="$TARGET" -std=gnu11 -O2 -fPIC -shared -Wall -Wextra -Werror \
    -DCTR_HOST_HARNESS -I "$HOST/include" \
    "$HOST/src/ctr_host.c" "$HOST/src/jni_bridge.c" "$HOST/src/diagnostics.c" "$HERE/fake_game.c" \
    -Wl,--no-undefined -Wl,-z,max-page-size=16384 \
    -llog -landroid -lEGL -lGLESv2 \
    -o "$OUT/jniLibs/$ABI/libemerald.so"
done
printf 'fake romfs file\n' > "$OUT/assets/romfs/fake/hello.txt"
head -c 300000 /dev/zero > "$OUT/assets/romfs/fake/blob.bin"
echo "built $OUT/jniLibs/armeabi-v7a/libemerald.so"
