#!/usr/bin/env bash
# iPhone build, sign and install run by Sessionist's Recomp Hub. Run from the repo root.
# Env from Sessionist: PORT_DIR, TEAM, BUNDLE, DEVICE, ICLOUD_CONTAINER.
# Needs the Mac build first (guest code and host shader cache).
set -e -o pipefail
cd "$(dirname "$0")/.."
. sessionist/common.sh

: "${PORT_DIR:?PORT_DIR is not set}"
SDK="$PORT_DIR/rexglue-sdk"
update_clone "$SDK" "https://github.com/n-elnouby/rexglue-sdk.git" ios-port

if [ ! -f out/build/ios-arm64/build.ninja ]; then
  SHADER_CACHE="$PWD/out/build/mac-arm64-release/generated/shader_cache.cpp"
  [ -f "$SHADER_CACHE" ] || { echo "[RED] Missing $SHADER_CACHE: run the Mac source build once first to generate guest code and the host shader cache."; exit 1; }
  cmake -S . -B out/build/ios-arm64 -G Ninja \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DREXSDK_DIR="$SDK" -DREXGLUE_ROOT="$SDK" \
    -DREBLUE_IOS_BUNDLE_ID="$BUNDLE" \
    -DREBLUE_ICLOUD_CONTAINER="${ICLOUD_CONTAINER:-}" \
    -DREBLUE_PREBUILT_SHADER_CACHE="$SHADER_CACHE"
fi
tools/ios_deploy.sh "$TEAM" "$BUNDLE" "$DEVICE"
