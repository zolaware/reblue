#!/usr/bin/env bash
# Mac build run by Sessionist's Recomp Hub. Run from the repo root.
# Env from Sessionist: PORT_DIR (the port's folder), ICLOUD_CONTAINER (iCloud.<bundle id>).
# Output: dist/reblue.app
set -e -o pipefail
cd "$(dirname "$0")/.."
. sessionist/common.sh

: "${PORT_DIR:?PORT_DIR is not set}"
SDK="$PORT_DIR/rexglue-sdk"
update_clone "$SDK" "https://github.com/n-elnouby/rexglue-sdk.git" ios-port

configure() {
  # REXGLUE_ROOT: older SDK checkouts need it to find MoltenVK_icd.json.
  cmake --preset mac-arm64-release -DREXSDK_DIR="$SDK" -DREXGLUE_ROOT="$SDK" -DREBLUE_PCH=OFF \
    -DREBLUE_ICLOUD_CONTAINER="${ICLOUD_CONTAINER:-}"
}
configure
if [ ! -f generated/sources.cmake ]; then
  cmake --build --preset mac-arm64-release --target reblue_codegen
  configure
fi
cmake --build --preset mac-arm64-release --target reblue -j

PRESET=mac-arm64-release OUT_FILE=reblue-macos-arm64.zip MAX_MACOS=13.3 SIGN_IDENTITY=- \
  bash .github/scripts/package-macos-app.sh
rm -rf dist/reblue.app dist/unpacked
ditto -x -k dist/reblue-macos-arm64.zip dist/unpacked
mv dist/unpacked/reblue.app dist/reblue.app
rm -rf dist/unpacked
