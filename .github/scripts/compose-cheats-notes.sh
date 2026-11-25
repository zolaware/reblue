#!/usr/bin/env bash
# Release notes for the rolling cheats prerelease: what is in the download, which
# commit built it, and what is not promised. The cheats page is documented in
# README.md, so the notes point there instead of repeating it.
#
# In: VERSION, SDK_TAG, COMMIT, TAG, GITHUB_REPOSITORY (set by Actions)
set -euo pipefail

commit="${COMMIT:-${GITHUB_SHA:-}}"
short="${commit:0:7}"
repo="${GITHUB_REPOSITORY:-}"

cat > notes.md <<EOF
# reblue ${VERSION} with cheats

Built from \`${short}\` against rexglue-sdk \`${SDK_TAG}\`.

Downloads:

- \`reblue-${VERSION}-cheats-win-amd64.zip\` - Windows x86-64. Contains
  \`reblue.exe\` (DX12) and \`reblue_vk.exe\` (Vulkan).
- \`reblue-${VERSION}-cheats-x86_64.AppImage\` - Linux amd64, including Steam Deck.
- \`reblue-${VERSION}-cheats-aarch64.AppImage\` - Linux arm64.

This is a rolling prerelease (\`${TAG}\`): each build replaces the one before it,
so the link stays current and there is nothing to hunt for. It is not a stable
release and it does not self-update.

Still needs your own copy of Blue Dragon, exactly as upstream does: point the
first-run wizard at your discs or disc images. Cheats live on their own page in
the config menu, are live without a restart, and save per profile. What they all
do is in the Cheats section of
[\`README.md\`](https://github.com/${repo}/blob/${TAG}/README.md).
EOF

echo "Wrote notes.md for reblue ${VERSION} @ ${short}"
