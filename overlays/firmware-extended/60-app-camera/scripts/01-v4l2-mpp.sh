#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-PackageHomePage: https://github.com/paxx12-snapmaker-u1/SnapmakerU1-Extended-Firmware
# SPDX-FileCopyrightText: Copyright (c) 2026 @paxx12

GIT_URL=https://github.com/paxx12/v4l2-mpp.git
GIT_SHA=87bda8efbb7b32d8d57fc6a8d76dfae9418a2190
MPP_SHA=437bfbeb9567cca9cd9080e3f6954aa9d6a94f18

if [[ -z "$CREATE_FIRMWARE" ]]; then
  echo "Error: This script should be run within the create_firmware.sh environment."
  exit 1
fi

set -eo pipefail

TARGET_DIR="$CACHE_DIR/v4l2-mpp"
cache_git.sh "$TARGET_DIR" "$GIT_URL" "$GIT_SHA"

MPP_DIR="$TARGET_DIR/deps/mpp"
MPP_PATCH="$(realpath "$(dirname "$0")/../apps/mpp-force-idr.patch")"
if [[ "$(git -C "$MPP_DIR" rev-parse HEAD)" != "$MPP_SHA" ]]; then
  echo "Error: MPP backport requires pinned revision $MPP_SHA." >&2
  exit 1
fi
# Upstream cf90e9328f474ace01579d54d08485a4d3407724, applied once per cache checkout.
if ! git -C "$MPP_DIR" apply --reverse --check "$MPP_PATCH" 2>/dev/null; then
  git -C "$MPP_DIR" apply --check "$MPP_PATCH"
  git -C "$MPP_DIR" apply "$MPP_PATCH"
fi

echo ">> Setting up cross-compilation environment..."
export CROSS_COMPILE=aarch64-linux-gnu-
export CC="${CROSS_COMPILE}gcc"
export CXX="${CROSS_COMPILE}g++"
export AR="${CROSS_COMPILE}ar"
export RANLIB="${CROSS_COMPILE}ranlib"
export STRIP="${CROSS_COMPILE}strip"

echo ">> Compiling dependencies..."
# MPP's recursive archive merge also scans usr-local. Exclude the previous
# installed archive and rebuild the combined archive from cached objects.
rm -f "$MPP_DIR/usr-local/lib/librockchip_mpp.a" "$MPP_DIR/mpp/librockchip_mpp.a"
make -C "$TARGET_DIR" deps
if [[ "$("$AR" t "$MPP_DIR/usr-local/lib/librockchip_mpp.a" | awk '$0 == "mpp_enc_impl.c.o" { n++ } END { print n+0 }')" != 1 ]]; then
  echo "Error: MPP static archive must contain exactly one encoder implementation." >&2
  exit 1
fi

echo ">> Compiling v4l2-mpp applications..."
# These targets do not list the static MPP archive as a dependency.
make -C "$TARGET_DIR/apps/capture-v4l2-jpeg-mpp" -B
make -C "$TARGET_DIR/apps/capture-v4l2-raw-mpp" -B
make -C "$TARGET_DIR" install DESTDIR="$1"

echo ">> Validate binaries..."
stat "$1/usr/local/bin/capture-v4l2-jpeg-mpp" >/dev/null
stat "$1/usr/local/bin/capture-v4l2-raw-mpp" >/dev/null
stat "$1/usr/local/bin/stream-rtsp" >/dev/null
stat "$1/usr/local/bin/stream-webrtc" >/dev/null
stat "$1/usr/local/bin/stream-http.py" >/dev/null
stat "$1/usr/local/bin/control-v4l2.py" >/dev/null
echo ">> v4l2-mpp installation completed successfully."
