#!/usr/bin/env bash
# Configures a reblue preset against a downloaded SDK slice. Run once before
# codegen and again after, so the generated sources.cmake is picked up.
#
# REBLUE_PCH=OFF: neither sccache nor ccache caches a precompiled-header
# compilation, so the PCH would cost every run a full rebuild. It also hides a
# TU that stopped including what it uses.
#
# In: PRESET, SDK_SLICE, COMPILER_LAUNCHER (optional, e.g. ccache),
#     REBLUE_UPDATE_BASE (optional), REBLUE_UPDATE_CHANNEL,
#     REBLUE_VERSION_SUFFIX (optional)
set -euo pipefail

args=(--preset "${PRESET}" "-DCMAKE_PREFIX_PATH=${PWD}/sdk/${SDK_SLICE}" -DREBLUE_PCH=OFF
      "-DREBLUE_UPDATE_BASE=${REBLUE_UPDATE_BASE:-}"
      "-DREBLUE_UPDATE_CHANNEL=${REBLUE_UPDATE_CHANNEL:-stable}"
      "-DREBLUE_VERSION_SUFFIX=${REBLUE_VERSION_SUFFIX:-}"
      -DREBLUE_OFFICIAL_BUILD=1)
if [ -n "${COMPILER_LAUNCHER:-}" ]; then
  args+=("-DCMAKE_C_COMPILER_LAUNCHER=${COMPILER_LAUNCHER}"
         "-DCMAKE_CXX_COMPILER_LAUNCHER=${COMPILER_LAUNCHER}")
fi

cmake "${args[@]}"
