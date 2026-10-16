#!/usr/bin/env bash
# Unpacks the pre-release "android-dev-deps" (.github/workflows/android-dev-deps.yml)
# into ~/android: the NDK, FreeType + OCCT for an ABI and the SDK tools, so
# the app builds here as in CI. Then:
#   export ANDROID_NDK_HOME=~/android/android-ndk-r27c ANDROID_HOME=~/android/sdk
#   export BZ_ANDROID_DEPS=~/android/deps/x86_64
#   cmake --preset android-x86_64 && cmake --build --preset android-x86_64
#
# Usage: dev-setup.sh [abi]   (x86_64 by default; arm64-v8a)
set -euo pipefail
ABI=${1:-x86_64}
DEST=$HOME/android
mkdir -p "$DEST/deps"
cd "$DEST"
for f in android-ndk android-sdk "android-deps-$ABI"; do
    # Direct links: gh release download asks GraphQL, closed in the sandbox.
    [ -f "$f.tar.xz" ] || curl -fL --retry 3 -o "$f.tar.xz" "https://github.com/MikeVerx/breezecad/releases/download/android-dev-deps/$f.tar.xz"
done
[ -d android-ndk-r27c ] || tar -xJf android-ndk.tar.xz
[ -d sdk ] || tar -xJf android-sdk.tar.xz
[ -d "deps/$ABI" ] || tar -C deps -xJf "android-deps-$ABI.tar.xz"
echo "ANDROID_NDK_HOME=$DEST/android-ndk-r27c ANDROID_HOME=$DEST/sdk BZ_ANDROID_DEPS=$DEST/deps/$ABI"
