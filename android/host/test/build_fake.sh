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
CC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang --target=armv7a-linux-androideabi28"

mkdir -p "$OUT/jniLibs/armeabi-v7a" "$OUT/assets/romfs/fake"
$CC -std=gnu11 -O2 -fPIC -shared -Wall -Wextra -Werror \
    -I "$HOST/include" \
    "$HOST/src/ctr_host.c" "$HOST/src/jni_bridge.c" "$HERE/fake_game.c" \
    -Wl,--no-undefined -Wl,-z,max-page-size=16384 \
    -llog -landroid -lEGL -lGLESv2 \
    -o "$OUT/jniLibs/armeabi-v7a/libemerald.so"
printf 'fake romfs file\n' > "$OUT/assets/romfs/fake/hello.txt"
head -c 300000 /dev/urandom > "$OUT/assets/romfs/fake/blob.bin"
echo "built $OUT/jniLibs/armeabi-v7a/libemerald.so"
