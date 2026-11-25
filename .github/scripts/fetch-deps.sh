#!/usr/bin/env bash
# Downloads one platform slice of a rexglue-sdk release into ./sdk/$SDK_SLICE.
# Uses the plain API rather than gh, which the ubuntu:22.04 container lacks.
#
# In: SDK_REPO, SDK_TAG, SDK_SLICE (win-amd64 | linux-amd64 | linux-arm64 |
#     mac-arm64 | mac-amd64), GH_TOKEN, ASSETS_REPO, ASSETS_TOKEN
set -euo pipefail

url=$(curl -fsSL -H "Authorization: Bearer ${GH_TOKEN}" \
  "https://api.github.com/repos/${SDK_REPO}/releases/tags/${SDK_TAG}" \
  | jq -r --arg slice "${SDK_SLICE}" \
      '.assets[] | select(.name | endswith($slice + ".zip")) | .browser_download_url' \
  | head -n1)

if [ -z "${url}" ]; then
  echo "::error::${SDK_REPO} ${SDK_TAG} publishes no ${SDK_SLICE} asset"
  exit 1
fi

curl -fsSL -o sdk.zip "${url}"
unzip -q sdk.zip -d sdk
if [ ! -d "sdk/${SDK_SLICE}" ]; then
  echo "::error::SDK archive has no ${SDK_SLICE}/ directory"
  exit 1
fi
echo "Extracted $(basename "${url}") to sdk/${SDK_SLICE}"

# Does this SDK still export the targets this tree links? It has to be checked
# here rather than left to cmake: generated/rexglue.cmake asks for
# find_package(rexglue 0.10.0 CONFIG) without EXACT, so nightly-20261122-44b7fdc5
# (0.12.0-dev) passes the version gate and then fails in the link step minutes
# later with "target rex::runtime not found", which reads like a broken
# CMakeLists.txt rather than a moving SDK. rexglue-sdk renamed its whole exported
# topology in nightly-20261109-2d51d257 - rex::runtime/rex::o1heap/rex::gpu-xenos
# became rex::base/rex::kernel/rex::xenos - so the message below names both sides.
cmake_dir="sdk/${SDK_SLICE}/lib/cmake/rexglue"
if [ ! -d "${cmake_dir}" ]; then
  echo "::error::${SDK_REPO} ${SDK_TAG} has no ${SDK_SLICE}/lib/cmake/rexglue, so its exported targets cannot be checked"
  exit 1
fi
for target in rex::runtime rex::o1heap; do
  if ! grep -qrE "add_library\(${target} (SHARED|STATIC|INTERFACE) IMPORTED\)" "${cmake_dir}"; then
    echo "::error::${SDK_REPO} ${SDK_TAG} does not export ${target}, which this tree links; the SDK changed its exported targets. Last release that still exports them: nightly-20261002-bd833a2a. Pin that in the workflow's sdk_ref input, or move this tree to the new names."
    echo "Targets ${SDK_TAG} does export:"
    grep -rhoE 'add_library\(rex::[a-z0-9_-]+ ' "${cmake_dir}" \
      | sed 's/add_library(//;s/ $//' | sort -u | tr '\n' ' '
    echo
    exit 1
  fi
done
echo "${SDK_TAG} exports rex::runtime and rex::o1heap"

rm -rf assets
git clone --depth 1 \
  "https://x-access-token:${ASSETS_TOKEN}@github.com/${ASSETS_REPO}.git" assets
git -C assets remote set-url origin "https://github.com/${ASSETS_REPO}.git"
echo "Cloned ${ASSETS_REPO} to assets"
