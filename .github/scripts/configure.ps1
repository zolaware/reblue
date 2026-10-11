# Configures the Windows preset against the downloaded SDK. Run once before
# codegen and again after, so the generated sources.cmake is picked up.
#
# REBLUE_PCH=OFF: sccache does not cache a precompiled-header compilation, so
# the PCH would cost every run a full rebuild. It also hides a TU that stopped
# including what it uses.
#
# In: PRESET, REBLUE_UPDATE_BASE (optional), REBLUE_UPDATE_CHANNEL,
#     REBLUE_VERSION_SUFFIX (optional)
$ErrorActionPreference = 'Stop'

$sdk = Join-Path $PWD "sdk/win-amd64"
$channel = if ($env:REBLUE_UPDATE_CHANNEL) { $env:REBLUE_UPDATE_CHANNEL } else { "stable" }
cmake --preset $env:PRESET `
  "-DCMAKE_PREFIX_PATH=$sdk" `
  "-DREBLUE_UPDATE_BASE=$env:REBLUE_UPDATE_BASE" `
  "-DREBLUE_UPDATE_CHANNEL=$channel" `
  "-DREBLUE_VERSION_SUFFIX=$env:REBLUE_VERSION_SUFFIX" `
  "-DREBLUE_OFFICIAL_BUILD=1" `
  "-DREBLUE_PCH=OFF" `
  "-DCMAKE_C_COMPILER_LAUNCHER=sccache" `
  "-DCMAKE_CXX_COMPILER_LAUNCHER=sccache"
