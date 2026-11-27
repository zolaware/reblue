#!/usr/bin/env bash
# Brings an ubuntu:24.04 container up to what reblue needs. 24.04 is the build
# base because the SDK's own binaries require GLIBC_2.38, which 22.04 cannot
# provide. Its stock clang is 18, so C++23 comes from apt.llvm.org.
set -eux

apt-get update
apt-get install -y --no-install-recommends \
  ca-certificates curl wget gnupg jq unzip zip git git-lfs file \
  software-properties-common lsb-release \
  binutils ccache cmake ninja-build python3 \
  g++-13 libvulkan-dev pkg-config desktop-file-utils \
  libcurl4-openssl-dev

# The SDK's own tools, rexglue among them, will not load without these.
apt-get install -y --no-install-recommends \
  libgtk-3-dev libx11-xcb-dev libxss-dev \
  libwayland-dev libwayland-bin wayland-protocols libxkbcommon-dev libdecor-0-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev

wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh
chmod +x /tmp/llvm.sh
/tmp/llvm.sh 19
ln -sf /usr/bin/clang-19 /usr/bin/clang
ln -sf /usr/bin/clang++-19 /usr/bin/clang++
ln -sf /usr/bin/lld-19 /usr/bin/ld.lld

cmake --version
clang++ --version
