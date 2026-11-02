#!/usr/bin/env bash
set -euo pipefail
test -n "${CREATE_FIRMWARE:-}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
make -C "$HERE/apps/gcode-preview" test
make -C "$HERE/apps/gcode-preview" CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ \
    MALI_LIB="$ROOTFS_DIR/usr/lib/libmali.so.1" \
    PROGRESS_LIBS="$ROOTFS_DIR/usr/lib/libboost_json.so.1.83.0 $ROOTFS_DIR/usr/lib/libcurl.so.4"
install -Dm755 "$HERE/apps/gcode-preview/libgcode-preview.so" "$ROOTFS_DIR/usr/local/lib/libgcode-preview.so"
