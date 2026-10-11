#!/usr/bin/env bash
# Builds a self-contained, signed reblue.app and zips it with ditto.
# Wraps the same bundle in a drag-to-Applications disk image.
#
# In: PRESET, OUT_FILE, MAX_MACOS, SIGN_IDENTITY ('-' for ad-hoc)
set -euo pipefail

BUILD_DIR="out/build/${PRESET}"
DMG_FILE="${DMG_FILE:-${OUT_FILE%.zip}.dmg}"
VERSION=$(sed -nE 's/^project\(reblue VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt | head -n1)

if [ ! -x "${BUILD_DIR}/reblue" ]; then
  echo "::error::missing executable ${BUILD_DIR}/reblue"
  exit 1
fi
if [ ! -f "${BUILD_DIR}/vulkan/lib/libMoltenVK.dylib" ]; then
  echo "::error::missing MoltenVK runtime in ${BUILD_DIR}/vulkan/lib"
  exit 1
fi

# True when dotted version $1 is greater than $2. Kept independent of GNU sort
# so this works with the stock tools on older macOS releases.
version_gt() {
  awk -v a="$1" -v b="$2" 'BEGIN {
    split(a, av, "."); split(b, bv, ".");
    for (i = 1; i <= 4; ++i) {
      ai = av[i] + 0; bi = bv[i] + 0;
      if (ai > bi) exit 0;
      if (ai < bi) exit 1;
    }
    exit 1;
  }'
}

ARCHS="$(lipo -archs "${BUILD_DIR}/reblue")"
STAGING="$(mktemp -d "${TMPDIR:-/tmp}/reblue-macos-package.XXXXXX")"
trap 'rm -rf "${STAGING}"' EXIT

APP_BUNDLE="${STAGING}/reblue.app"
CONTENTS="${APP_BUNDLE}/Contents"
MACOS_DIR="${CONTENTS}/MacOS"
RESOURCES_DIR="${CONTENTS}/Resources"
VK_LIB_DIR="${MACOS_DIR}/vulkan/lib"
mkdir -p "${MACOS_DIR}" "${RESOURCES_DIR}" "${VK_LIB_DIR}"

cp "${BUILD_DIR}/reblue" "${MACOS_DIR}/reblue"
chmod 755 "${MACOS_DIR}/reblue"

# Follow the dependency graph rather than globbing the build dir, so optional
# profiler libraries sitting next to the binary do not get shipped.
queue=("${MACOS_DIR}/reblue")
seen='|reblue|'
queue_index=0
while [ ${queue_index} -lt ${#queue[@]} ]; do
  current="${queue[${queue_index}]}"
  while IFS= read -r dep; do
    case "${dep}" in
      @rpath/*|@loader_path/*) name="${dep##*/}" ;;
      *) continue ;;
    esac
    case "${seen}" in *"|${name}|"*) continue ;; esac
    source_lib="${BUILD_DIR}/${name}"
    if [ ! -f "${source_lib}" ]; then
      echo "::error::${current} requires ${dep}, but ${source_lib} is missing"
      exit 1
    fi
    cp "${source_lib}" "${MACOS_DIR}/${name}"
    chmod u+w "${MACOS_DIR}/${name}"
    queue+=("${MACOS_DIR}/${name}")
    seen="${seen}${name}|"
  done < <(otool -L "${current}" | tail -n +2 | awk '{print $1}')
  queue_index=$((queue_index + 1))
done

# Plume's volk opens libvulkan.dylib first and libMoltenVK.dylib second. Point
# both leaf names at the bundled MoltenVK so the app needs neither a separately
# installed Vulkan loader nor an external ICD manifest.
cp "${BUILD_DIR}/vulkan/lib/libMoltenVK.dylib" "${VK_LIB_DIR}/libMoltenVK.dylib"
chmod u+w "${VK_LIB_DIR}/libMoltenVK.dylib"
ln -s libMoltenVK.dylib "${VK_LIB_DIR}/libvulkan.dylib"

