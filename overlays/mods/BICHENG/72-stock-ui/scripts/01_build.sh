#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
GUI_SHA=$(sha256sum "$ROOTFS_DIR/usr/bin/gui" | cut -d" " -f1)
grep -Fq ".sha256=\"$GUI_SHA\"," "$HERE/apps/stock-ui/stock-abi.h"
make -C "$HERE/apps/stock-ui" test
make -C "$HERE/apps/stock-ui" CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ MALI_LIB="$ROOTFS_DIR/usr/lib/libmali.so.1" \
    PROGRESS_LIBS="$ROOTFS_DIR/usr/lib/libboost_json.so.1.83.0 $ROOTFS_DIR/usr/lib/libcurl.so.4"
install -Dm755 "$HERE/apps/stock-ui/libstock-ui.so" "$ROOTFS_DIR/usr/local/lib/libstock-ui.so"
