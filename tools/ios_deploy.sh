#!/bin/zsh
# Build the iOS runtime, sign it through packaging/ios/xcode and install it.
#   tools/ios_deploy.sh <TEAMID> <bundle id> <device UDID> [--launch]
set -euo pipefail
cd "$(dirname "$0")/.."
team=$1 bundle=$2 device=$3
cmake --build out/build/ios-arm64 --target reblue
rm -rf packaging/ios/xcode/build
(cd packaging/ios/xcode && { [[ -d reblueIOS.xcodeproj ]] || xcodegen; } &&
  xcodebuild -quiet -project reblueIOS.xcodeproj -target reblue -configuration Release     -sdk iphoneos DEVELOPMENT_TEAM="$team" REBLUE_IOS_BUNDLE_ID="$bundle" -allowProvisioningUpdates build)
xcrun devicectl device install app --device "$device" packaging/ios/xcode/build/Release-iphoneos/reblue.app >/dev/null
echo "installed $bundle"
if [[ ${4:-} == --launch ]]; then
  xcrun devicectl device process launch --console --terminate-existing --device "$device" "$bundle"
fi