# Symlinks are deliberately excluded: codesign signs the MoltenVK target and
# the bundle seal records the alias.
macho_files=()
while IFS= read -r -d '' candidate; do
  if file "${candidate}" | grep -q 'Mach-O'; then
    macho_files+=("${candidate}")
  fi
done < <(find "${MACOS_DIR}" -type f -print0)

if [ ${#macho_files[@]} -eq 0 ]; then
  echo "::error::application contains no Mach-O files"
  exit 1
fi

minimum_macos() {
  otool -l "$1" | awk '
    $1 == "cmd" && $2 == "LC_BUILD_VERSION" { build = 1; legacy = 0; next }
    $1 == "cmd" && $2 == "LC_VERSION_MIN_MACOSX" { legacy = 1; build = 0; next }
    build && $1 == "minos" { print $2; exit }
    legacy && $1 == "version" { print $2; exit }
  '
}

package_min_macos=0.0
macos_violations=()
for binary in "${macho_files[@]}"; do
  minos="$(minimum_macos "${binary}")"
  if [ -z "${minos}" ]; then
    echo "::error::could not read the macOS deployment target from ${binary}"
    exit 1
  fi
  if version_gt "${minos}" "${package_min_macos}"; then
    package_min_macos="${minos}"
  fi
  if version_gt "${minos}" "${MAX_MACOS}"; then
    macos_violations+=("${binary#${APP_BUNDLE}/}: requires macOS ${minos}")
  fi
  for arch in ${ARCHS}; do
    if ! lipo "${binary}" -verify_arch "${arch}" >/dev/null 2>&1; then
      echo "::error::${binary#${APP_BUNDLE}/} does not contain architecture ${arch}"
      exit 1
    fi
  done
done

if [ ${#macos_violations[@]} -gt 0 ]; then
  printf '::error::%s\n' "${macos_violations[@]}"
  echo "refusing to package above the macOS ${MAX_MACOS} ceiling. Rebuild reblue" >&2
  echo "and the SDK with CMAKE_OSX_DEPLOYMENT_TARGET=${MAX_MACOS} or lower." >&2
  exit 1
fi

# Ensure no Homebrew, SDK or other build-host dependency escaped into the app.
macho_loads() {
  otool -l "$1" | awk '
    $1 == "cmd" && ($2 == "LC_LOAD_DYLIB" ||
                    $2 == "LC_LOAD_WEAK_DYLIB" ||
                    $2 == "LC_REEXPORT_DYLIB" ||
                    $2 == "LC_LOAD_UPWARD_DYLIB") { want_name = 1; next }
    want_name && $1 == "name" { print $2; want_name = 0 }
  '
}

dependency_errors=()
for binary in "${macho_files[@]}"; do
  while IFS= read -r dep; do
    case "${dep}" in
      /System/Library/*|/usr/lib/*) ;;
      @rpath/*)
        name="${dep##*/}"
        [ -e "${MACOS_DIR}/${name}" ] || [ -e "${VK_LIB_DIR}/${name}" ] ||
          dependency_errors+=("${binary#${APP_BUNDLE}/}: unresolved ${dep}")
        ;;
      @executable_path/*)
        relative="${dep#@executable_path/}"
        [ -e "${MACOS_DIR}/${relative}" ] ||
          dependency_errors+=("${binary#${APP_BUNDLE}/}: unresolved ${dep}")
        ;;
      @loader_path/*)
        relative="${dep#@loader_path/}"
        [ -e "$(dirname "${binary}")/${relative}" ] ||
          dependency_errors+=("${binary#${APP_BUNDLE}/}: unresolved ${dep}")
        ;;
      *) dependency_errors+=("${binary#${APP_BUNDLE}/}: host dependency ${dep}") ;;
    esac
  done < <(macho_loads "${binary}")
done

if [ ${#dependency_errors[@]} -gt 0 ]; then
  printf '::error::%s\n' "${dependency_errors[@]}"
  echo "application is not self-contained" >&2
  exit 1
fi

# sips can decode ICO but cannot resize it, so flatten its largest frame to PNG
# once before generating the iconset. The source is 256px, so the Retina slots
# are upscaled to hand Finder a complete icon family.
ICONSET="${STAGING}/reblue.iconset"
ICON_SOURCE="${STAGING}/reblue-icon-source.png"
mkdir -p "${ICONSET}"
sips -s format png res/reblue.ico --out "${ICON_SOURCE}" >/dev/null
make_icon() {
  sips -z "$1" "$1" "${ICON_SOURCE}" --out "${ICONSET}/$2" >/dev/null
}
make_icon 16 icon_16x16.png
make_icon 32 icon_16x16@2x.png
make_icon 32 icon_32x32.png
make_icon 64 icon_32x32@2x.png
make_icon 128 icon_128x128.png
make_icon 256 icon_128x128@2x.png
make_icon 256 icon_256x256.png
make_icon 512 icon_256x256@2x.png
make_icon 512 icon_512x512.png
make_icon 1024 icon_512x512@2x.png
if ! iconutil -c icns "${ICONSET}" -o "${RESOURCES_DIR}/reblue.icns" 2>/dev/null; then
  # Some beta macOS toolchains reject otherwise valid iconsets.
  echo "::warning::iconutil rejected the iconset; using the ICO's largest frame"
  sips -s format icns "${ICON_SOURCE}" --out "${RESOURCES_DIR}/reblue.icns" >/dev/null
fi

cat > "${CONTENTS}/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleDisplayName</key>
    <string>re:Blue</string>
    <key>CFBundleExecutable</key>
    <string>reblue</string>
    <key>CFBundleIconFile</key>
    <string>reblue.icns</string>
    <key>CFBundleIdentifier</key>
    <string>com.zolaware.reblue</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>re:Blue</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.games</string>
    <key>LSMinimumSystemVersion</key>
    <string>${package_min_macos}</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
PLIST
plutil -lint "${CONTENTS}/Info.plist" >/dev/null

# Strip build-host metadata, then sign nested code from the inside out. --deep
# is retained for strict verification only, never for mutation.
xattr -cr "${APP_BUNDLE}"
sign_args=(--force --sign "${SIGN_IDENTITY}")
if [ "${SIGN_IDENTITY}" != - ]; then
  sign_args+=(--options runtime --timestamp)
fi
for binary in "${macho_files[@]}"; do
  if [ "${binary}" != "${MACOS_DIR}/reblue" ]; then
    codesign "${sign_args[@]}" "${binary}"
  fi
done
codesign "${sign_args[@]}" --entitlements res/macos/reblue.entitlements "${MACOS_DIR}/reblue"
codesign "${sign_args[@]}" --entitlements res/macos/reblue.entitlements "${APP_BUNDLE}"
codesign --verify --deep --strict --verbose=2 "${APP_BUNDLE}"

mkdir -p dist
ditto -c -k --sequesterRsrc --keepParent "${APP_BUNDLE}" "dist/${OUT_FILE}"

DMG_STAGING="${STAGING}/dmg"
mkdir -p "${DMG_STAGING}"
mv "${APP_BUNDLE}" "${DMG_STAGING}/reblue.app"
ln -s /Applications "${DMG_STAGING}/Applications"
hdiutil create -quiet -ov -volname "reblue ${VERSION}" -srcfolder "${DMG_STAGING}" \
  -fs HFS+ -format UDZO -imagekey zlib-level=9 "dist/${DMG_FILE}"

if [ "${SIGN_IDENTITY}" != - ]; then
  codesign --force --sign "${SIGN_IDENTITY}" --timestamp "dist/${DMG_FILE}"
fi

echo "Packaged dist/${OUT_FILE} ($(du -sh "dist/${OUT_FILE}" | awk '{print $1}'); minimum macOS ${package_min_macos})"
echo "Packaged dist/${DMG_FILE} ($(du -sh "dist/${DMG_FILE}" | awk '{print $1}'))"
