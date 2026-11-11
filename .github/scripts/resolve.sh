#!/usr/bin/env bash
# Decides what a build is: which reblue version, which rexglue-sdk release, and
# what the artifacts are called. Writes them to $GITHUB_OUTPUT.
#
# In:  SDK_REPO, SDK_REF ('latest-nightly' | 'latest-stable' | a tag),
#      ASSET_TAG, GH_TOKEN, GITHUB_OUTPUT
# Out: version, sdk_tag, sdk_short, asset_base
set -euo pipefail

version=$(sed -nE 's/^project\(reblue VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt | head -n1)
if [ -z "${version}" ]; then
  echo "::error::could not parse project version from CMakeLists.txt"
  exit 1
fi

case "${SDK_REF}" in
  latest-nightly)
    sdk_tag=$(gh release list --repo "${SDK_REPO}" --limit 100 \
      --json tagName,isPrerelease,createdAt \
      | jq -r '[.[] | select(.isPrerelease) | select(.tagName | startswith("nightly-"))]
               | sort_by(.createdAt) | last | .tagName // ""')
    ;;
  latest-stable)
    sdk_tag=$(gh release list --repo "${SDK_REPO}" --limit 100 \
      --json tagName,isPrerelease,createdAt \
      | jq -r '[.[] | select(.isPrerelease | not)]
               | sort_by(.createdAt) | last | .tagName // ""')
    ;;
  *)
    sdk_tag="${SDK_REF}"
    ;;
esac
if [ -z "${sdk_tag}" ]; then
  echo "::error::no rexglue-sdk release matched '${SDK_REF}'"
  exit 1
fi

sdk_short=$(printf '%s' "${sdk_tag}" | sed -nE 's/^nightly-[0-9]{8}-([0-9a-f]{1,8}).*/\1/p')
[ -n "${sdk_short}" ] || sdk_short="${sdk_tag}"

{
  echo "version=${version}"
  echo "sdk_tag=${sdk_tag}"
  echo "sdk_short=${sdk_short}"
  echo "asset_base=reblue-${version}${ASSET_TAG}"
} >> "${GITHUB_OUTPUT}"

echo "reblue ${version} against rexglue-sdk ${sdk_tag}"
